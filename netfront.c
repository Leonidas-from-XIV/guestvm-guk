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
/* Minimal network driver for Mini-OS. 
 * Copyright (c) 2006-2007 Jacob Gorm Hansen, University of Copenhagen.
 * Based on netfront.c from Xen Linux.
 *
 * Does not handle fragments or extras.
 *
 * Modified: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007,
             Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 */

#include <guk/os.h>
#include <guk/init.h>
#include <guk/service.h>
#include <guk/xenbus.h>
#include <guk/events.h>
#include <guk/gnttab.h>
#include <guk/xmalloc.h>
#include <guk/time.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/smp.h>
#include <guk/trace.h>

#include <xen/io/netif.h>
#include <spinlock.h>
#include <errno.h>

static struct net_info {
    struct netif_tx_front_ring tx;
    struct netif_rx_front_ring rx;
    int tx_ring_ref;
    int rx_ring_ref;
    unsigned int evtchn, local_port;
    int rings;
    int device_id;
    int state;
#define ST_UNKNOWN      0
#define ST_READY        1
#define ST_SUSPENDING   2
#define ST_SUSPENDED    3
#define ST_RESUMING     4
} net_info;

static DEFINE_SPINLOCK(net_info_lock);

#define DEVICE_STRING "device/vif"
#define MAX_PATH      128

#define NET_TX_RING_SIZE __RING_SIZE((struct netif_tx_sring *)0, PAGE_SIZE)
#define NET_RX_RING_SIZE __RING_SIZE((struct netif_rx_sring *)0, PAGE_SIZE)
#define GRANT_INVALID_REF 0

#define WATCH_TOKEN "net-front"

static unsigned short rx_freelist[NET_RX_RING_SIZE];
static unsigned short tx_freelist[NET_TX_RING_SIZE];

struct net_buffer {
    void* page;
    int gref;
};

static DEFINE_SPINLOCK(freelist_lock);
static struct net_buffer rx_buffers[NET_RX_RING_SIZE];
static struct net_buffer tx_buffers[NET_TX_RING_SIZE];

static inline void add_id_to_freelist(unsigned int id, unsigned short* freelist)
{
    long flags;
    spin_lock_irqsave(&freelist_lock, flags);
    freelist[id] = freelist[0];
    freelist[0]  = id;
    spin_unlock_irqrestore(&freelist_lock, flags);
}

static inline unsigned short get_id_from_freelist(unsigned short* freelist)
{
    unsigned int id;
    long flags;
    spin_lock_irqsave(&freelist_lock, flags);
    id = freelist[0];
    freelist[0] = freelist[id];
    spin_unlock_irqrestore(&freelist_lock, flags);
    return id;
}

__attribute__((weak)) void guk_netif_rx(unsigned char* data,int len)
{
    struct thread *thread = current;
    printk("%d bytes incoming at %p, thread %s\n",len,data, thread->name);
}

__attribute__((weak)) void guk_net_app_main(unsigned char *mac, char *nic) {
}

static inline int xennet_rxidx(RING_IDX idx)
{
    return idx & (NET_RX_RING_SIZE - 1);
}

static void network_rx(void)
{
    struct net_info *np = &net_info;
    RING_IDX rp,cons;
    struct netif_rx_response *rx;

    if (net_info.state != ST_READY)
	return;

moretodo:
    rp = np->rx.sring->rsp_prod;
    rmb(); /* Ensure we see queued responses up to 'rp'. */
    cons = np->rx.rsp_cons;

    int nr_consumed=0;
    while ((cons != rp))
    {
        struct net_buffer* buf;
        unsigned char* page;

        rx = RING_GET_RESPONSE(&np->rx, cons);

        if (rx->flags & NETRXF_extra_info)
        {
            printk("+++++++++++++++++++++ we have extras!\n");
            continue;
        }


        if (rx->status == NETIF_RSP_NULL) continue;

        int id = rx->id;

        buf = &rx_buffers[id];
        page = (unsigned char*)buf->page;
        gnttab_end_access(buf->gref);

        if(rx->status > 0) {
            guk_netif_rx(page+rx->offset,rx->status);
        }

        add_id_to_freelist(id,rx_freelist);

        nr_consumed++;

        ++cons;
    }
    np->rx.rsp_cons=rp;

    int more;
    RING_FINAL_CHECK_FOR_RESPONSES(&np->rx,more);
    if(more)
	goto moretodo;

    RING_IDX req_prod = np->rx.req_prod_pvt;

    int i;
    netif_rx_request_t *req;

    for(i=0; i<nr_consumed; i++)
    {
        int id = xennet_rxidx(req_prod + i);
        req = RING_GET_REQUEST(&np->rx, req_prod + i);
        struct net_buffer* buf = &rx_buffers[id];
        void* page = buf->page;

        buf->gref = req->gref =
            gnttab_grant_access(0,virt_to_mfn(page),0);

        req->id = id;
    }

    wmb();

    np->rx.req_prod_pvt = req_prod + i;

    int notify;
    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&np->rx, notify);
    if (notify)
        notify_remote_via_evtchn(np->evtchn);

}

