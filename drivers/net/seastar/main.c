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
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/htirq.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <net/arp.h>
#include "firmware.h"
#include "seastar.h"


#define SEASTAR_VERSION_STR "1.0"


MODULE_DESCRIPTION("Cray SeaStar Native IP driver");
MODULE_AUTHOR("Maintainer: Kevin Pedretti <ktpedre@sandia.gov>");
MODULE_VERSION(SEASTAR_VERSION_STR);
MODULE_LICENSE("GPL");


static struct pending *alloc_tx_pending(struct ss_priv *ssp)
{
	struct pending *pending = ssp->tx_pending_free_list;
	if (!pending)
		return NULL;

	ssp->tx_pending_free_list = pending->next;
	pending->next = 0;

	return pending;
}


static void free_tx_pending(struct ss_priv *ssp, struct pending *pending)
{
	pending->next             = ssp->tx_pending_free_list;
	ssp->tx_pending_free_list = pending;
}


static uint16_t pending_to_index(struct ss_priv *ssp, struct pending *pending)
{
	return pending - ssp->pending_table;
}


static struct pending *index_to_pending(struct ss_priv *ssp, unsigned int index)
{
	return &ssp->pending_table[index];
}


static void refill_skb(struct net_device *netdev, int i)
{
	struct ss_priv *ssp = netdev_priv(netdev);
	struct sk_buff *skb;

	skb = dev_alloc_skb(netdev->mtu + SKB_PAD);
	if (!skb) {
		dev_err(&ssp->pdev->dev, "dev_alloc_skb() failed.\n");
		return;
	}

	skb->dev = netdev;
	skb_reserve(skb, SKB_PAD);

	/* Push it down to the PPC as a quadbyte address */
	ssp->skb_table_phys[i] = virt_to_phys(skb->data) >> 2;
	ssp->skb_table_virt[i] = skb;
}


static int ss_open(struct net_device *netdev)
{
	struct ss_priv *ssp = netdev_priv(netdev);
	int i;

	netif_start_queue(netdev);

	for (i = 0; i < NUM_SKBS; i++) {
		ssp->skb_table_phys[i] = 0;
		ssp->skb_table_virt[i] = 0;
		refill_skb(netdev, i);
	}

	return 0;
}


static int eth2ss(struct ss_priv *ssp, struct sk_buff *skb)
{
	struct ethhdr *ethhdr;
	struct sshdr *sshdr;
	uint8_t source_lo_mac, dest_lo_mac;
	uint32_t qb_len;

	/* Read the "low" bytes of the source and destination MAC addresses */
	ethhdr = (struct ethhdr *)skb->data;
	source_lo_mac = ethhdr->h_source[5];
	dest_lo_mac   = ethhdr->h_dest[5];

	/* Drop anything not IPv4 */
	if (ethhdr->h_proto != ntohs(ETH_P_IP)) {
		dev_err(&ssp->pdev->dev, "squashing non-IPv4 packet.");
		return -1;
	}

	/* Squash broadcast packets, SeaStar doesn't support broadcast */
	if (dest_lo_mac == 0xFF) {
		dev_err(&ssp->pdev->dev, "squashing broadcast packet.");
		return -1;
	}

	/* We only support 4 bits of virtual hosts per physical node */
	if ((source_lo_mac & ~0xF) || (dest_lo_mac & ~0xF)) {
		dev_err(&ssp->pdev->dev, "lo_mac out of range.");
		return -1;
	}

	/* Move ahead to allow sshdr to be filled in overtop of the ethhdr */
	sshdr = (struct sshdr *)
		skb_pull(skb, (unsigned int)(ETH_HLEN - sizeof(struct sshdr)));

	/* The length in quad bytes, rounded up to the nearest quad byte.
	 * SS header is already counted in skb->len as per skb_pull() above */
	qb_len = (ROUNDUP4(skb->len) >> 2) - 1;

	/* Build the SeaStar header */
	sshdr->length   = qb_len;
	sshdr->lo_macs  = (source_lo_mac << 4) | dest_lo_mac;
	sshdr->hdr_type = (2 << 5); /* Datagram 2, type 0 == IP */

	return 0;
}


static int ss2eth(struct sk_buff *skb)
{
	struct sshdr *sshdr;
	struct ethhdr *ethhdr;
	uint8_t source_lo_mac, dest_lo_mac;

	/* Read the "low" bytes of the source and destination MAC addresses */
	sshdr = (struct sshdr *)skb->data;
	source_lo_mac = (sshdr->lo_macs >> 4);
	dest_lo_mac    = sshdr->lo_macs & 0xF;

	/* Make room for the rest of the ethernet header and zero it */
	ethhdr = (struct ethhdr *)
	     skb_push(skb, (unsigned int)(ETH_HLEN - sizeof(struct sshdr)));
	memset(ethhdr, 0x00, ETH_HLEN);

	/* h_proto and h_dest[] are available.  Just 0xff h_source[2-5] */
	ethhdr->h_proto = htons(ETH_P_IP);

	/* We're assuming the source MAC is the same as the local
	 * host's MAC in order to support loopback in promiscous mode */
	memcpy(&ethhdr->h_source, &skb->dev->dev_addr, ETH_ALEN);
	memcpy(&ethhdr->h_dest, &skb->dev->dev_addr, ETH_ALEN);
	ethhdr->h_source[5] = source_lo_mac;
	ethhdr->h_dest[5]   = dest_lo_mac;

	return 0;
}


