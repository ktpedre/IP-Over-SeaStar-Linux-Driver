/*******************************************************************************
    SeaStar NIC Linux Driver
    Copyright (C) 2009 Cray Inc. and Sandia National Laboratories

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Contact Information:
    Kevin Pedretti <ktpedre@sandia.gov>
    Scalable System Software Dept.
    Sandia National Laboratories
    P.O. Box 5800 MS 1319
    Albuquerque, NM 87185

*******************************************************************************/

#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include "firmware.h"
#include "seastar.h"


/**
 * Maps a region of host memory into the SeaStar.
 */
static void seastar_map_host_region(struct ss_priv *ssp, const void *addr)
{
	/* Round addr to the nearest 128 MB */
	unsigned long raw_paddr = __pa(addr);
	unsigned long paddr = raw_paddr & ~((1 << 28) - 1);

	htb_map[8] = 0x8000 | ((paddr >> 28) + 0);
	htb_map[9] = 0x8000 | ((paddr >> 28) + 1);

	ssp->host_region_phys = paddr;
}


/**
 * Converts a kernel virtual address to a SeaStar address.
 */
static uint32_t virt_to_fw(struct ss_priv *ssp, void *addr)
{
	unsigned long saddr;

	saddr = __pa(addr) - ssp->host_region_phys;
	saddr &= (2 << 28) - 1;
	saddr += (8 << 28);

	return saddr;
}


/**
 * Send a command to the Seastar.
 */
static uint32_t seastar_cmd(struct ss_priv *ssp, const struct command *cmd,
			    int wait_for_result)
{
	struct mailbox *mbox = ssp->mailbox;
	unsigned int next_write;
	uint32_t tail, result;

	/* Copy the command into the mailbox */
	mbox->commandq[ssp->mailbox_cached_write] = *cmd;
	next_write = ssp->mailbox_cached_write + 1;
	if (next_write == COMMAND_Q_LENGTH)
		next_write = 0;

	/* Wait until it is safe to advance the write pointer */
	while (next_write == ssp->mailbox_cached_read)
		ssp->mailbox_cached_read = mbox->commandq_read;

	/* Advance the write pointer */
	mbox->commandq_write       = next_write;
	ssp->mailbox_cached_write = next_write;

	if (!wait_for_result)
		return 0;

	/* Wait for the result to arrive */
	tail = mbox->resultq_read;
	while (tail == mbox->resultq_write)
		;

	/* Read the result */
	result = mbox->resultq[tail];
	mbox->resultq_read = (tail >= RESULT_Q_LENGTH - 1) ? 0 : tail + 1;

	return result;
}


/**
 * Sends a datagram transmit command to the SeaStar.
 */
void seastar_ip_tx_cmd(struct ss_priv *ssp, uint16_t nid, uint16_t length,
		       uint64_t address, uint16_t pending_index)
{
	struct command_ip_tx tx_cmd = {
		.op		= COMMAND_IP_TX,
		.nid		= nid,
		.length		= length,
		.address	= address,
		.pending_index	= pending_index,
	};

	seastar_cmd(ssp, (struct command *) &tx_cmd, 0);
}


/**
 * Programs the SeaStar's HTB_BI register.
 */
void seastar_setup_htb_bi(uint32_t idr)
{
	/* Mask the APIC dest setup by Linux, causes problems with SeaStar */
	idr &= 0xFFFF0000;

	*htb_bi = 0xFD000000 | (idr >> 8);
}


/**
 * Brings up the low-level Seastar hardware.
 */
int seastar_hw_init(struct ss_priv *ssp)
{
	uint32_t lower_memory = SEASTAR_HOST_BASE;
	const int num_eq = 1;
	uint32_t lower_pending;
	uint32_t lower_eqcb;
	uint32_t result;
	struct command_init init_cmd;
	struct command_init_eqcb eqcb_cmd;
	struct command_mark_alive alive_cmd;

	/* Read our NID from SeaStar and write it to the NIC control block */
	niccb->local_nid = *tx_source;

	printk(KERN_INFO "%s: nid %d (0x%x) version %x built %x\n",
		__func__,
		niccb->local_nid,
		niccb->local_nid,
		niccb->version,
		niccb->build_time
	);

	/* Allocate the PPC memory */
	lower_pending = lower_memory;
	lower_memory += NUM_PENDINGS * FW_PENDING_SIZE;

	lower_eqcb = lower_memory;
	lower_memory = num_eq * FW_EQCB_SIZE;

	/* Initialize the HTB map so that the Seastar can see our memory.
	 * Since we are only doing upper pendings, we just use the
	 * upper_pending_phys instead of the host_phys area. */
	seastar_map_host_region(ssp, ssp);

	ssp->mailbox			= &seastar_mailbox[0];
	ssp->mailbox_cached_read	= ssp->mailbox->commandq_read;
	ssp->mailbox_cached_write	= ssp->mailbox->commandq_write;

	/* Attempt to send a setup command to the NIC */
	init_cmd.op			= COMMAND_INIT;
	init_cmd.process_index		= 1;
	init_cmd.uid			= 0;
	init_cmd.jid			= 0;

	init_cmd.num_pendings		= NUM_PENDINGS;
	init_cmd.pending_tx_limit	= NUM_TX_PENDINGS;
	init_cmd.pending_table_addr	= lower_pending;
	init_cmd.up_pending_table_addr	= virt_to_fw(ssp, ssp->pending_table);
	init_cmd.up_pending_table_ht_addr = 0;

	init_cmd.num_memds		= 0;
	init_cmd.memd_table_addr	= 0;

	init_cmd.num_eqcbs		= num_eq;
	init_cmd.eqcb_table_addr	= lower_eqcb;
	init_cmd.eqheap_addr		= virt_to_fw(ssp, ssp->eq);
	init_cmd.eqheap_length		= NUM_EQ_ENTRIES * sizeof(ssp->eq[0]);

	init_cmd.shdr_table_ht_addr	= 0;
	init_cmd.result_block_addr	= 0;
	init_cmd.smb_table_addr		= 0;

	result = seastar_cmd(ssp, (struct command *) &init_cmd, 1);
	if (result != 0) {
		dev_err(&ssp->pdev->dev,
			"init command failed, result=%d.\n", result);
		return -1;
	}

	eqcb_cmd.op			= COMMAND_INIT_EQCB;
	eqcb_cmd.eqcb_index		= 0;
	eqcb_cmd.base			= virt_to_fw(ssp, ssp->eq);
	eqcb_cmd.count			= NUM_EQ_ENTRIES;

	result = seastar_cmd(ssp, (struct command *) &eqcb_cmd, 1);
	if (result != 1) {
		dev_err(&ssp->pdev->dev,
			"init_eqcb command failed, result=%d.\n", result);
		return -1;
	}

	alive_cmd.op			= COMMAND_MARK_ALIVE;
	alive_cmd.index			= 1;

	result = seastar_cmd(ssp, (struct command *) &alive_cmd, 1);
	if (result != 0) {
		dev_err(&ssp->pdev->dev,
			"mark_alive command failed, result=%d\n", result);
		return -1;
	}

	return 0;
}
