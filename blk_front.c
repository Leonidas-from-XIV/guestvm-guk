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
 * Block device front end
 *
 * Author: Harald Roeck, Sun Microsystems, Inc. intern summer 2008.
 *
 * TODO: support multiple devices
 */

#include <guk/os.h>
#include <guk/service.h>
#include <guk/init.h>
#include <guk/xenbus.h>
#include <guk/events.h>
#include <guk/gnttab.h>
#include <guk/time.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/blk_front.h>
#include <guk/completion.h>
#include <guk/xmalloc.h>
#include <guk/spinlock.h>
#include <xen/io/blkif.h>
#include <xen/io/xenbus.h>

#include <errno.h>

#define MAX_PATH      64
#define MAX_DEVICES    1

#define DEVICE_STRING "device/vbd"

#define DEBUG(...) do {\
    if (trace_blk()) \
        tprintk(__VA_ARGS__);\
}while(0)

/*
 * device information
 */
struct device_info {
    int sector_size;
    int sectors;
    int info; /* XEN flags: CD-ROM 1; removable 2, read only 4 */
    int id; /* XEN id */
};

/*
 * front end data
 */
struct blk_dev {
	int ring_ref;
	int16_t blk_id;
	int16_t state;
	evtchn_port_t evtchn;
	unsigned int local_port;
	char *backend;
	struct blkif_front_ring ring;
	struct device_info device;
	spinlock_t lock;
};
#define ST_UNKNOWN      0
#define ST_READY        1
#define ST_SUSPENDING   2
#define ST_SUSPENDED    3
#define ST_RESUMING     4

static struct blk_dev blk_devices[MAX_DEVICES];

#define BLK_RING_SIZE  __RING_SIZE((struct blkif_sring *)0, PAGE_SIZE)

/*
 *  buffer to read and write from the device
 */
struct blk_shadow {
    grant_ref_t gref[MAX_PAGES_PER_REQUEST];
    short num_refs;
    short free;
    struct blk_request *request;
};

static DEFINE_SPINLOCK(freelist_lock);
static unsigned short freelist[BLK_RING_SIZE];
static struct blk_shadow shadows[BLK_RING_SIZE];

static DECLARE_COMPLETION(ready_completion);

static inline unsigned int get_freelist_id(void)
{
    unsigned int id;
    int flags;

    spin_lock_irqsave(&freelist_lock, flags);
    id = freelist[0];
    freelist[0] = freelist[id];
    shadows[id].free = 0;
    spin_unlock_irqrestore(&freelist_lock, flags);
    return id;
}

static inline void add_id_freelist(unsigned int id)
{
    int flags;
    spin_lock_irqsave(&freelist_lock, flags);
    freelist[id] = freelist[0];
    freelist[0] = id;
    shadows[id].free = 1;
    spin_unlock_irqrestore(&freelist_lock, flags);
}

static void init_buffers(void)
{
    int i;
    for(i=0; i<BLK_RING_SIZE; ++i) {
	add_id_freelist(i);
    }
}
/* end of shadow buffer handling */

static inline void complete_request(struct blk_shadow *shadow)
{
    int i;
    for(i = 0; i < shadow->num_refs; ++i) {
	gnttab_end_access(shadow->gref[i]); /* finish this */
    }
}


static void blk_front_handler(evtchn_port_t port, struct blk_dev *dev)
{
    struct blk_request *io_req;
    struct blk_shadow *shadow;
    struct blkif_response *response;
    RING_IDX cons, prod;
again:
    /* pull all responses from the ring */
    prod = dev->ring.sring->rsp_prod;
    rmb(); /* read barrier */

    for(cons = dev->ring.rsp_cons; cons != prod; cons++) {
	response = RING_GET_RESPONSE(&dev->ring, cons);

	/* find the request to this response and remove grant table entries */
	shadow = &shadows[response->id];

	BUG_ON(shadow->free); /* is this a bug or a spurious interrupt */

	complete_request(shadow);

	io_req = shadow->request;
	BUG_ON(io_req->state != BLK_SUBMITTED); /* is this a bug or a spurious interrupt */

	add_id_freelist(response->id);
	io_req->state = response->status == BLKIF_RSP_OKAY?BLK_DONE_SUCCESS:BLK_DONE_ERROR;

	/* invoke application callback */
	if (io_req->callback) {
	    io_req->callback(io_req);
	} else
	    printk("blk_front WARNING: no callback\n");
    }

    /* increase ring counter */
    dev->ring.rsp_cons = cons;

    if (cons != dev->ring.req_prod_pvt) {
	int more_to_do;
	RING_FINAL_CHECK_FOR_RESPONSES(&dev->ring, more_to_do);
	if (more_to_do)
	    goto again;
    } else
	dev->ring.sring->rsp_event = cons + 1;
}