static void network_tx_buf_gc(void)
{
    RING_IDX cons, prod;
    unsigned short id;
    struct net_info *np = &net_info;

    if (net_info.state != ST_READY)
	return;
    
    do {
        prod = np->tx.sring->rsp_prod;
        rmb(); /* Ensure we see responses up to 'rp'. */

        for (cons = np->tx.rsp_cons; cons != prod; cons++) 
        {
            struct netif_tx_response *txrsp;

            txrsp = RING_GET_RESPONSE(&np->tx, cons);
            if (txrsp->status == NETIF_RSP_NULL)
                continue;

            id  = txrsp->id;
            struct net_buffer* buf = &tx_buffers[id];
            gnttab_end_access(buf->gref);
            buf->gref=GRANT_INVALID_REF;

            add_id_to_freelist(id,tx_freelist);
        }

        np->tx.rsp_cons = prod;

        /*
         * Set a new event, then check for race with update of tx_cons.
         * Note that it is essential to schedule a callback, no matter
         * how few tx_buffers are pending. Even if there is space in the
         * transmit ring, higher layers may be blocked because too much
         * data is outstanding: in such cases notification from Xen is
         * likely to be the only kick that we'll get.
         */
        np->tx.sring->rsp_event =
            prod + ((np->tx.sring->req_prod - prod) >> 1) + 1;
        mb();
    } while ((cons == prod) && (prod != np->tx.sring->rsp_prod));
}

static void netfront_handler(evtchn_port_t port, void *data)
{
    spin_lock(&net_info_lock);

    network_tx_buf_gc();
    network_rx();

    spin_unlock(&net_info_lock);
}

static char* backend;

static void alloc_buffers(void)
{
    int i;
    for(i=0;i<NET_TX_RING_SIZE;i++) {
        add_id_to_freelist(i,tx_freelist);
	if (tx_buffers[i].page == NULL)
	    tx_buffers[i].page = (char*)alloc_page();
    }

    for(i=0;i<NET_RX_RING_SIZE;i++) {
        add_id_to_freelist(i,rx_freelist);
	if (rx_buffers[i].page == NULL)
	    rx_buffers[i].page = (char*)alloc_page();
    }

}

static inline void alloc_rings(struct net_info *info)
{
    struct netif_tx_sring *txs;
    struct netif_rx_sring *rxs;

    if (!info->rings) {
	txs = (struct netif_tx_sring*) alloc_page();
	rxs = (struct netif_rx_sring *) alloc_page();
	memset(txs,0,PAGE_SIZE);
	memset(rxs,0,PAGE_SIZE);
    }

    SHARED_RING_INIT(txs);
    SHARED_RING_INIT(rxs);
    FRONT_RING_INIT(&info->tx, txs, PAGE_SIZE);
    FRONT_RING_INIT(&info->rx, rxs, PAGE_SIZE);

    info->tx_ring_ref = gnttab_grant_access(0,virt_to_mfn(txs),0);
    info->rx_ring_ref = gnttab_grant_access(0,virt_to_mfn(rxs),0);

}

static inline int alloc_evtchn(struct net_info *info)
{
    int retval;
    evtchn_alloc_unbound_t op;
    op.dom = DOMID_SELF;
    op.remote_dom = 0;

    retval = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &op);

    clear_evtchn(op.port);        /* Without, handler gets invoked now! */
    info->local_port = bind_evtchn(op.port, ANY_CPU, netfront_handler, NULL);
    info->evtchn = op.port;

    return retval;
}

USED static void netfront_unmap_rx_buffers(void)
{
    int i, notify;
    struct net_info *np = &net_info;
    for (i = 0; i < NET_RX_RING_SIZE; i++)
    {
        struct net_buffer* buf = &rx_buffers[i];
	gnttab_end_transfer(buf->gref);
	gnttab_end_access(buf->gref);
    }

    np->rx.req_prod_pvt = 0;

    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&np->rx, notify);

    if (notify)
        notify_remote_via_evtchn(np->evtchn);

    np->rx.sring->rsp_event = np->rx.rsp_cons + 1;

}

