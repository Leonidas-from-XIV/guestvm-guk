/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
/*
 * Author: Harald Roeck
 */
#ifndef _BLK_FRONT_H_
#define _BLK_FRONT_H_

#include <xen/io/blkif.h>

/* these are default values: a sector is 512 bytes */
#define SECTOR_BITS      9
#define SECTOR_SIZE      (1<<SECTOR_BITS)
#define SECTORS_PER_PAGE (PAGE_SIZE/SECTOR_SIZE)
#define addr_to_sec(addr)  (addr/SECTOR_SIZE)
#define sec_to_addr(sec)   (sec * SECTOR_SIZE)


struct blk_request;
typedef void (*blk_callback)(struct blk_request*);
#define MAX_PAGES_PER_REQUEST  BLKIF_MAX_SEGMENTS_PER_REQUEST /* 11 */
/*
 * I/O request; a request consists of multiple buffers and belongs to a thread;
 * when the request is completed a callback is invoked
 */
struct blk_request {
    void *pages[MAX_PAGES_PER_REQUEST]; /* virtual addresses of the pages to read/write */
    int num_pages; /* number of valid pages */
    int start_sector; /* number of the first sector in the first page to use; start at 0 */
    int end_sector; /* number of the last sector in the last page to use; start at 0 */

    int device; /* device id; for now, we support only device 0 */
    long address; /* address on the device to write to/read from; must be a sector boundary */

    enum {
	BLK_EMPTY = 1,
	BLK_SUBMITTED,
	BLK_DONE_SUCCESS,
	BLK_DONE_ERROR,
    } state;

    enum { /* operation on the device */
	BLK_REQ_READ = BLKIF_OP_READ,
	BLK_REQ_WRITE = BLKIF_OP_WRITE,
    } operation;

    /* notification callback 
     * invoked by the interrupt handler when the request is done */
    blk_callback callback;
    /* argument to the callback */  
    unsigned long callback_data;
};

/*
 * return the number of devices
 */
extern int guk_blk_get_devices(void);

/*
 * return number of sectors on a device
 */
extern int guk_blk_get_sectors(int device);

/*
 * submit request to Xen; note this is async the caller has take care
 * of registering a callback and wait for the completion of the request
 */
extern int guk_blk_do_io(struct blk_request *req);

/*
 * more userfriendly I/O calls that block the caller thread until
 * the request was successfully processed
 */ 
extern int guk_blk_write(int device, long address, void *buf, int size);
extern int guk_blk_read(int device, long address, void *buf, int size);

#define blk_write guk_blk_write
#define blk_read guk_blk_read
#define blk_do_io guk_blk_do_io
#define blk_get_devices guk_blk_get_devices
#define blk_get_sectors guk_blk_get_sectors

#endif /* _BLK_FRONT_H_ */