static void __blk_front_handler(evtchn_port_t port, void *data)
{
    struct blk_dev *dev = (struct blk_dev *)data;
    spin_lock(&dev->lock);
    if (dev->state == ST_READY)
	blk_front_handler(port, dev);
    spin_unlock(&dev->lock);
}


static int blk_init_ring(struct blkif_sring *ring, struct blk_dev *dev)
{

    if (ring == NULL)
	ring = (struct blkif_sring*)alloc_page();

    if (!ring) {
	DEBUG("%s error: alloc_page\n", __FUNCTION__);
	return 1;
    }
    memset(ring, 0, PAGE_SIZE);

    SHARED_RING_INIT(ring);
    FRONT_RING_INIT(&dev->ring, ring, PAGE_SIZE);

    dev->ring_ref = gnttab_grant_access(0, virt_to_mfn(ring), 0);
    return 0;
}

static int blk_get_evtchn(struct blk_dev *dev)
{
    evtchn_alloc_unbound_t op;
    op.dom = DOMID_SELF;
    op.remote_dom = 0;
    if(HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &op)) {
	DEBUG("%s ERROR: event_channel_op\n", __FUNCTION__);
	return 1;
    }
    clear_evtchn(op.port);

    dev->local_port = bind_evtchn(op.port, ANY_CPU, __blk_front_handler, dev);
    dev->evtchn = op.port;

    return 0;
}

void blk_rebind_evtchn(int cpu, struct blk_dev *dev)
{
    evtchn_bind_to_cpu(dev->evtchn, cpu);
}

static int blk_explore(char ***dirs)
{
    char *msg;
    int i;

    msg = xenbus_ls(XBT_NIL, DEVICE_STRING, dirs);
    if (msg) {
	free(msg);
	*dirs = NULL;
	return -1;
    }

    for (i=0; dirs[i]; i++) ; /* count the connected devices */

    return i;
}

static int blk_inform_back(char *path, struct blk_dev *dev)
{
    int retry = 0;
    char *err;
    xenbus_transaction_t xbt;

again:
    err = xenbus_transaction_start(&xbt);
    if (err) {
	free(err);
	printk("%s ERROR: transaction_start\n", __FUNCTION__);
	return EAGAIN;
    }

    err = xenbus_printf(xbt, path, "ring-ref", "%u", dev->ring_ref);
    if (err) {
	printk("%s ERROR: printf ring ref\n", __FUNCTION__);
	goto abort;
    }

    err = xenbus_printf(xbt, path, "event-channel", "%u", dev->evtchn);
    if (err) {
	printk("%s ERROR: printf path\n", __FUNCTION__);
	goto abort;
    }

    err = xenbus_printf(xbt, path, "state", "%u", XenbusStateInitialised);
    if (err) {
	printk("%s ERROR: printf state\n", __FUNCTION__);
	goto abort;
    }
    err = xenbus_transaction_end(xbt, 0, &retry);
    if(retry) {
	goto again;
    } else if (err) {
	free(err);
    }

    return 0;
abort:
    if(err)
	free(err);
    DEBUG("%s ERROR: abort transaction\n", __FUNCTION__);
    err = xenbus_transaction_end(xbt, 1, &retry);
    if(err)
	free(err);

    return 1;
}

