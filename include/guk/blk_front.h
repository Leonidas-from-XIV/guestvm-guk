/*
 * Copyright (c) 2009 Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, California 95054, U.S.A. All rights reserved.
 * 
 * U.S. Government Rights - Commercial software. Government users are
 * subject to the Sun Microsystems, Inc. standard license agreement and
 * applicable provisions of the FAR and its supplements.
 * 
 * Use is subject to license terms.
 * 
 * This distribution may include materials developed by third parties.
 * 
 * Parts of the product may be derived from Berkeley BSD systems,
 * licensed from the University of California. UNIX is a registered
 * trademark in the U.S.  and in other countries, exclusively licensed
 * through X/Open Company, Ltd.
 * 
 * Sun, Sun Microsystems, the Sun logo and Java are trademarks or
 * registered trademarks of Sun Microsystems, Inc. in the U.S. and other
 * countries.
 * 
 * This product is covered and controlled by U.S. Export Control laws and
 * may be subject to the export or import laws in other
 * countries. Nuclear, missile, chemical biological weapons or nuclear
 * maritime end uses or end users, whether direct or indirect, are
 * strictly prohibited. Export or reexport to countries subject to
 * U.S. embargo or to entities identified on U.S. export exclusion lists,
 * including, but not limited to, the denied persons and specially
 * designated nationals lists is strictly prohibited.
 * 
 */
/*
 * Author: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
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
