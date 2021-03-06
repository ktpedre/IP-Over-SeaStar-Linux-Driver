/*
    comedi/drivers/ni_daq_dio24.c
    Driver for National Instruments PCMCIA DAQ-Card DIO-24
    Copyright (C) 2002 Daniel Vecino Castel <dvecino@able.es>

    PCMCIA crap at end of file is adapted from dummy_cs.c 1.31 2001/08/24 12:13:13
    from the pcmcia package.
    The initial developer of the pcmcia dummy_cs.c code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

************************************************************************
*/
/*
Driver: ni_daq_dio24
Description: National Instruments PCMCIA DAQ-Card DIO-24
Author: Daniel Vecino Castel <dvecino@able.es>
Devices: [National Instruments] PCMCIA DAQ-Card DIO-24 (ni_daq_dio24)
Status: ?
Updated: Thu, 07 Nov 2002 21:53:06 -0800

This is just a wrapper around the 8255.o driver to properly handle
the PCMCIA interface.
*/

			    /* #define LABPC_DEBUG *//*  enable debugging messages */
#undef LABPC_DEBUG

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#include "8255.h"

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev = NULL;

#define DIO24_SIZE 4		/*  size of io region used by board */

static int dio24_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int dio24_detach(struct comedi_device *dev);

enum dio24_bustype { pcmcia_bustype };

struct dio24_board_struct {
	const char *name;
	int device_id;		/*  device id for pcmcia board */
	enum dio24_bustype bustype;	/*  PCMCIA */
	int have_dio;		/*  have 8255 chip */
	/*  function pointers so we can use inb/outb or readb/writeb as appropriate */
	unsigned int (*read_byte) (unsigned int address);
	void (*write_byte) (unsigned int byte, unsigned int address);
};

static const struct dio24_board_struct dio24_boards[] = {
	{
	 .name = "daqcard-dio24",
	 .device_id = 0x475c,	/*  0x10b is manufacturer id, 0x475c is device id */
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
	{
	 .name = "ni_daq_dio24",
	 .device_id = 0x475c,	/*  0x10b is manufacturer id, 0x475c is device id */
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct dio24_board_struct *)dev->board_ptr)

struct dio24_private {

	int data;		/* number of data points left to be taken */
};

#define devpriv ((struct dio24_private *)dev->private)

static struct comedi_driver driver_dio24 = {
	.driver_name = "ni_daq_dio24",
	.module = THIS_MODULE,
	.attach = dio24_attach,
	.detach = dio24_detach,
	.num_names = ARRAY_SIZE(dio24_boards),
	.board_name = &dio24_boards[0].name,
	.offset = sizeof(struct dio24_board_struct),
};

static int dio24_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = 0;
#ifdef incomplete
	unsigned int irq = 0;
#endif
	struct pcmcia_device *link;

	/* allocate and initialize dev->private */
	if (alloc_private(dev, sizeof(struct dio24_private)) < 0)
		return -ENOMEM;

	/*  get base address, irq etc. based on bustype */
	switch (thisboard->bustype) {
	case pcmcia_bustype:
		link = pcmcia_cur_dev;	/* XXX hack */
		if (!link)
			return -EIO;
		iobase = link->io.BasePort1;
#ifdef incomplete
		irq = link->irq.AssignedIRQ;
#endif
		break;
	default:
		printk("bug! couldn't determine board type\n");
		return -EINVAL;
		break;
	}
	printk("comedi%d: ni_daq_dio24: %s, io 0x%lx", dev->minor,
	       thisboard->name, iobase);
#ifdef incomplete
	if (irq) {
		printk(", irq %u", irq);
	}
#endif

	printk("\n");

	if (iobase == 0) {
		printk("io base address is zero!\n");
		return -EINVAL;
	}

	dev->iobase = iobase;

#ifdef incomplete
	/* grab our IRQ */
	dev->irq = irq;
#endif

	dev->board_name = thisboard->name;

	if (alloc_subdevices(dev, 1) < 0)
		return -ENOMEM;

	/* 8255 dio */
	s = dev->subdevices + 0;
	subdev_8255_init(dev, s, NULL, dev->iobase);

	return 0;
};