static int blk_connected(char *path)
{
    int retry = 0;
    char *err;
    xenbus_transaction_t xbt;

again:
    err = xenbus_transaction_start(&xbt);
    if (err) {
	printk("%s ERROR: transaction_start\n", __FUNCTION__);
	return EAGAIN;
    }
    err = xenbus_printf(xbt, path, "state", "%u", XenbusStateConnected);
    if (err) {
	free(err);
	goto abort;
    }
    err = xenbus_transaction_end(xbt, 0, &retry);
    if(retry) {
	printk("%s ERROR: transaction end\n", __FUNCTION__);
	goto again;
    } else if (err)
	free(err);

    return 0;
abort:
    printk("%s ERROR: abort transaction\n", __FUNCTION__);
    err = xenbus_transaction_end(xbt, 1, &retry);
    if (err)
	free(err);
    return 1;
}

static inline void wait_for_init(void)
{
    wait_for_completion(&ready_completion);
}

int guk_blk_get_devices(void)
{
    int i;
    wait_for_init();
    for (i = 0; i < MAX_DEVICES; ++i) {
	if(blk_devices[i].state != ST_READY)
	    break;
    }
    return i;
}

int guk_blk_get_sectors(int device)
{
    if (device >= MAX_DEVICES || blk_devices[device].state != ST_READY)
	return 0;

    return blk_devices[device].device.sectors;
}

static inline long wait_for_device_ready(struct blk_dev *dev)
{
    long flags;
    spin_lock_irqsave(&dev->lock, flags);
    while (dev->state != ST_READY) {
	spin_unlock_irqrestore(&dev->lock, flags);
	wait_for_completion(&ready_completion);
	spin_lock_irqsave(&dev->lock, flags);
    }
    return flags;
}

/*
 * submit an async request
 */
int guk_blk_do_io(struct blk_request *io_req)
{
    struct blkif_request *xen_req;
    struct blk_shadow *shadow;
    int notify;
    int id;
    int err = 0;
    RING_IDX prod;
    struct blk_dev *dev = &blk_devices[io_req->device];

    BUG_ON(io_req->state != BLK_EMPTY);
    BUG_ON(io_req->device != 0);
    BUG_ON(dev->state != ST_READY);
    BUG_ON((io_req->address >> SECTOR_BITS) + io_req->end_sector > dev->device.sectors);

    prod = dev->ring.req_prod_pvt;
    xen_req = RING_GET_REQUEST(&dev->ring, prod);

    id = get_freelist_id();
    if (!id) {
	DEBUG("run out of IO requests\n");
	err = ENOMEM;
	goto out;
    }
    shadow = &shadows[id];
    shadow->request = io_req;
    xen_req->id = id;

    /* data to write/read */
    /* first segment */
    xen_req->seg[0].gref = gnttab_grant_access(0, virt_to_mfn(io_req->pages[0]), 0);
    shadow->gref[0] = xen_req->seg[0].gref;
    xen_req->seg[0].first_sect = io_req->start_sector;
    if (io_req->num_pages == 1) /* only one segment; first and last segment at once */
	xen_req->seg[0].last_sect = io_req->end_sector;
    else {
	int i;
	xen_req->seg[0].last_sect = SECTORS_PER_PAGE - 1;
	/* the middle segments */
	for(i = 1; i < io_req->num_pages; ++i) {
	    xen_req->seg[i].gref = gnttab_grant_access(0, virt_to_mfn(io_req->pages[i]), 0);;
	    shadow->gref[i] = xen_req->seg[i].gref;
	    xen_req->seg[i].first_sect = 0;
	    xen_req->seg[i].last_sect = SECTORS_PER_PAGE - 1;
	}
	/* last segment */
	xen_req->seg[i].last_sect = io_req->end_sector;
    }

    shadow->num_refs = io_req->num_pages;
    xen_req->nr_segments = io_req->num_pages;

    /* where to write to/read from */
    xen_req->sector_number = addr_to_sec(io_req->address);
    xen_req->handle = 0x12;/* FIXME: what is the correct handle? */
    xen_req->operation = io_req->operation; /* read or write */
    io_req->state = BLK_SUBMITTED;
    notify = 0;
    dev->ring.req_prod_pvt = prod + 1;

    wmb();
    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&dev->ring, notify);
    if (notify) {
	notify_remote_via_evtchn(dev->evtchn);
    }
out:
    return err;
}

