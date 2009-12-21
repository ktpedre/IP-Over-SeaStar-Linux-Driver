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

#ifndef _SEASTAR_FIRMWARE_H
#define _SEASTAR_FIRMWARE_H


/**
 * Number of entries in Host -> SeaStar command queue.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
#define COMMAND_Q_LENGTH		63


/**
 * Number of entries in SeaStar -> Host result queue.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
#define RESULT_Q_LENGTH			2


/**
 * SeaStar -> Host event types.
 *
 * WARNING: These must match the definitions used by the
 *          closed-source SeaStar firmware.
 */
#define EVENT_TX_END			125
#define EVENT_RX			126
#define EVENT_RX_EMPTY			127


/**
 * Host -> SeaStar command types.
 *
 * WARNING: These must match the definitions used by the
 *          closed-source SeaStar firmware.
 */
#define COMMAND_INIT			0
#define COMMAND_MARK_ALIVE		1
#define COMMAND_INIT_EQCB		2
#define COMMAND_IP_TX			13


/**
 * Number of entries in the incoming datagram buffer table.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
#define NUM_SKBS			64


/**
 * Size of the pending structure used by the SeaStar firmware.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
#define FW_PENDING_SIZE			32


/**
 * Size of the event queue control block structure used by the SeaStar firmware.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
#define FW_EQCB_SIZE			32


/**
 * SeaStar addresses of important structures in SeaStar memory.
 *
 * WARNING: These must match the definitions used by the
 *          closed-source SeaStar firmware.
 */
#define SEASTAR_SCRATCH_BASE		0xFFFA0000
#define SEASTAR_TX_SOURCE		0xFFE00108
#define SEASTAR_MAILBOX_BASE		0xFFFA0000
#define SEASTAR_SKB_BASE		0xFFFA4000
#define SEASTAR_HOST_BASE		0xFFFA5000
#define SEASTAR_HTB_BASE		0xFFE20000
#define SEASTAR_HTB_BI			0xFFE20048
#define SEASTAR_NICCB_BASE		0xFFFFE000


/**
 * Kernel virtual address where the SeaStar memory is mapped.
 */
#define SEASTAR_VIRT_BASE		(0xFFFFFFFFull << 32)


/**
 * Kernel virtual address of the SeaStar's NIC control block.
 */
static volatile struct niccb * const niccb
	= (void *)(SEASTAR_VIRT_BASE + SEASTAR_NICCB_BASE);


/**
 * Kernel virtual address of the SeaStar's HTB_BI register.
 */
static volatile uint32_t * const htb_bi
	= (void *)(SEASTAR_VIRT_BASE + SEASTAR_HTB_BI);


/**
 * Kernel virtual address of the SeaStar's HyperTransport map.
 */
static volatile uint32_t * const htb_map
	= (void *)(SEASTAR_VIRT_BASE + SEASTAR_HTB_BASE);


/**
 * Kernel virtual address of the Host <-> SeaStar mailbox.
 */
static struct mailbox * const seastar_mailbox
	= (void *)(SEASTAR_VIRT_BASE + SEASTAR_MAILBOX_BASE);


/**
 * Kernel virtual address of the incoming datagram buffer table.
 */
static volatile uint64_t * const seastar_skb
	= (void *)(SEASTAR_VIRT_BASE + SEASTAR_SKB_BASE);


/**
 * Kernel virtual address of the SeaStar TX Source register.
 */
static volatile uint16_t * const tx_source
	= (void *)(SEASTAR_VIRT_BASE + SEASTAR_TX_SOURCE);


/**
 * The SeaStar NIC Control Block.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct niccb {
	uint32_t	version;			/* 0   */
	uint8_t		pad[24];
	uint32_t	build_time;			/* 28  */
	uint8_t		pad2[68];
	uint32_t	ip_tx;				/* 100 */
	uint32_t	ip_tx_drop;			/* 104 */
	uint32_t	ip_rx;				/* 108 */
	uint32_t	ip_rx_drop;			/* 112 */
	uint8_t		pad3[52];
	uint16_t	local_nid;			/* 168 */
} __attribute__((packed, aligned));


/**
 * SeaStar datagram packet wire header.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct sshdr {
	uint16_t	length;				/* 0 */
	uint8_t		lo_macs;			/* 2 */
	uint8_t		hdr_type;			/* 3 */
} __attribute__((packed));


/**
 * Generic Host -> SeaStar command structure.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct command {
	uint8_t		op;				/* 0      */
	uint8_t		pad[63];			/* [1,63] */
} __attribute__((packed));


/**
 * Initialize firmware command.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct command_init {
	uint8_t		op;				/* 0  */
	uint8_t		process_index;			/* 1  */
	uint16_t	pad;				/* 2  */
	uint16_t	pid;				/* 4  */
	uint16_t	jid;				/* 6  */
	uint16_t	num_pendings;			/* 8  */
	uint16_t	num_memds;			/* 10 */
	uint16_t	num_eqcbs;			/* 12 */
	uint16_t	pending_tx_limit;		/* 14 */
	uint32_t	pending_table_addr;		/* 16 */
	uint32_t	up_pending_table_addr;		/* 20 */
	uint32_t	up_pending_table_ht_addr;	/* 24 */
	uint32_t	memd_table_addr;		/* 28 */
	uint32_t	eqcb_table_addr;		/* 32 */
	uint32_t	shdr_table_ht_addr;		/* 36 */
	uint32_t	result_block_addr;		/* 40 */
	uint32_t	eqheap_addr;			/* 44 */
	uint32_t	eqheap_length;			/* 48 */
	uint32_t	smb_table_addr;			/* 52 */
	uint32_t	uid;				/* 56 */
} __attribute__((packed));


/**
 * Start firmware running command.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct command_mark_alive {
	uint8_t		op;				/* 0 */
	uint8_t		index;				/* 1 */
} __attribute__((packed));


/**
 * Initialize event queue command.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct command_init_eqcb {
	uint8_t		op;				/* 0 */
	uint8_t 	pad;				/* 1 */
	uint16_t	eqcb_index;			/* 2 */
	uint32_t	base;				/* 4 */
	uint32_t	count;				/* 8 */
} __attribute__((packed));


/**
 * Send datagram command.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct command_ip_tx {
	uint8_t		op;				/* 0  */
	uint8_t		pad;				/* 1  */
	uint16_t	nid;				/* 2  */
	uint16_t	length;				/* 4  */
	uint16_t	pad2;				/* 6  */
	uint64_t	address;			/* 8  */
	uint16_t	pending_index;			/* 16 */
} __attribute__((packed));


/**
 * Host <-> SeaStar Mailbox structure.
 *
 * WARNING: This must match the definition used by the
 *          closed-source SeaStar firmware.
 */
struct mailbox {
	volatile struct command		commandq[COMMAND_Q_LENGTH]; /* 0    */
	volatile uint32_t		resultq[RESULT_Q_LENGTH];   /* 4032 */

	volatile uint32_t		resultq_read;		    /* 4040 */
	volatile uint32_t		resultq_write;		    /* 4044 */
	volatile uint32_t		commandq_write;		    /* 4048 */
	volatile uint32_t		commandq_read;		    /* 4052 */
} __attribute__((packed, aligned(PAGE_SIZE)));


struct ss_priv;


extern void
seastar_ip_tx_cmd(
	struct ss_priv		*ssp,
	uint16_t		nid,
	uint16_t		length,
	uint64_t		address,
	uint16_t		pending_index
);


void
seastar_setup_htb_bi(
	uint32_t		idr
);


extern int
seastar_hw_init(
	struct ss_priv		*ssp
);


#endif