static int dio24_detach(struct comedi_device *dev)
{
	printk("comedi%d: ni_daq_dio24: remove\n", dev->minor);

	if (dev->subdevices)
		subdev_8255_cleanup(dev, dev->subdevices + 0);

	if (thisboard->bustype != pcmcia_bustype && dev->iobase)
		release_region(dev->iobase, DIO24_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);

	return 0;
};

/* PCMCIA crap */

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/
#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0644);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version = "ni_daq_dio24.c, based on dummy_cs.c";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

static void dio24_config(struct pcmcia_device *link);
static void dio24_release(struct pcmcia_device *link);
static int dio24_cs_suspend(struct pcmcia_device *p_dev);
static int dio24_cs_resume(struct pcmcia_device *p_dev);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static int dio24_cs_attach(struct pcmcia_device *);
static void dio24_cs_detach(struct pcmcia_device *);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'memory_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/

static const dev_info_t dev_info = "ni_daq_dio24";

struct local_info_t {
	struct pcmcia_device *link;
	dev_node_t node;
	int stop;
	struct bus_operations *bus;
};

/*======================================================================

    dio24_cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int dio24_cs_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;

	printk(KERN_INFO "ni_daq_dio24: HOLA SOY YO - CS-attach!\n");

	DEBUG(0, "dio24_cs_attach()\n");

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(struct local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->link = link;
	link->priv = local;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->irq.Handler = NULL;

	/*
	   General socket configuration defaults can go here.  In this
	   client, we assume very little, and rely on the CIS for almost
	   everything.  In most clients, many details (i.e., number, sizes,
	   and attributes of IO windows) are fixed by the nature of the
	   device, and can be hard-wired here.
	 */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	pcmcia_cur_dev = link;

	dio24_config(link);

	return 0;
}				/* dio24_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void dio24_cs_detach(struct pcmcia_device *link)
{

	printk(KERN_INFO "ni_daq_dio24: HOLA SOY YO - cs-detach!\n");

	DEBUG(0, "dio24_cs_detach(0x%p)\n", link);

	if (link->dev_node) {
		((struct local_info_t *)link->priv)->stop = 1;
		dio24_release(link);
	}

	/* This points to the parent local_info_t struct */
	if (link->priv)
		kfree(link->priv);

}				/* dio24_cs_detach */

/*======================================================================

    dio24_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static void dio24_config(struct pcmcia_device *link)
{
	struct local_info_t *dev = link->priv;
	tuple_t tuple;
	cisparse_t parse;
	int last_ret;
	u_char buf[64];
	win_req_t req;
	memreq_t map;
	cistpl_cftable_entry_t dflt = { 0 };

	printk(KERN_INFO "ni_daq_dio24: HOLA SOY YO! - config\n");

	DEBUG(0, "dio24_config(0x%p)\n", link);

	/*
	   This reads the card's CONFIG tuple to find its configuration
	   registers.
	 */
	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;

	last_ret = pcmcia_get_first_tuple(link, &tuple);
	if (last_ret) {
		cs_error(link, GetFirstTuple, last_ret);
		goto cs_failed;
	}

	last_ret = pcmcia_get_tuple_data(link, &tuple);
	if (last_ret) {
		cs_error(link, GetTupleData, last_ret);
		goto cs_failed;
	}

	last_ret = pcmcia_parse_tuple(&tuple, &parse);
	if (last_ret) {
		cs_error(link, ParseTuple, last_ret);
		goto cs_failed;
	}
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/*
	   In this loop, we scan the CIS for configuration table entries,
	   each of which describes a valid card configuration, including
	   voltage, IO window, memory window, and interrupt settings.

	   We make no assumptions about the card to be configured: we use
	   just the information available in the CIS.  In an ideal world,
	   this would work for any PCMCIA card, but it requires a complete
	   and accurate CIS.  In practice, a driver usually "knows" most of
	   these things without consulting the CIS, and most client drivers
	   will only use the CIS to fill in implementation-defined details.
	 */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;

	last_ret = pcmcia_get_first_tuple(link, &tuple);
	if (last_ret) {
		cs_error(link, GetFirstTuple, last_ret);
		goto cs_failed;
	}
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		if (pcmcia_get_tuple_data(link, &tuple) != 0)
			goto next_entry;
		if (pcmcia_parse_tuple(&tuple, &parse) != 0)
			goto next_entry;

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Does this card need audio output? */
		if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
			link->conf.Attributes |= CONF_ENABLE_SPKR;
			link->conf.Status = CCSR_AUDIO_ENA;
		}

		/* Do we need to allocate an interrupt? */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
			/* This reserves IO space but doesn't actually enable it */
			if (pcmcia_request_io(link, &link->io) != 0)
				goto next_entry;
		}

		if ((cfg->mem.nwin > 0) || (dflt.mem.nwin > 0)) {
			cistpl_mem_t *mem =
			    (cfg->mem.nwin) ? &cfg->mem : &dflt.mem;
			req.Attributes = WIN_DATA_WIDTH_16 | WIN_MEMORY_TYPE_CM;
			req.Attributes |= WIN_ENABLE;
			req.Base = mem->win[0].host_addr;
			req.Size = mem->win[0].len;
			if (req.Size < 0x1000)
				req.Size = 0x1000;
			req.AccessSpeed = 0;
			if (pcmcia_request_window(&link, &req, &link->win))
				goto next_entry;
			map.Page = 0;
			map.CardOffset = mem->win[0].card_addr;
			if (pcmcia_map_mem_page(link->win, &map))
				goto next_entry;
		}
		/* If we got this far, we're cool! */
		break;