static void complete_callback(struct blk_request *req)
{
    struct completion *comp = (struct completion *)req->callback_data;
    complete(comp);

}

static inline void set_default_callback(struct blk_request *req, struct completion *comp)
{
    req->callback = complete_callback;
    req->callback_data = (unsigned long)comp;
}

/*
 * synchronous interface, blocks calling thread until request is
 * processed and data is available or written onto the disk
 */
int guk_blk_write(int device, long address, void *buf, int size)
{
    uint8_t *pages;
    int sectors;
    int free_buf, order;
    struct blk_request req;
    struct completion comp;
    int i;

    BUG_ON(address & (SECTOR_SIZE-1));
    BUG_ON(size & (SECTOR_SIZE - 1));

    sectors = size >> SECTOR_BITS;
    if (((unsigned long)buf & (PAGE_SIZE - 1)) 
	    || ((unsigned long)size & (SECTOR_SIZE -1)) ) {
	DEBUG("buffer not page aligned! buffer does not end at a sector boundary\n");
	order = get_order(size);
	pages = (uint8_t *)alloc_pages(order);
	memcpy(pages, buf, size);
	free_buf = 1;
    } else {
	pages = buf;
	free_buf = order = 0;
    }

    i = 0;
    while (size > PAGE_SIZE) {
	req.pages[i] = pages + i*PAGE_SIZE;
	size -= PAGE_SIZE;
	++i;
    }
    req.pages[i] = pages + i*PAGE_SIZE;
    req.num_pages = i+1;
    req.start_sector = 0;
    req.end_sector = (size >> SECTOR_BITS) - 1;
    req.device = device;
    req.address = address;
    req.state = BLK_EMPTY;
    req.operation = BLK_REQ_WRITE;

    /* on return keep the lock of the device */
    long flags = wait_for_device_ready(&blk_devices[device]);

    init_completion(&comp);
    set_default_callback(&req, &comp);

    if (blk_do_io(&req)) {
	sectors = -1;
	spin_unlock_irqrestore(&blk_devices[device].lock, flags);
    } else {
	spin_unlock_irqrestore(&blk_devices[device].lock, flags);
	wait_for_completion(&comp);
    }

    if(free_buf)
	free_pages(pages, order);

    if (req.state == BLK_DONE_SUCCESS)
	return sectors;
    else
	return -1;
}

/*
 * synchronous interface, blocks calling thread until request is
 * processed and data is available or written onto the disk
 */
int guk_blk_read(int device, long address, void *buf, int size)
{
    uint8_t *pages;
    int sectors;
    int free_buf, order;
    struct blk_request req;
    struct completion comp;
    int i;

    BUG_ON(address & (SECTOR_SIZE-1));
    BUG_ON(size & (SECTOR_SIZE - 1));

    sectors = size >> SECTOR_BITS;
    if (((unsigned long)buf & (PAGE_SIZE - 1))
	    | ((unsigned long)size & (SECTOR_SIZE -1)) ) {
	DEBUG("buffer not page aligned! buffer does not end at a sector boundary\n");
	order = get_order(size);
	pages = (uint8_t *)alloc_pages(order);
	free_buf = 1;
    } else {
	pages = buf;
	free_buf = order = 0;
    }

    i = 0;
    while (size > PAGE_SIZE) {
	req.pages[i] = pages + i*PAGE_SIZE;
	size -= PAGE_SIZE;
	++i;
    }
    req.pages[i] = pages + i*PAGE_SIZE;
    req.num_pages = i+1;
    req.start_sector = 0;
    req.end_sector = (size >> SECTOR_BITS) - 1;
    req.device = device;
    req.address = address;
    req.state = BLK_EMPTY;
    req.operation = BLK_REQ_READ;

    long flags = wait_for_device_ready(&blk_devices[device]);
    init_completion(&comp);
    set_default_callback(&req, &comp);

    if (blk_do_io(&req)) {
	spin_unlock_irqrestore(&blk_devices[device].lock, flags);
	return -1;
    } else {
	spin_unlock_irqrestore(&blk_devices[device].lock, flags);
	wait_for_completion(&comp);
    }
    if (free_buf) {
	memcpy(buf, pages, size);
	free_pages(pages, order);
    }

    if (req.state == BLK_DONE_SUCCESS)
	return sectors;
    else
	return -1;
}