static void init_rx_buffers(void)
{
    struct net_info* np = &net_info;
    int i, requeue_idx;
    netif_rx_request_t *req;
    int notify;

    /* Rebuild the RX buffer freelist and the RX ring itself. */
    if (trace_net())
	tprintk("netfront map %d pages\n", NET_RX_RING_SIZE);
    for (requeue_idx = 0, i = 0; i < NET_RX_RING_SIZE; i++)
    {
        struct net_buffer* buf = &rx_buffers[requeue_idx];
        req = RING_GET_REQUEST(&np->rx, requeue_idx);

        buf->gref = req->gref =
            gnttab_grant_access(0,virt_to_mfn(buf->page),0);

        req->id = requeue_idx;

        requeue_idx++;
    }

    np->rx.req_prod_pvt = requeue_idx;

    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&np->rx, notify);

    if (notify)
        notify_remote_via_evtchn(np->evtchn);

    np->rx.sring->rsp_event = np->rx.rsp_cons + 1;
}

static char *nf_explore(void)
{
    char *first;
    char *msg;
    char **dirs;
    int i;

    msg = xenbus_ls(XBT_NIL, DEVICE_STRING, &dirs);
    if (msg) {
	free(msg);
	return NULL;
    }

    for (i=0; dirs[i]; i++) {
	if(i>0)
	    free(dirs[i]);
    }
    if (i==0)
	return NULL;

    first = dirs[0];
    free(dirs);
    return first;
}

static void init_netfront(void)
{
    xenbus_transaction_t xbt;
    struct net_info* info = &net_info;
    char* err;
    char* message=NULL;
    char nodename[MAX_PATH];
    int retry=0;
    char* mac;
    char* msg;
    char* nic;
    char *device; 

    if (trace_net()) 
	tprintk("Initialising netfront\n");

    device = nf_explore();
    if (!device) {
	if (trace_net())
	    tprintk("%s no devices found\n", __FUNCTION__);
	goto out_err;
    }

    info->device_id = (int)simple_strtol(device, NULL, 10);
    snprintf(nodename, MAX_PATH, "%s/%s", DEVICE_STRING, device);

    alloc_buffers();

    alloc_rings(info);

    BUG_ON(alloc_evtchn(info));

again:
    err = xenbus_transaction_start(&xbt);
    if (err) {
        tprintk("starting transaction\n");
    }

    err = xenbus_printf(xbt, nodename, "tx-ring-ref","%u",
                info->tx_ring_ref);
    if (err) {
	free(err);
        message = "writing tx ring-ref";
        goto abort_transaction;
    }
    err = xenbus_printf(xbt, nodename, "rx-ring-ref","%u",
                info->rx_ring_ref);
    if (err) {
	free(err);
        message = "writing rx ring-ref";
        goto abort_transaction;
    }
    err = xenbus_printf(xbt, nodename,
                "event-channel", "%u", info->evtchn);
    if (err) {
	free(err);
        message = "writing event-channel";
        goto abort_transaction;
    }

    err = xenbus_printf(xbt, nodename, "request-rx-copy", "%u", 1);

    if (err) {
	free(err);
        message = "writing request-rx-copy";
        goto abort_transaction;
    }

    err = xenbus_printf(xbt, nodename, "state", "%u", 4); /* connected */
    if(err) {
	free(err);
    }


    err = xenbus_transaction_end(xbt, 0, &retry);
    if (retry) {
            goto again;
        tprintk("completing transaction\n");
    } else if (err)
	free(err);

    goto done;

abort_transaction:
    err = xenbus_transaction_end(xbt, 1, &retry);
    if (err)
	free(err);
    goto out_err_free;

done:

    snprintf(nodename, MAX_PATH, "%s/%s/backend", DEVICE_STRING, device);
    msg = xenbus_read(XBT_NIL, nodename, &backend);
    if (msg) {
	if (trace_net())
	    tprintk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, msg);
	free(msg);
	goto out_err_free;
    }

    snprintf(nodename, MAX_PATH, "%s/%s/mac", DEVICE_STRING, device);
    msg = xenbus_read(XBT_NIL, nodename, &mac);
    if (msg) {
	if (trace_net())
	    tprintk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, msg);
	free(msg);
	goto out_err_free;
    }

    if ((backend == NULL) || (mac == NULL)) {
        struct evtchn_close op = { info->local_port };
        if (trace_net()) tprintk("%s: backend/mac failed\n", __func__);
        unbind_evtchn(info->evtchn);
        HYPERVISOR_event_channel_op(EVTCHNOP_close, &op);
	goto out_err_free;
    }

    if (trace_net()) tprintk("backend at %s\n",backend);
    if (trace_net()) tprintk("mac is %s\n",mac);

    /* This seems to be Solaris xVM specific, so no error if it isn't found.
       It's mainly a debugging aid for snooping the vnic */
    snprintf(nodename, MAX_PATH, "%s/nic", backend);
    msg = xenbus_read(XBT_NIL, nodename, &nic);
    if (msg) {
	// printk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, msg);
	free(msg);
	nic = "unknown";
    }
    if (trace_net())
	tprintk("nic is %s\n",nic);

    snprintf(nodename, MAX_PATH, "%s/state", backend);

    msg = xenbus_watch_path(XBT_NIL, nodename, WATCH_TOKEN);
    if (msg) {
	free(msg);
    }

    xenbus_wait_for_value(WATCH_TOKEN, nodename,"4");

    if (trace_net()) tprintk("*********** connected to backend ***************\n");

    init_rx_buffers();

    unsigned char rawmac[6];
        /* Special conversion specifier 'hh' needed for __ia64__. Without
           this kernel panics with 'Unaligned reference'. */
    sscanf(mac,"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
            &rawmac[0],
            &rawmac[1],
            &rawmac[2],
            &rawmac[3],
            &rawmac[4],
            &rawmac[5]);

    spin_lock(&net_info_lock);
    info->state = ST_READY;
    spin_unlock(&net_info_lock);

    if (trace_net())
	tprintk("** call guk_net_app_main **\n");
    guk_net_app_main(rawmac, nic);

    free(device);
    return;