next_entry:

		last_ret = pcmcia_get_next_tuple(link, &tuple);
		if (last_ret) {
			cs_error(link, GetNextTuple, last_ret);
			goto cs_failed;
		}
	}

	/*
	   Allocate an interrupt line.  Note that this does not assign a
	   handler to the interrupt, unless the 'Handler' member of the
	   irq structure is initialized.
	 */
	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		last_ret = pcmcia_request_irq(link, &link->irq);
		if (last_ret) {
			cs_error(link, RequestIRQ, last_ret);
			goto cs_failed;
		}
	}

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	last_ret = pcmcia_request_configuration(link, &link->conf);
	if (last_ret) {
		cs_error(link, RequestConfiguration, last_ret);
		goto cs_failed;
	}

	/*
	   At this point, the dev_node_t structure(s) need to be
	   initialized and arranged in a linked list at link->dev.
	 */
	sprintf(dev->node.dev_name, "ni_daq_dio24");
	dev->node.major = dev->node.minor = 0;
	link->dev_node = &dev->node;

	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x",
	       dev->node.dev_name, link->conf.ConfigIndex);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1 + link->io.NumPorts1 - 1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2 + link->io.NumPorts2 - 1);
	if (link->win)
		printk(", mem 0x%06lx-0x%06lx", req.Base,
		       req.Base + req.Size - 1);
	printk("\n");

	return;

cs_failed:
	printk(KERN_INFO "Fallo");
	dio24_release(link);

}				/* dio24_config */

static void dio24_release(struct pcmcia_device *link)
{
	DEBUG(0, "dio24_release(0x%p)\n", link);

	pcmcia_disable_device(link);
}				/* dio24_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int dio24_cs_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;
	return 0;
}				/* dio24_cs_suspend */

static int dio24_cs_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;
	return 0;
}				/* dio24_cs_resume */

/*====================================================================*/

static struct pcmcia_device_id dio24_cs_ids[] = {
	/* N.B. These IDs should match those in dio24_boards */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x475c),	/* daqcard-dio24 */
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, dio24_cs_ids);

struct pcmcia_driver dio24_cs_driver = {
	.probe = dio24_cs_attach,
	.remove = dio24_cs_detach,
	.suspend = dio24_cs_suspend,
	.resume = dio24_cs_resume,
	.id_table = dio24_cs_ids,
	.owner = THIS_MODULE,
	.drv = {
		.name = dev_info,
		},
};

static int __init init_dio24_cs(void)
{
	printk("ni_daq_dio24: HOLA SOY YO!\n");
	DEBUG(0, "%s\n", version);
	pcmcia_register_driver(&dio24_cs_driver);
	return 0;
}

static void __exit exit_dio24_cs(void)
{
	DEBUG(0, "ni_dio24: unloading\n");
	pcmcia_unregister_driver(&dio24_cs_driver);
}

int __init init_module(void)
{
	int ret;

	ret = init_dio24_cs();
	if (ret < 0)
		return ret;

	return comedi_driver_register(&driver_dio24);
}

void __exit cleanup_module(void)
{
	exit_dio24_cs();
	comedi_driver_unregister(&driver_dio24);
}