static int blk_shutdown(void)
{
    return 0;
}

static void post_connect(struct blk_dev *dev)
{
    char xenbus_path[MAX_PATH];
    int error;

    /* get the device info from the backend */
    snprintf(xenbus_path, MAX_PATH, "%s/sector-size", dev->backend);
    dev->device.sector_size = xenbus_read_integer(xenbus_path);

    snprintf(xenbus_path, MAX_PATH, "%s/sectors", dev->backend);
    dev->device.sectors = xenbus_read_integer(xenbus_path);

    snprintf(xenbus_path, MAX_PATH, "%s/info", dev->backend);
    dev->device.info = xenbus_read_integer(xenbus_path);

    snprintf(xenbus_path, MAX_PATH, "%s/%d", DEVICE_STRING, dev->device.id);
    error = blk_connected(xenbus_path);
    if (error) {
	DEBUG("%s ERROR: connected\n", __FUNCTION__);
	return;
    }

    long flags;
    spin_lock_irqsave(&dev->lock, flags);
    dev->state = ST_READY;
    complete_all(&ready_completion);
    spin_unlock_irqrestore(&dev->lock, flags);
}

static void blk_init(void)
{
    char **devices;
    int num_devices;
    int i;
    char xenbus_path[MAX_PATH];
    int error;
    char *err;

    xenbus_watch_path(XBT_NIL, DEVICE_STRING, "blk_xenbus");
again:
    num_devices = blk_explore(&devices);
    if(num_devices < 0) {
	char *msg;
	msg = xenbus_read_watch("blk_xenbus");
	free(msg);
	DEBUG("blk_front no block devices found\n");
	complete_all(&ready_completion);
	goto again;
    }
    xenbus_rm_watch("blk_xenbus");

    init_buffers();

    for (i=0; i<num_devices && i<MAX_DEVICES; ++i) {
	blk_devices[i].device.id = (int)simple_strtol(devices[i], NULL, 10);
	snprintf(xenbus_path, MAX_PATH, "%s/%s/backend", DEVICE_STRING, devices[i]);

	err = xenbus_read(XBT_NIL, xenbus_path, &blk_devices[i].backend);
	if (err) {
	    printk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, err);
	    free(err);
	    complete_all(&ready_completion);
	    continue;
	}

	snprintf(xenbus_path, MAX_PATH, "%s/%s", DEVICE_STRING, devices[i]);

	if (blk_init_ring(NULL, &blk_devices[i])) {
	    complete_all(&ready_completion);
	    continue;
	}

	if (blk_get_evtchn(&blk_devices[i])) {
	    complete_all(&ready_completion);
	    continue;
	}

	error = blk_inform_back(xenbus_path, &blk_devices[i]);
	if (error) {
	    printk("%s ERROR: initialized\n", __FUNCTION__);
	    complete_all(&ready_completion);
	    continue;
	}


	snprintf(xenbus_path, MAX_PATH, "%s/state", blk_devices[i].backend);
	xenbus_watch_path(XBT_NIL, xenbus_path, "blk-front");
	xenbus_wait_for_value("blk-front", xenbus_path, "4"); /* wait for backend connection */

	post_connect(&blk_devices[i]);
    }
    for (i=0; i<num_devices; ++i)
	    free(devices[i]);
    free(devices);
    return;
}

static int ring_has_incomp_req(struct blkif_front_ring *ring)
{
    return (ring->req_prod_pvt != ring->sring->rsp_prod);
}

static int ring_has_unprocessed_resp(struct blkif_front_ring *ring)
{
    return (ring->rsp_cons != ring->sring->rsp_prod);
}

