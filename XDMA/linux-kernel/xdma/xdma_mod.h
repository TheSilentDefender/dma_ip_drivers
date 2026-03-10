/*
 * This file is part of the Xilinx DMA IP Core driver for Linux
 *
 * Copyright (c) 2016-present,  Xilinx, Inc.
 * All rights reserved.
 *
 * This source code is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 */

#ifndef __XDMA_MODULE_H__
#define __XDMA_MODULE_H__

#include <linux/types.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/aio.h>
#include <linux/splice.h>
#include <linux/version.h>
#include <linux/uio.h>
#include <linux/spinlock_types.h>
/* FIX: added for kref and completion */
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/atomic.h>

#include "libxdma.h"
#include "xdma_thread.h"

#define MAGIC_ENGINE	0xEEEEEEEEUL
#define MAGIC_DEVICE	0xDDDDDDDDUL
#define MAGIC_CHAR	0xCCCCCCCCUL
#define MAGIC_BITSTREAM 0xBBBBBBBBUL

extern unsigned int desc_blen_max;
extern unsigned int h2c_timeout;
extern unsigned int c2h_timeout;

struct xdma_cdev {
	unsigned long magic;		/* structure ID for sanity checks */
	struct xdma_pci_dev *xpdev;
	struct xdma_dev *xdev;
	dev_t cdevno;			/* character device major:minor */
	struct cdev cdev;		/* character device embedded struct */
	int bar;			/* PCIe BAR for HW access, if needed */
	unsigned long base;		/* bar access offset */
	struct xdma_engine *engine;	/* engine instance, if needed */
	struct xdma_user_irq *user_irq;	/* IRQ value, if needed */
	struct device *sys_device;	/* sysfs device */
	spinlock_t lock;

	/*
	 * FIX: hot-disconnect safety.
	 *
	 * 'online' is cleared (set to 0) by destroy_xcdev() before any
	 * teardown begins.  char_open() and every ioctl/read/write handler
	 * must test this flag under xcdev->lock before touching hw pointers.
	 *
	 * 'open_count' tracks the number of file descriptors currently open
	 * against this cdev.  destroy_xcdev() waits on 'close_wait' until
	 * open_count reaches zero so that no caller can be inside a fops
	 * handler when we pull the rug out.
	 */
	atomic_t		online;
	atomic_t		open_count;
	wait_queue_head_t	close_wait;
};

/* XDMA PCIe device specific book-keeping */
struct xdma_pci_dev {
	unsigned long magic;		/* structure ID for sanity checks */
	struct pci_dev *pdev;	/* pci device struct from probe() */
	struct xdma_dev *xdev;
	int major;		/* major number */
	int instance;		/* instance number */
	int user_max;
	int c2h_channel_max;
	int h2c_channel_max;

	unsigned int flags;
	/* character device structures */
	struct xdma_cdev ctrl_cdev;
	struct xdma_cdev sgdma_c2h_cdev[XDMA_CHANNEL_NUM_MAX];
	struct xdma_cdev sgdma_h2c_cdev[XDMA_CHANNEL_NUM_MAX];
	struct xdma_cdev events_cdev[16];

	struct xdma_cdev user_cdev;
	struct xdma_cdev bypass_c2h_cdev[XDMA_CHANNEL_NUM_MAX];
	struct xdma_cdev bypass_h2c_cdev[XDMA_CHANNEL_NUM_MAX];
	struct xdma_cdev bypass_cdev_base;

	struct xdma_cdev xvc_cdev;

	void *data;

	/*
	 * FIX: kref-based deferred free for hot-disconnect safety.
	 *
	 * The problem: remove_one() -> xpdev_free() calls xdma_device_close()
	 * which frees the underlying xdev/engine structs.  If any file
	 * descriptor is still open at that point, the next fops call
	 * dereferences freed memory -> NULL pointer dereference / panic.
	 *
	 * Solution: xpdev holds a kref.  remove_one() marks every cdev
	 * offline and drops its initial ref.  Each open() takes a ref;
	 * each close() drops it.  The actual xdma_device_close() + kfree
	 * only runs when the last ref is dropped, regardless of ordering.
	 *
	 * 'ref_completion' lets the module unload path (xdma_mod_exit) wait
	 * for the final free if needed, avoiding use-after-free on module
	 * unload racing with open file descriptors.
	 */
	struct kref		refcount;
	struct completion	ref_completion;
};

struct cdev_async_io {
	struct kiocb *iocb;
	struct xdma_io_cb *cb;
	bool write;
	bool cancel;
	int cmpl_cnt;
	int req_cnt;
	spinlock_t lock;
	struct work_struct wrk_itm;
	struct cdev_async_io *next;
	ssize_t res;
	ssize_t res2;
	int err_cnt;
};

#endif /* ifndef __XDMA_MODULE_H__ */