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
 ****************************************************************************
 * (C) 2006 - Cambridge University
 ****************************************************************************
 *
 *        File: xenbus.c
 *      Author: Steven Smith (sos22@cam.ac.uk)
 *     Changes: Grzegorz Milos (gm281@cam.ac.uk)
 *     Changes: John D. Ramsdell
 *
 *        Date: Jun 2006, chages Aug 2005
 *
 * Environment: GuestXen microkernel volved from  Xen Minimal OS
 * Description: Minimal implementation of xenbus
 *
 ****************************************************************************
 **/
#include <guk/os.h>
#include <guk/mm.h>
#include <guk/traps.h>
#include <guk/xenbus.h>
#include <guk/events.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/wait.h>
#include <guk/xmalloc.h>
#include <guk/smp.h>
#include <guk/trace.h>
#include <guk/completion.h>
#include <guk/spinlock.h>

#include <lib.h>
#include <errno.h>

#include <xen/io/xs_wire.h>

#define min(x,y) ({                       \
        typeof(x) tmpx = (x);                 \
        typeof(y) tmpy = (y);                 \
        tmpx < tmpy ? tmpx : tmpy;            \
        })

#ifdef XENBUS_DEBUG
#define DEBUG(_f, _a...) \
    xprintk("MINI_OS(file=xenbus.c, line=%d) " _f , __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

static struct xenstore_domain_interface *xenstore_buf;
static DEFINE_SPINLOCK(xb_lock);
static DECLARE_WAIT_QUEUE_HEAD(xb_waitq);


static int suspend = 0;
static DECLARE_COMPLETION(suspend_comp);
static DECLARE_COMPLETION(resume_comp);

static struct thread *xenbus_thread;

struct xenbus_req_info
{
    int in_use;
    struct wait_queue_head waitq;
    void *reply;
};

#define NR_REQS 32
static struct xenbus_req_info req_info[NR_REQS];

static LIST_HEAD(watch_list);
static DEFINE_SPINLOCK(watch_list_lock);

struct xenbus_watch
{
    char *token;
    char *path;
#define MAX_PATHS   10
    int path_prod_idx;
    int path_cons_idx;
    char *paths[MAX_PATHS];
    struct thread *thread;
    struct list_head list;
};

static struct xenbus_watch* find_watch(char *token)
{
    struct list_head *list_element;
    struct xenbus_watch *watch;

    list_for_each(list_element, &watch_list)
    {
        watch = list_entry(list_element, struct xenbus_watch, list);
        if(strcmp(watch->token, token) == 0)
            return watch;
    }

    return NULL;
}

static void memcpy_from_ring(const void *Ring,
        void *Dest,
        int off,
        int len)
{
    int c1, c2;
    const char *ring = Ring;
    char *dest = Dest;
    c1 = min(len, XENSTORE_RING_SIZE - off);
    c2 = len - c1;
    memcpy(dest, ring + off, c1);
    memcpy(dest + c1, ring, c2);
}

//#undef DEBUG
//#define DEBUG xprintk
static void xenbus_thread_func(void *ign)
{
    struct xsd_sockmsg msg;
    unsigned prod = 0;

    for (;;)
    {
        wait_event(xb_waitq, (prod != xenstore_buf->rsp_prod) || suspend);
	if(suspend) {
	    complete_all(&suspend_comp);
	    wait_for_completion(&resume_comp);
	    BUG_ON(suspend);
	}
	while (1) {
	    long flags;
	    spin_lock_irqsave(&xb_lock, flags);
	    prod = xenstore_buf->rsp_prod;
	    DEBUG("Rsp_cons %d, rsp_prod %d.\n", xenstore_buf->rsp_cons,
		    xenstore_buf->rsp_prod);
	    if (xenstore_buf->rsp_prod - xenstore_buf->rsp_cons < sizeof(msg)) {
		spin_unlock_irqrestore(&xb_lock, flags);
		break;
	    }
	    rmb();
	    memcpy_from_ring(xenstore_buf->rsp,
		    &msg,
		    MASK_XENSTORE_IDX(xenstore_buf->rsp_cons),
		    sizeof(msg));
	    DEBUG("Msg len %d, %d avail, id %d.\n",
		    msg.len + sizeof(msg),
		    xenstore_buf->rsp_prod - xenstore_buf->rsp_cons,
		    msg.req_id);
	    if (xenstore_buf->rsp_prod - xenstore_buf->rsp_cons <
		    sizeof(msg) + msg.len) {
		spin_unlock_irqrestore(&xb_lock, flags);
		break;
	    }

	    DEBUG("Message is good.\n");

	    if(msg.type == XS_WATCH_EVENT) {
		char* payload = (char*)malloc(sizeof(msg) + msg.len);
		char *path,*token;
		struct xenbus_watch *watch;

		memcpy_from_ring(xenstore_buf->rsp,
			payload,
			MASK_XENSTORE_IDX(xenstore_buf->rsp_cons),
			msg.len + sizeof(msg));

		path = payload + sizeof(msg);
		token = path + strlen(path) + 1;
		DEBUG("watch event %s %s\n", path, token);
		spin_lock(&watch_list_lock);
		watch = find_watch(token);
		if(watch == NULL)
		{
		    printk("Spurious watch event for token: %s\n", token);
		    goto free_watch_msg;
		}
		/* Check if we haven't run out of space on our circural buf */
		BUG_ON(watch->path_prod_idx - watch->path_cons_idx > MAX_PATHS);
		watch->paths[watch->path_prod_idx++ % MAX_PATHS] = strdup(path);
		if(watch->thread) {
		    wake(watch->thread);
		}

free_watch_msg:
		spin_unlock(&watch_list_lock); 
		xenstore_buf->rsp_cons += msg.len + sizeof(msg);
		free(payload);
	    } else {
		DEBUG("NO watch event, msg id %d in use %d\n", msg.req_id, req_info[msg.req_id].in_use);
		BUG_ON(!req_info[msg.req_id].in_use);
		req_info[msg.req_id].reply = malloc(sizeof(msg) + msg.len);
		memcpy_from_ring(xenstore_buf->rsp,
			req_info[msg.req_id].reply,
			MASK_XENSTORE_IDX(xenstore_buf->rsp_cons),
			msg.len + sizeof(msg));
		xenstore_buf->rsp_cons += msg.len + sizeof(msg);

		wake_up(&req_info[msg.req_id].waitq);
	    }
	    spin_unlock_irqrestore(&xb_lock, flags);
	}
    }
}

static void xenbus_evtchn_handler(evtchn_port_t port, void *ign)
{
    wake_up(&xb_waitq);
}

static int nr_live_reqs;
static spinlock_t req_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_WAIT_QUEUE_HEAD(req_wq);

/* Release a xenbus identifier */
static void release_xenbus_id(int id)
{
    BUG_ON(!req_info[id].in_use);
    spin_lock(&req_lock);
    nr_live_reqs--;
    req_info[id].in_use = 0;
    if (nr_live_reqs == NR_REQS - 1 || nr_live_reqs == 0)
        wake_up(&req_wq);
    spin_unlock(&req_lock);
}

/* Allocate an identifier for a xenbus request.  Blocks if none are
   available. */
static int allocate_xenbus_id(void)
{
    static int probe;
    int o_probe;

    while (1) 
    {
        spin_lock(&req_lock);
        if (nr_live_reqs < NR_REQS)
            break;
        spin_unlock(&req_lock);
        wait_event(req_wq, (nr_live_reqs < NR_REQS) && !suspend);
    }

    o_probe = probe;
    for (;;) 
    {
        if (!req_info[o_probe].in_use)
            break;
        o_probe = (o_probe + 1) % NR_REQS;
        BUG_ON(o_probe == probe);
    }
    nr_live_reqs++;
    req_info[o_probe].in_use = 1;
    probe = (o_probe + 1) % NR_REQS;
    spin_unlock(&req_lock);
    init_waitqueue_head(&req_info[o_probe].waitq);

    return o_probe;
}

char* xenbus_printf(xenbus_transaction_t xbt,
                                  char* node, char* path,
                                  char* fmt, ...)
{
#define BUFFER_SIZE 256
    char fullpath[BUFFER_SIZE];
    char val[BUFFER_SIZE];
    va_list args;

    BUG_ON(strlen(node) + strlen(path) + 1 >= BUFFER_SIZE);
    sprintf(fullpath,"%s/%s", node, path);
    va_start(args, fmt);
    vsprintf(val, fmt, args);
    va_end(args);
    return xenbus_write(xbt,fullpath,val);

}

extern char xenstore_page[PAGE_SIZE];
static struct xenstore_domain_interface *map_xenstore_page(xen_pfn_t mfn)
{
    if ( HYPERVISOR_update_va_mapping(
		(unsigned long)xenstore_page, __pte( PFN_PHYS(mfn) | 7), UVMF_INVLPG) )
    {
	printk("Failed to map xenstore_page!!\n");
	crash_exit();
    }
    return (struct xenstore_domain_interface *)xenstore_page;

}

struct write_req {
    const void *data;
    unsigned len;
};


/* Send data to xenbus.  This can block.  All of the requests are seen
   by xenbus as if sent atomically.  The header is added
   automatically, using type %type, req_id %req_id, and trans_id
   %trans_id. */
static void xb_write(int type, int req_id, xenbus_transaction_t trans_id,
		     const struct write_req *req, int nr_reqs)
{
    XENSTORE_RING_IDX prod;
    int r;
    int len = 0;
    const struct write_req *cur_req;
    int req_off;
    int total_off;
    int this_chunk;
    struct xsd_sockmsg m = {.type = type, .req_id = req_id,
        .tx_id = trans_id };
    struct write_req header_req = { &m, sizeof(m) };

    for (r = 0; r < nr_reqs; r++)
        len += req[r].len;
    m.len = len;
    len += sizeof(m);

    cur_req = &header_req;

    BUG_ON(len > XENSTORE_RING_SIZE);
    /* Wait for the ring to drain to the point where we can send the
       message. */
    long flags;
    spin_lock_irqsave(&xb_lock, flags);
    prod = xenstore_buf->req_prod;
    if (prod + len - xenstore_buf->req_cons > XENSTORE_RING_SIZE) 
    {
	spin_unlock_irqrestore(&xb_lock, flags);
        /* Wait for there to be space on the ring */
        //xprintk("prod %d, len %d, cons %d, size %d; waiting.\n",
        //        prod, len, xenstore_buf->req_cons, XENSTORE_RING_SIZE);
        wait_event(xb_waitq,
                xenstore_buf->req_prod + len - xenstore_buf->req_cons <=
                XENSTORE_RING_SIZE);
	spin_lock_irqsave(&xb_lock, flags);
        DEBUG("Back from wait.\n");
        prod = xenstore_buf->req_prod;
    }

    /* We're now guaranteed to be able to send the message without
       overflowing the ring.  Do so. */
    total_off = 0;
    req_off = 0;
    while (total_off < len) 
    {
        this_chunk = min(cur_req->len - req_off,
                XENSTORE_RING_SIZE - MASK_XENSTORE_IDX(prod));
        memcpy((char *)xenstore_buf->req + MASK_XENSTORE_IDX(prod),
                (char *)cur_req->data + req_off, this_chunk);
        prod += this_chunk;
        req_off += this_chunk;
        total_off += this_chunk;
        if (req_off == cur_req->len) 
        {
            req_off = 0;
            if (cur_req == &header_req)
                cur_req = req;
            else
                cur_req++;
        }
    }

    DEBUG("Complete main loop of xb_write.\n");
    BUG_ON(req_off != 0);
    BUG_ON(total_off != len);
    BUG_ON(prod > xenstore_buf->req_cons + XENSTORE_RING_SIZE);

    /* Remote must see entire message before updating indexes */
    wmb();

    xenstore_buf->req_prod += len;
    spin_unlock_irqrestore(&xb_lock, flags);

    /* Send evtchn to notify remote */
    notify_remote_via_evtchn(start_info.store_evtchn);
}

/* Send a mesasge to xenbus, in the same fashion as xb_write, and
   block waiting for a reply.  The reply is malloced and should be
   freed by the caller. */
static struct xsd_sockmsg *
xenbus_msg_reply(int type,
		 xenbus_transaction_t trans,
		 struct write_req *io,
		 int nr_reqs)
{
    int id;
    DEFINE_WAIT(w);
    struct xsd_sockmsg *rep;
    id = allocate_xenbus_id();

    preempt_disable();
    add_waiter(w, req_info[id].waitq);

    xb_write(type, id, trans, io, nr_reqs);

    preempt_enable();
    schedule();

    rep = req_info[id].reply;
    BUG_ON(rep->req_id != id);
    release_xenbus_id(id);

    return rep;
}

static char *errmsg(struct xsd_sockmsg *rep)
{
    if (!rep)
	    return strdup("no reply");

    if (rep->type != XS_ERROR)
	    return NULL;

    char *res = malloc(rep->len + 1);
    memcpy(res, rep + 1, rep->len);
    res[rep->len] = 0;
    free(rep);
    return res;
}

/* Send a debug message to xenbus.  Can block. */
static void xenbus_debug_msg(const char *msg)
{
    int len = strlen(msg);
    struct write_req req[] = {
        { "print", sizeof("print") },
        { msg, len },
        { "", 1 }};
    struct xsd_sockmsg *reply;

    reply = xenbus_msg_reply(XS_DEBUG, 0, req, ARRAY_SIZE(req));
    DEBUG("Got a reply, type %d, id %d, len %d.\n",
            reply->type, reply->req_id, reply->len);
}

/* List the contents of a directory.  Returns a malloc()ed array of
   pointers to malloc()ed strings.  The array is NULL terminated.  May
   block. */
char *xenbus_ls(xenbus_transaction_t xbt, const char *pre, char ***contents)
{
    struct xsd_sockmsg *reply, *repmsg;
    struct write_req req[] = { { pre, strlen(pre)+1 } };
    int nr_elems, x, i;
    char **res;

    repmsg = xenbus_msg_reply(XS_DIRECTORY, xbt, req, ARRAY_SIZE(req));
    char *msg = errmsg(repmsg);
    if (msg) {
	*contents = NULL;
	return msg;
    }
    reply = repmsg + 1;
    for (x = nr_elems = 0; x < repmsg->len; x++)
        nr_elems += (((char *)reply)[x] == 0);
    res = malloc(sizeof(res[0]) * (nr_elems + 1));
    for (x = i = 0; i < nr_elems; i++) {
        int l = strlen((char *)reply + x);
        res[i] = malloc(l + 1);
        memcpy(res[i], (char *)reply + x, l + 1);
        x += l + 1;
    }
    res[i] = NULL;
    free(repmsg);
    *contents = res;
    return NULL;
}

char *guk_xenbus_read(xenbus_transaction_t xbt, const char *path, char **value)
{
    struct write_req req[] = { {path, strlen(path) + 1} };
    struct xsd_sockmsg *rep;
    char *res;
    rep = xenbus_msg_reply(XS_READ, xbt, req, ARRAY_SIZE(req));
    char *msg = errmsg(rep);
    if (msg) {
	*value = NULL;
	return msg;
    }
    res = malloc(rep->len + 1);
    memcpy(res, rep + 1, rep->len);
    res[rep->len] = 0;
    free(rep);
    *value = res;
    return NULL;
}

char *xenbus_write(xenbus_transaction_t xbt, const char *path, const char *value)
{
    struct write_req req[] = { 
	{path, strlen(path) + 1},
	{value, strlen(value) + 1},
    };
    struct xsd_sockmsg *rep;
    rep = xenbus_msg_reply(XS_WRITE, xbt, req, ARRAY_SIZE(req));
    char *msg = errmsg(rep);
    if (msg) return msg;
    free(rep);
    return NULL;
}

char* xenbus_watch_path(xenbus_transaction_t xbt,
                        char *path,
                        char *token)
{
    struct xsd_sockmsg *rep;
    struct xenbus_watch *watch;
    char *msg;
    struct write_req req[] = {
        {path,  strlen(path)  + 1},
        {token, strlen(token) + 1},
    };

    spin_lock(&watch_list_lock);
    /* We need to add a new watch to the list */
    BUG_ON(find_watch(token) != NULL);
    watch = xmalloc(struct xenbus_watch);
    watch->path = path;
    watch->token = token;
    watch->thread = NULL;
    watch->path_prod_idx = 0;
    watch->path_cons_idx = 0;
    INIT_LIST_HEAD(&watch->list);
    list_add(&watch->list, &watch_list);
    spin_unlock(&watch_list_lock);

    rep = xenbus_msg_reply(XS_WATCH, xbt, req, ARRAY_SIZE(req));
    msg = errmsg(rep);
    if(msg != NULL)
    {
        spin_lock(&watch_list_lock);
        /* If msg is NULL we need to free rep, as errmsg didn't do it */
        free(rep);
        watch = find_watch(token);
        BUG_ON(watch == NULL);
        list_del(&watch->list);
        free(watch);
        spin_unlock(&watch_list_lock);
    }

    return msg;
}

int xenbus_rm_watch(char *token)
{
    struct xenbus_watch *watch;
    struct xsd_sockmsg *rep;

    spin_lock(&watch_list_lock);
    watch = find_watch(token);
    if (watch) {
	struct write_req req[] = {
	    {watch->path, strlen(watch->path) + 1},
	    {watch->token, strlen(watch->token) + 1},
	};

	spin_unlock(&watch_list_lock);

	rep = xenbus_msg_reply(XS_UNWATCH, XBT_NIL, req, ARRAY_SIZE(req));
	spin_lock(&watch_list_lock);
	list_del(&watch->list);
	free(watch);
    }
    spin_unlock(&watch_list_lock);

    return 1;
}

char *xenbus_read_watch(char *token)
{
    struct xenbus_watch *watch;
    char *path;

    spin_lock(&watch_list_lock);
    watch = find_watch(token);
again:
    if (trace_xenbus()) tprintk("xenbus_read_watch: again %s %d %d\n", current->name, watch->path_cons_idx, watch->path_prod_idx);
    if(watch->path_cons_idx < watch->path_prod_idx)
    /* There are some paths to consume, no need to block */
    {
        path = watch->paths[watch->path_cons_idx++ % MAX_PATHS];
        watch->thread = NULL;
        spin_unlock(&watch_list_lock);
        return path;
    }

    if (trace_xenbus()) tprintk("xenbus_read_watch: blocking %s %d %d\n", current->name, watch->path_cons_idx, watch->path_prod_idx);
    /* Othewise block current */
    watch->thread = current;
    block(current);
    spin_unlock(&watch_list_lock);
    schedule();
    spin_lock(&watch_list_lock);
    goto again;
}

char* xenbus_wait_for_value(char* token, char *path, char* value)
{
    for(;;)
    {
        char *changed_path, *msg, *res;

        changed_path = xenbus_read_watch(token);
        if(strcmp(changed_path, path) == 0)
        {
            free(changed_path);
            msg = xenbus_read(XBT_NIL, path, &res);
            if(msg) return msg;
            if(strcmp(value, res) == 0)
            {
                free(res);
                return NULL;
            }
            else
                free(res);
        }
        else
            free(changed_path);
    }
    /* We should never get here */
    BUG();
    return NULL;
}


char *xenbus_rm(xenbus_transaction_t xbt, const char *path)
{
    struct write_req req[] = { {path, strlen(path) + 1} };
    struct xsd_sockmsg *rep;
    rep = xenbus_msg_reply(XS_RM, xbt, req, ARRAY_SIZE(req));
    char *msg = errmsg(rep);
    if (msg)
	return msg;
    free(rep);
    return NULL;
}

char *xenbus_get_perms(xenbus_transaction_t xbt, const char *path, char **value)
{
    struct write_req req[] = { {path, strlen(path) + 1} };
    struct xsd_sockmsg *rep;
    char *res;
    rep = xenbus_msg_reply(XS_GET_PERMS, xbt, req, ARRAY_SIZE(req));
    char *msg = errmsg(rep);
    if (msg) {
	*value = NULL;
	return msg;
    }
    res = malloc(rep->len + 1);
    memcpy(res, rep + 1, rep->len);
    res[rep->len] = 0;
    free(rep);
    *value = res;
    return NULL;
}

#define PERM_MAX_SIZE 32
char *xenbus_set_perms(xenbus_transaction_t xbt, const char *path, domid_t dom, char perm)
{
    char value[PERM_MAX_SIZE];
    snprintf(value, PERM_MAX_SIZE, "%c%hu", perm, dom);
    struct write_req req[] = { 
	{path, strlen(path) + 1},
	{value, strlen(value) + 1},
    };
    struct xsd_sockmsg *rep;
    rep = xenbus_msg_reply(XS_SET_PERMS, xbt, req, ARRAY_SIZE(req));
    char *msg = errmsg(rep);
    if (msg)
	return msg;
    free(rep);
    return NULL;
}

char *xenbus_transaction_start(xenbus_transaction_t *xbt)
{
    /* xenstored becomes angry if you send a length 0 message, so just
       shove a nul terminator on the end */
    struct write_req req = { "", 1};
    struct xsd_sockmsg *rep;
    char *err;

    rep = xenbus_msg_reply(XS_TRANSACTION_START, 0, &req, 1);
    err = errmsg(rep);
    if (err)
	return err;
    sscanf((char *)(rep + 1), "%u", xbt);
    free(rep);
    return NULL;
}

char *
xenbus_transaction_end(xenbus_transaction_t t, int abort, int *retry)
{
    struct xsd_sockmsg *rep;
    struct write_req req;
    char *err;

    *retry = 0;

    req.data = abort ? "F" : "T";
    req.len = 2;
    rep = xenbus_msg_reply(XS_TRANSACTION_END, t, &req, 1);
    err = errmsg(rep);
    if (err) {
	if (!strcmp(err, "EAGAIN")) {
	    *retry = 1;
	    free(err);
	    return NULL;
	} else {
	    return err;
	}
    }
    free(rep);
    return NULL;
}

int xenbus_read_integer(char *path)
{
    char *res, *buf;
    int t;

    res = xenbus_read(XBT_NIL, path, &buf);
    if (res) {
	DEBUG("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, res);
	free(res);
	return -1;
    }
    sscanf(buf, "%d", &t);
    free(buf);
    return t;
}

static void do_ls_test(const char *pre)
{
    char **dirs;
    int x;

    DEBUG("ls %s...\n", pre);
    char *msg = xenbus_ls(XBT_NIL, pre, &dirs);
    if (msg) {
	DEBUG("Error in xenbus ls: %s\n", msg);
	free(msg);
	return;
    }
    for (x = 0; dirs[x]; x++) 
    {
        DEBUG("ls %s[%d] -> %s\n", pre, x, dirs[x]);
        free(dirs[x]);
    }
    free(dirs);
}

static void do_read_test(const char *path)
{
    char *res;
    DEBUG("Read %s...\n", path);
    char *msg = xenbus_read(XBT_NIL, path, &res);
    if (msg) {
	DEBUG("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, msg);
	free(msg);
	return;
    }
    DEBUG("Read %s -> %s.\n", path, res);
    free(res);
}

static void do_write_test(const char *path, const char *val)
{
    DEBUG("Write %s to %s...\n", val, path);
    char *msg = xenbus_write(XBT_NIL, path, val);
    if (msg) {
	DEBUG("Result %s\n", msg);
	free(msg);
    } else {
	DEBUG("Success.\n");
    }
}

static void do_rm_test(const char *path)
{
    DEBUG("rm %s...\n", path);
    char *msg = xenbus_rm(XBT_NIL, path);
    if (msg) {
	DEBUG("Result %s\n", msg);
	free(msg);
    } else {
	DEBUG("Success.\n");
    }
}

/* Simple testing thing */
void test_xenbus(void)
{
    DEBUG("Doing xenbus test.\n");
    xenbus_debug_msg("Testing xenbus...\n");

    DEBUG("Doing ls test.\n");
    do_ls_test("device");
    do_ls_test("device/vif");
    do_ls_test("device/vif/0");

    DEBUG("Doing read test.\n");
    do_read_test("device/vif/0/mac");
    do_read_test("device/vif/0/backend");

    DEBUG("Doing write test.\n");
    do_write_test("device/vif/0/flibble", "flobble");
    do_read_test("device/vif/0/flibble");
    do_write_test("device/vif/0/flibble", "widget");
    do_read_test("device/vif/0/flibble");

    DEBUG("Doing rm test.\n");
    do_rm_test("device/vif/0/flibble");
    do_read_test("device/vif/0/flibble");
    DEBUG("(Should have said ENOENT)\n");
}

/* Initialise xenbus. */
void init_xenbus(void)
{
    int err;
    if (trace_xenbus()) tprintk("Initialising xenbus\n");
    DEBUG("init_xenbus called.\n");

    suspend = 0;
    xenstore_buf = map_xenstore_page(start_info.store_mfn);
    xenbus_thread = create_thread("xenstore", xenbus_thread_func, UKERNEL_FLAG, NULL);
    DEBUG("buf at %p.\n", xenstore_buf);
    err = bind_evtchn(start_info.store_evtchn,
                      ANY_CPU,
		      xenbus_evtchn_handler,
                      NULL);
    DEBUG("xenbus on irq %d\n", err);
}

void xenbus_suspend(void)
{
    suspend = 1;
    /* check for live requests */
    spin_lock(&req_lock);
    if (nr_live_reqs) {
	printk("Xenbus suspend: wait for %d live reqs\n", nr_live_reqs);
#if 0
	int i;
	struct list_head *tmp;
	for (i = 0; i < NR_REQS; ++i) {
	    if(req_info[i].in_use) {
		spin_lock(&req_info[i].waitq.lock);
		list_for_each(tmp, &req_info[i].waitq.thread_list) {
		    struct wait_queue *curr;

		    curr = list_entry(tmp, struct wait_queue, thread_list);
		    xprintk("%d thread %d %s waiting\n", i, curr->thread->id, curr->thread->name);

		}
		spin_unlock(&req_info[i].waitq.lock);
	    }
	}
#endif
	spin_unlock(&req_lock);
	wait_event(req_wq, (nr_live_reqs == 0));
	spin_lock(&req_lock);
    }

    init_completion(&resume_comp);
    wmb();
    unbind_evtchn(start_info.store_evtchn);
    wake(xenbus_thread);
    spin_unlock(&req_lock);
    wait_for_completion(&suspend_comp);
}


void xenbus_resume(void)
{
    struct write_req req[2];
    struct xsd_sockmsg *rep;
    struct list_head *list_element, *next;
    struct xenbus_watch *watch;
    char *msg;
    int err;

    xenstore_buf = map_xenstore_page(start_info.store_mfn);
    suspend = 0;
    wmb();
    complete_all(&resume_comp);

    DEBUG("buf at %p.\n", xenstore_buf);
    err = bind_evtchn(start_info.store_evtchn,
                      ANY_CPU,
		      xenbus_evtchn_handler,
                      NULL);

    spin_lock(&watch_list_lock);
    list_for_each_safe(list_element, next, &watch_list) {
        watch = list_entry(list_element, struct xenbus_watch, list);
	req[0].data = watch->path; req[0].len = strlen(watch->path) + 1;
	req[1].data = watch->token; req[1].len = strlen(watch->token) +1;

	spin_unlock(&watch_list_lock);
	rep = xenbus_msg_reply(XS_WATCH, XBT_NIL, req, ARRAY_SIZE(req));

	msg = errmsg(rep);
	if (msg != NULL) {
	    free(rep);
	    xprintk("error on XS_WATCH: %s\n", msg);
	}

	spin_lock(&watch_list_lock);
    }
    spin_unlock(&watch_list_lock);

    notify_remote_via_evtchn(start_info.store_evtchn);

    wake_up(&req_wq);
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * End:
 */