static int ss_tx(struct sk_buff *skb, struct net_device *netdev)
{
	unsigned long flags;
	struct ss_priv *ssp = netdev_priv(netdev);
	struct ethhdr *eh = (struct ethhdr *)skb->data;
	struct sshdr *sshdr;
	uint32_t dest_nid = ntohl(*(uint32_t *)eh->h_dest);
	struct pending *pending = NULL;
	void *msg;

	spin_lock_irqsave(&ssp->lock, flags);

	if (netif_queue_stopped(netdev)) {
		spin_unlock_irqrestore(&ssp->lock, flags);
		return NETDEV_TX_BUSY;
	}

	/* Convert the SKB from an ethernet frame to a seastar frame */
	if (eth2ss(ssp, skb)) {
		netdev->stats.tx_errors++;
		goto drop;
	}

	sshdr = (struct sshdr *)skb->data;

	/* Get a tx_pending so that we can track the completion of this SKB */
	pending = alloc_tx_pending(ssp);
	if (!pending) {
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&ssp->lock, flags);
		return NETDEV_TX_BUSY;
	}

	/* Stash skb away in the pending, will be needed in ss_tx_end() */
	pending->skb = skb;

	/* Make sure buffer we pass to SeaStar is quad-byte aligned */
	if (((unsigned long)skb->data & 0x3) == 0) {
		pending->bounce = NULL;
		msg = skb->data;
	} else {
		/* Need to use bounce buffer to get quad-byte alignment */
		pending->bounce = kmalloc(skb->len, GFP_KERNEL);
		if (!pending->bounce) {
			dev_err(&ssp->pdev->dev, "dev_alloc_skb() failed.\n");
			goto drop;
		}
		memcpy(pending->bounce, skb->data, skb->len);
		msg = pending->bounce;
	}

	seastar_ip_tx_cmd(
		ssp,
		dest_nid,
		sshdr->length,
		virt_to_phys(msg) >> 2,
		pending_to_index(ssp, pending)
	);

	netdev->stats.tx_packets++;
	netdev->stats.tx_bytes += skb->len;

	spin_unlock_irqrestore(&ssp->lock, flags);
	return 0;

drop:
	dev_kfree_skb_any(skb);
	if (pending)
		free_tx_pending(ssp, pending);
	spin_unlock_irqrestore(&ssp->lock, flags);
	return 0;
}


static void ss_tx_end(struct net_device *netdev, unsigned int pending_index)
{
	unsigned long flags;
	struct ss_priv *ssp = netdev_priv(netdev);
	struct pending *pending = index_to_pending(ssp, pending_index);

	spin_lock_irqsave(&ssp->lock, flags);

	if (pending->skb)
		dev_kfree_skb_any(pending->skb);

	kfree(pending->bounce);

	free_tx_pending(ssp, pending);

	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);

	spin_unlock_irqrestore(&ssp->lock, flags);
}


static void ss_rx_skb(struct net_device *netdev, struct sk_buff *skb)
{
	struct sshdr *sshdr = (struct sshdr *)skb_tail_pointer(skb);

	const uint32_t qb_len = sshdr->length;
	const uint32_t len    = (qb_len + 1) << 2;

	skb_put(skb, len);
	ss2eth(skb);

	skb->protocol  = htons(ETH_P_IP);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_set_mac_header(skb, 0);

	/* Skip past the ethernet header we just built */
	skb_pull(skb, ETH_HLEN);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += len;

	netif_rx(skb);
}


static void ss_rx(struct net_device *netdev, unsigned int skb_index)
{
	struct ss_priv *ssp = netdev_priv(netdev);
	struct sk_buff *skb = ssp->skb_table_virt[skb_index];

	ssp->skb_table_virt[skb_index] = 0;
	ss_rx_skb(netdev, skb);

	refill_skb(netdev, skb_index);
}


static int ss_header_create(struct sk_buff *skb, struct net_device *netdev,
			    unsigned short type, const void *daddr,
			    const void *saddr, unsigned int length)
{
	struct ethhdr *eh;

	/* Make room for the ethernet header and zero it */
	eh = (struct ethhdr *)skb_push(skb, ETH_HLEN);
	memset(eh, 0, ETH_HLEN);

	/* Although we can only do IPv4, build other packets correctly for
	 * now and drop it in the ndo_start_xmit hook.  This way the fact that
	 * these packets are being generated is not invisible. */
	eh->h_proto = htons(type);

	/* Set the source hardware address */
	if (!saddr)
		saddr = netdev->dev_addr;
	memcpy(eh->h_source, saddr, ETH_ALEN);

	/* Set the destination hardware address */
	if (daddr) {
		memcpy(eh->h_dest, daddr, ETH_ALEN);
		return ETH_HLEN;
	}

	/* No destination address supplied !?! */
	return -ETH_HLEN;
}


