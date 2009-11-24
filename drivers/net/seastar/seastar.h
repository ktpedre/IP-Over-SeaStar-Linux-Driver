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

#ifndef _SEASTAR_H
#define _SEASTAR_H


/**
 * Rounds up to the nearest quadbyte.
 */
#define ROUNDUP4(val)   ((val + (4-1)) & ~(4-1))


/**
 * SeaStar datagram packet maximum transfer unit size in bytes.
 */
#define SEASTAR_MTU		8192


/**
 * Number of transmit and receive pending structures.
 */
#define NUM_TX_PENDINGS		64
#define NUM_RX_PENDINGS		64
#define NUM_PENDINGS		(NUM_TX_PENDINGS + NUM_RX_PENDINGS)


/**
 * Number of entries in the SeaStar -> Host event queue.
 */
#define NUM_EQ_ENTRIES		1024


/**
 * When allocating an SKB, allocate this many bytes extra.
 */
#define SKB_PAD			(16 - sizeof(struct sshdr))


/**
 * Pending structure.
 * One of these is used to track each in progress transmit.
 */
struct pending {
	struct sk_buff *	skb;
	struct pending *	next;
	void *			bounce;
};


/**
 * SeaStar driver private data.
 */
struct ss_priv {
	spinlock_t		lock;

	unsigned long		host_region_phys;

	volatile uint64_t *	skb_table_phys;
	struct sk_buff *	skb_table_virt[NUM_SKBS];

	struct pending		pending_table[NUM_PENDINGS];
	struct pending *	tx_pending_free_list;

	uint32_t		eq[NUM_EQ_ENTRIES];
	unsigned int		eq_read;

	struct mailbox *	mailbox;
	unsigned int		mailbox_cached_read;
	unsigned int		mailbox_cached_write;

	struct pci_dev *	pdev;
};


#endif