#define MAX_POLL 12
static long drain_io(long flags, struct blk_dev *dev)
{
    int i;

    dev = &blk_devices[0];

    for(i = 0; i < MAX_POLL; ++i) {
	if (ring_has_unprocessed_resp(&dev->ring)) {
	    blk_front_handler(0, dev);
	}
	if(!ring_has_incomp_req(&dev->ring))
	    break;

	if(!in_irq()){
	    spin_unlock_irqrestore(&dev->lock, flags);
	    sleep(5<<i);
	    spin_lock_irqsave(&dev->lock, flags);
	}
    }
    if(i == MAX_POLL && ring_has_incomp_req(&dev->ring)) {
	xprintk("BLOCK DEVICE WARNING: could not finish I/O requests in %d iterations\n", i);
	DEBUG("BLOCK DEVICE WARNING: could not finish I/O requests in %d iterations\n", i);
    }
    return flags;
}

static int blk_suspend(void)
{
    long flags;
    struct blk_dev *dev;
    int i;

    for (i=0; i<MAX_DEVICES; ++i) {
	dev = &blk_devices[i];

	unbind_evtchn(dev->evtchn);
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->state != ST_READY) {
	    spin_unlock_irqrestore(&dev->lock, flags);
	    continue;
	}

	dev->state = ST_SUSPENDING;
	flags = drain_io(flags, dev);
	spin_unlock_irqrestore(&dev->lock, flags);
    }
    init_completion(&ready_completion);
    /* all pending requests returned */

    xenbus_rm_watch("blk-front");
    for (i=0; i<MAX_DEVICES; ++i)
	if (blk_devices[i].state == ST_SUSPENDING)
	    blk_devices[i].state = ST_SUSPENDED;

    return 0;
}

static int blk_resume(void)
{
    int retval = 1;
    char xenbus_path[MAX_PATH];
    char * err;
    char **devices;
    int num_devices;
    struct blk_dev *dev;
    int i;

    num_devices = blk_explore(&devices);
    if(num_devices < 0)
	goto out;

    for(i=0; i<num_devices && i<MAX_DEVICES; ++i) {
	dev = &blk_devices[i];

	if (dev->state != ST_SUSPENDED)
	    continue;

	dev->state = ST_RESUMING;


	snprintf(xenbus_path, MAX_PATH, "%s/%s/backend", DEVICE_STRING, devices[i]);
	err = xenbus_read(XBT_NIL, xenbus_path, &dev->backend);
	if (err) {
	    printk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, err);
	    free(err);
	}

	if (blk_init_ring(dev->ring.sring, dev)) {
	    continue;
	}

	if (blk_get_evtchn(dev)) {
	    continue;
	}

	snprintf(xenbus_path, MAX_PATH, "%s/%s", DEVICE_STRING, devices[i]);
	if (blk_inform_back(xenbus_path, dev)) {
	    printk("%s ERROR: initialised\n", __FUNCTION__);
	    continue;
	}

	snprintf(xenbus_path, MAX_PATH, "%s/state", dev->backend);
	xenbus_watch_path(XBT_NIL, xenbus_path, "blk-front");
	xenbus_wait_for_value("blk-front", xenbus_path, "4"); /* wait for backend connection */

	post_connect(dev);
	retval = 0;
    }

    for(i=0; i<num_devices; ++i)
	free(devices[i]);
    free(devices);
out:
    return retval;
}


static void blk_thread(void *p)
{
    int num_devices;

    blk_init();
    num_devices = blk_get_devices();
    if (trace_blk()) {
	if (num_devices > 0 && trace_blk()) {
	    int i;
	    printk("block front initialized %d devices\n", num_devices);
	    for (i=0; i < num_devices; ++i)
		printk("\tdevice [%d] with %d sectors\n", i, blk_get_sectors(i));
	}
    }
}

static int start_blk_front(void *arg)
{
    create_thread("blk_front_thread", blk_thread, UKERNEL_FLAG, arg);
    return 0;
}

static struct service blk_service = {
    .name = "blk_service",
    .init = start_blk_front,
    .shutdown = blk_shutdown,
    .suspend = blk_suspend,
    .resume = blk_resume,
    .arg = NULL,
};

USED static int init_func(void)
{
    int i;
    memset(&blk_devices, 0, sizeof(blk_devices));

    for (i=0; i<MAX_DEVICES; ++i) {
	spin_lock_init(&blk_devices[i].lock);
	blk_devices[i].state = ST_UNKNOWN;
    }
    init_completion(&ready_completion);
    register_service(&blk_service);
    return 0;
}
DECLARE_INIT(init_func);