static uint32_t next_event(struct ss_priv *ssp)
{
	uint32_t ev = ssp->eq[ssp->eq_read];
	if (!ev)
		return 0;

	ssp->eq[ssp->eq_read] = 0;
	ssp->eq_read = (ssp->eq_read + 1) % NUM_EQ_ENTRIES;

	return ev;
}


static void ss_rx_refill(struct net_device *netdev)
{
	struct ss_priv *ssp = netdev_priv(netdev);
	int i;

	for (i = 0; i < NUM_SKBS; i++) {
		if (ssp->skb_table_virt[i] == 0)
			refill_skb(netdev, i);
	}
}


static irqreturn_t ss_interrupt(int irq, void *dev)
{
	struct net_device *netdev = (struct net_device *)dev;
	struct ss_priv *ssp = netdev_priv(netdev);
	uint32_t ev;
	unsigned int type, index;

	while (1) {
		ev = next_event(ssp);
		if (!ev)
			break;

		type  = (ev >> 16) & 0xFFFF;
		index = (ev >>  0) & 0xFFFF;

		switch (type) {

		case EVENT_TX_END:
			ss_tx_end(netdev, index);
			break;

		case EVENT_RX:
			ss_rx(netdev, index);
			break;

		case EVENT_RX_EMPTY:
			ss_rx_refill(netdev);
			break;

		default:
			dev_err(&ssp->pdev->dev,
				"unknown event type (type=%u, index=%u).\n",
				type, index);
		}
	}

	return IRQ_HANDLED;
}


static const struct net_device_ops ss_netdev_ops = {
	.ndo_open		= ss_open,
	.ndo_start_xmit		= ss_tx,
	.ndo_set_mac_address	= eth_mac_addr,
};


static const struct header_ops ss_header_ops = {
	.create			= ss_header_create,
};


static void ss_ht_irq_update(struct pci_dev *dev, int irq,
			     struct ht_irq_msg *msg)
{
	seastar_setup_htb_bi(msg->address_lo);
}


static int __devinit ss_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct net_device *netdev;
	struct ss_priv *ssp;
	int i, irq, err = 0;

	err = pci_enable_device(pdev);
	if (err != 0) {
		dev_err(&pdev->dev, "Could not enable PCI device.\n");
		return -ENODEV;
	}

	netdev = alloc_etherdev(sizeof(*ssp));
	if (netdev == NULL) {
		dev_err(&pdev->dev, "Could not allocate ethernet device.\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	strcpy(netdev->name, "ss");
	netdev->netdev_ops	= &ss_netdev_ops;
	netdev->header_ops	= &ss_header_ops;
	netdev->mtu		= 16000;
	netdev->flags		= IFF_NOARP;

	/* Setup private state */
	ssp = netdev_priv(netdev);
	memset(ssp, 0, sizeof(*ssp));

	spin_lock_init(&ssp->lock);
	ssp->skb_table_phys	= seastar_skb;
	ssp->eq_read		= 0;
	ssp->pdev		= pdev;

	/* Build the TX pending free list */
	ssp->tx_pending_free_list = 0;
	for (i = 0; i < NUM_TX_PENDINGS; i++)
		free_tx_pending(ssp, index_to_pending(ssp, i));

	irq = __ht_create_irq(pdev, 0, ss_ht_irq_update);
	if (irq < 0) {
		dev_err(&pdev->dev, "__ht_create_irq() failed, err=%d.\n", err);
		goto err_out;
	}

	err = request_irq(irq, ss_interrupt, IRQF_NOBALANCING,
			  "seastar", netdev);
	if (err != 0) {
		dev_err(&pdev->dev, "request_irq() failed, err=%d.\n", err);
		goto err_out;
	}

	err = seastar_hw_init(netdev_priv(netdev));
	if (err != 0) {
		dev_err(&pdev->dev, "seastar_hw_init() failed, err=%d.\n", err);
		goto err_out;
	}

	err = register_netdev(netdev);
	if (err != 0) {
		dev_err(&pdev->dev, "register_netdev() failed, err=%d.\n", err);
		goto err_out;
	}

	return 0;

err_out:
	free_netdev(netdev);
	return err;
}


static void __devexit ss_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);

	unregister_netdev(netdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}


#define PCI_VENDOR_ID_CRAY		0x17DB
#define PCI_DEVICE_ID_SEASTAR		0x0101


static struct pci_device_id ss_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_CRAY, PCI_DEVICE_ID_SEASTAR)},
	{0},
};


static struct pci_driver ss_driver = {
	.name = "seastar",
	.probe = ss_probe,
	.remove = __devexit_p(ss_remove),
	.id_table = ss_pci_tbl,
};


static __init int ss_init_module(void)
{
	printk(KERN_INFO "%s: module loaded (version %s)\n",
	       ss_driver.name, SEASTAR_VERSION_STR);

	return pci_register_driver(&ss_driver);
}


static __exit void ss_cleanup_module(void)
{
	pci_unregister_driver(&ss_driver);
}


module_init(ss_init_module);
module_exit(ss_cleanup_module);