out_err_free:
    free(device);
out_err:
    guk_net_app_main(NULL, NULL);
    return;
}

void guk_netfront_xmit(unsigned char* data,int len)
{
    int flags;
    struct net_info* info = &net_info;
    struct netif_tx_request *tx;
    RING_IDX i;
    int notify;
    int id = get_id_from_freelist(tx_freelist);
    struct net_buffer* buf = &tx_buffers[id];
    void* page = buf->page;

    spin_lock_irqsave(&net_info_lock, flags);
    if (net_info.state != ST_READY) {
	spin_unlock_irqrestore(&net_info_lock, flags);
	return;
    }

    i  = info->tx.req_prod_pvt;
    tx = RING_GET_REQUEST(&info->tx, i);

    memcpy(page,data,len);

    buf->gref =
        tx->gref = gnttab_grant_access(0,virt_to_mfn(page),0);

    tx->offset=0;
    tx->size = len;
    tx->flags=0;
    tx->id = id;
    info->tx.req_prod_pvt = i + 1;

    wmb();
    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->tx, notify);

    if(notify) 
	notify_remote_via_evtchn(info->evtchn);

    //network_tx_buf_gc();

    spin_unlock_irqrestore(&net_info_lock, flags);
}

static int netfront_shutdown(void)
{
    long flags;

    spin_lock_irqsave(&net_info_lock, flags);
    if (backend == NULL || net_info.state != ST_READY) {
	spin_unlock_irqrestore(&net_info_lock, flags);
	return 1;
    }

    unbind_evtchn(net_info.local_port); /* no more interrupts from the network */
    net_info.state = ST_SUSPENDING;
    spin_unlock_irqrestore(&net_info_lock, flags);

    if (trace_net())
	tprintk("close network: backend at %s\n",backend);

    char* err;
    char nodename[MAX_PATH];

    snprintf(nodename, MAX_PATH, "%s/%d", DEVICE_STRING, net_info.device_id);
    err = xenbus_printf(XBT_NIL, nodename, "state", "%u", 6); /* closing */
    if(err) {
	free(err);
    }

    snprintf(nodename, MAX_PATH, "%s/state", backend);
    xenbus_wait_for_value(WATCH_TOKEN, nodename,"6");

    snprintf(nodename, MAX_PATH, "%s/%d", DEVICE_STRING, net_info.device_id);
    err = xenbus_printf(XBT_NIL, nodename, "state", "%u", 1);
    if(err) {
	free(err);
    }

    snprintf(nodename, MAX_PATH, "%s/state", backend);
    xenbus_wait_for_value(WATCH_TOKEN, nodename,"2");

    netfront_unmap_rx_buffers();

    xenbus_rm_watch(WATCH_TOKEN);
    spin_lock(&net_info_lock);
    net_info.state = ST_SUSPENDED;
    spin_unlock(&net_info_lock);
    return 0;

}

static void netfront_thread(void *p)
{
    init_netfront();
    if (trace_net())
	tprintk("netfront thread terminating\n");
}

static int start_netfront_thread(void *arg)
{
    create_thread("netfront", netfront_thread, UKERNEL_FLAG, NULL);
    return 0;
}

static int netfront_suspend(void)
{
    return netfront_shutdown();
}

static int netfront_resume(void)
{
    return start_netfront_thread(NULL);
}

static struct service nf_service = {
    .name = "netfront service",
    .init = start_netfront_thread,
    .shutdown = netfront_shutdown,
    .suspend = netfront_suspend,
    .resume = netfront_resume,
    .arg = NULL,
};

USED static int init_func(void)
{
    memset(&net_info, 0, sizeof(net_info));
    memset(rx_buffers, 0, sizeof(rx_buffers));
    memset(tx_buffers, 0, sizeof(tx_buffers));
    register_service(&nf_service);
    return 0;
}
DECLARE_INIT(init_func);
