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
 * Frontend for Guest VM microkernel debugging support
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 *         Mick Jordan, Sun Microsystems, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <xenctrl.h>
#include <sys/mman.h>
#include <xs.h>
#include "db-frontend.h"

/* uncomment next line to build a version that traces all activity to a file in /tmp */
#define DUMP_TRACE

#ifdef DUMP_TRACE
static FILE *trace_file = NULL; 
#define TRACE(_f, _a...) \
    fprintf(trace_file, "db-frontend, func=%s, line=%d: " _f "\n", __FUNCTION__,  __LINE__, ## _a); \
    fflush(trace_file)
#else
#define TRACE(_f, _a...)    ((void)0)
#endif

struct xs_handle *xsh = NULL;
static int gnth = -1;
static int evth = -1;

domid_t dom_id;
static struct dbif_front_ring ring;
static evtchn_port_t evtchn, local_evtchn;
static char *data_page;

static int signed_off = 0;

struct dbif_request* get_request(RING_IDX *idx)
{
    assert(RING_FREE_REQUESTS(&ring) > 0); 
    *idx = ring.req_prod_pvt++;
    
    return RING_GET_REQUEST(&ring, *idx);
}

void commit_request(RING_IDX idx)
{
    int notify;

    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&ring, notify);
    if(notify)
        xc_evtchn_notify(evth, local_evtchn);
}

struct dbif_response *get_response(void)
{
    evtchn_port_t port;
    RING_IDX prod, cons;
    struct dbif_response *rsp;
    int more;
    
wait_again:    
    port = xc_evtchn_pending(evth);
    assert(port == local_evtchn);
    assert(xc_evtchn_unmask(evth, local_evtchn) >= 0);
    if(RING_HAS_UNCONSUMED_RESPONSES(&ring) <= 0)
        goto wait_again;
    
    prod = ring.sring->req_prod;
    rmb();
    cons = ring.rsp_cons;
    assert(prod = cons + 1);
    rsp = RING_GET_RESPONSE(&ring, cons);
    /* NOTE: we are not copying the response from the ring to a private
     * structure because we are completly single threaded, and the response will
     * never be overwritten before we complete dealing with it */
    ring.rsp_cons = prod;
    RING_FINAL_CHECK_FOR_RESPONSES(&ring, more);
    assert(more == 0);

    return rsp;
}

/******************** Implementation of ptrace like functions *****************/

uint64_t read_u64(unsigned long address)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("address: %lx", address);
    req = get_request(&idx);
    req->type = REQ_READ_U64;
    /* Magic number */
    req->id = 13;
    req->u.read_u64.address = address;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("rsp: %lx", rsp->ret_val);

    return rsp->ret_val;
}

void write_u64(unsigned long address, uint64_t value)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("address: %lx, value: %lx", address, value);
    req = get_request(&idx);
    req->type = REQ_WRITE_U64;
    /* Magic number */
    req->id = 13;
    req->u.write_u64.address = address;
    req->u.write_u64.value = value;

    commit_request(idx);

    rsp = get_response();
    assert(rsp->id == 13);
    assert(rsp->ret_val == 0);
    TRACE("rsp: %lx", rsp->ret_val);
}

uint16_t multibytebuffersize(void)
{
  return PAGE_SIZE;
}

uint16_t readbytes(unsigned long address, char *buffer, uint16_t n)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    int i;
    RING_IDX idx;

    TRACE("address: %lx, n: %d", address, n);
    req = get_request(&idx);
    req->type = REQ_READBYTES;
    /* Magic number */
    req->id = 13;
    req->u.readbytes.address = address;
    req->u.readbytes.n = n;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("rsp: %d", (uint16_t)rsp->ret_val);

    for (i = 0; i < rsp->ret_val; i++) {
      buffer[i] = data_page[i];
    }

    return rsp->ret_val;
}

uint16_t writebytes(unsigned long address, char *buffer, uint16_t n)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;
    int i;

    if (signed_off) return n;
    TRACE("address: %lx, n: %d", address, n);
    req = get_request(&idx);
    req->type = REQ_WRITEBYTES;
    /* Magic number */
    req->id = 13;
    req->u.writebytes.address = address;
    req->u.writebytes.n = n;
    for (i = 0; i < n; i++) {
      data_page[i] = buffer[i];
    }

    commit_request(idx);

    rsp = get_response();
    assert(rsp->id == 13);
    assert(rsp->ret_val == n);
    TRACE("rsp: %d", (uint16_t)rsp->ret_val);
    return n;
}

struct db_thread* gather_threads(int *num)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;
    int numThreads, i;
    struct db_thread *thread_data = (struct db_thread *)data_page;
    struct db_thread *threads;

    req = get_request(&idx);
    req->type = REQ_GATHER_THREAD;
    /* Magic number */
    req->id = 13;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    if (rsp->ret_val == -1) {
      TRACE("target domain terminated");
      if (num != NULL)
          *num = 0;
      return NULL;
    }
    numThreads = rsp->ret_val;
    TRACE("numThreads: %d", numThreads);
    threads = (struct db_thread *)malloc(numThreads * sizeof(struct db_thread));
    for (i = 0; i < numThreads; i++) {
      TRACE("id %d, flags %x, stack %lx, stacksize %lx", thread_data->id, thread_data->flags,
	    thread_data->stack, thread_data->stack_size);
      threads[i].id = thread_data->id;
      threads[i].flags = thread_data->flags;
      threads[i].stack = thread_data->stack;
      threads[i].stack_size = thread_data->stack_size;
      thread_data++;
    }
    if(num != NULL)
        *num = numThreads;
    return threads;
}

int suspend(uint16_t thread_id)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("thread_id: %d", thread_id);
    req = get_request(&idx);
    req->type = REQ_SUSPEND_THREAD;
    /* Magic number */
    req->id = 13;
    req->u.suspend.thread_id = thread_id;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return (int)rsp->ret_val;
}

int resume(uint16_t thread_id)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("thread_id: %d", thread_id);
    req = get_request(&idx);
    req->type = REQ_RESUME_THREAD;
    /* Magic number */
    req->id = 13;
    req->u.resume.thread_id = thread_id;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return (int)rsp->ret_val;
}

int suspend_threads(void) {
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("suspend_threads");
    req = get_request(&idx);
    req->type = REQ_SUSPEND_THREADS;
    /* Magic number */
    req->id = 13;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return (int)rsp->ret_val;
}

int single_step(uint16_t thread_id)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("thread_id: %d", thread_id);
    req = get_request(&idx);
    req->type = REQ_SINGLE_STEP_THREAD;
    /* Magic number */
    req->id = 13;
    req->u.suspend.thread_id = thread_id;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);
    
    return (int)rsp->ret_val;
}


struct db_regs* get_regs(uint16_t thread_id)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    struct db_regs *regs;
    struct db_regs *db_regs;
    RING_IDX idx;

    if (signed_off) return NULL;
    TRACE("thread_id: %d", thread_id);
    req = get_request(&idx);
    req->type = REQ_GET_REGS;
    /* Magic number */
    req->id = 13;
    req->u.get_regs.thread_id = thread_id;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);
    if((int)rsp->ret_val < 0)
        return NULL;

    regs = (struct db_regs*)malloc(sizeof(struct db_regs));
    db_regs = &rsp->u.regs;

    regs->xmm0 = db_regs->xmm0;
    regs->xmm1 = db_regs->xmm1;
    regs->xmm2 = db_regs->xmm2;
    regs->xmm3 = db_regs->xmm3;
    regs->xmm4 = db_regs->xmm4;
    regs->xmm5 = db_regs->xmm5;
    regs->xmm6 = db_regs->xmm6;
    regs->xmm7 = db_regs->xmm7;
    regs->xmm8 = db_regs->xmm8;
    regs->xmm9 = db_regs->xmm9;
    regs->xmm10 = db_regs->xmm10;
    regs->xmm11 = db_regs->xmm11;
    regs->xmm12 = db_regs->xmm12;
    regs->xmm13 = db_regs->xmm13;
    regs->xmm14 = db_regs->xmm14;
    regs->xmm15 = db_regs->xmm15;

    regs->r15 = db_regs->r15;
    regs->r14 = db_regs->r14;
    regs->r13 = db_regs->r13;
    regs->r12 = db_regs->r12;
    regs->rbp = db_regs->rbp;
    regs->rbx = db_regs->rbx;
    regs->r11 = db_regs->r11;
    regs->r10 = db_regs->r10;	
    regs->r9 = db_regs->r9;
    regs->r8 = db_regs->r8;
    regs->rax = db_regs->rax;
    regs->rcx = db_regs->rcx;
    regs->rdx = db_regs->rdx;
    regs->rsi = db_regs->rsi;
    regs->rdi = db_regs->rdi;
    regs->rip = db_regs->rip;
    regs->flags = db_regs->flags;
    regs->rsp = db_regs->rsp; 

    TRACE("Regs: r15=%lx, "
                "r14=%lx, "
                "r13=%lx, "
                "r12=%lx, "
                "rbp=%lx, "
                "rbx=%lx, "
                "r11=%lx, "
                "r10=%lx, "
                "r9=%lx, "
                "r8=%lx, "
                "rax=%lx, "
                "rcx=%lx, "
                "rdx=%lx, "
                "rsi=%lx, "
                "rdi=%lx, "
                "rip=%lx, "
                "flags=%lx, "
                "rsp=%lx.",
                 db_regs->r15, 
                 db_regs->r14,
                 db_regs->r13,
                 db_regs->r12,
                 db_regs->rbp,
                 db_regs->rbx,
                 db_regs->r11,
                 db_regs->r10,
                 db_regs->r9,
                 db_regs->r8,
                 db_regs->rax,
                 db_regs->rcx,
                 db_regs->rdx,
                 db_regs->rsi,
                 db_regs->rdi,
                 db_regs->rip,
                 db_regs->flags,
                 db_regs->rsp);

    return regs;
}

int set_ip(uint16_t thread_id, unsigned long ip)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("thread_id: %d, ip=%lx", thread_id, ip);
    req = get_request(&idx);
    req->type = REQ_SET_IP;
    /* Magic number */
    req->id = 13;
    req->u.set_ip.thread_id = thread_id;
    req->u.set_ip.ip = ip;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);

    TRACE("ret_val: %lx", rsp->ret_val);
    return (int)rsp->ret_val;
}

int get_thread_stack(uint16_t thread_id, 
                     unsigned long *stack_start,
                     unsigned long *stack_size)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("thread_id: %d", thread_id);
    req = get_request(&idx);
    req->type = REQ_GET_THREAD_STACK;
    /* Magic number */
    req->id = 13;
    req->u.get_stack.thread_id = thread_id;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx, ret_val2: %lx", rsp->ret_val, rsp->ret_val2);
    if(rsp->ret_val == (uint64_t)-1)
    {
        *stack_start = 0;
        *stack_size = 0;
        return 0;
    } else
    {
        *stack_start = rsp->ret_val;
        *stack_size = rsp->ret_val2;
        return 1;
    }

    return rsp->ret_val;
}


uint64_t app_specific1(uint64_t arg)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("arg: %lx", arg);
    req = get_request(&idx);
    req->type = REQ_APP_SPECIFIC1;
    /* Magic number */
    req->id = 13;
    req->u.app_specific.arg = arg;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return rsp->ret_val;
}

int db_debug(int level)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("level: %d", level);
    req = get_request(&idx);
    req->type = REQ_DB_DEBUG;
    /* Magic number */
    req->id = 13;
    req->u.db_debug.level = level;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return rsp->ret_val;
}

void db_signoff(void) {
    struct dbif_request *req;
    RING_IDX idx;

    TRACE("signoff");
    req = get_request(&idx);
    req->type = REQ_SIGNOFF;
    /* Magic number */
    req->id = 13;
    commit_request(idx);
    signed_off = 1;
    // no response
}


int db_attach(int domain_id)
{
    int ret;
    grant_ref_t gref, dgref;
    struct dbif_sring *sring;
#ifdef DUMP_TRACE
    char buffer[256];
    
    sprintf(buffer, "/tmp/db-front-trace-%d", domain_id);
    trace_file = fopen(buffer, "w");
#endif
    dom_id = domain_id;
    /* Open the connection to XenStore first */
    xsh = xs_domain_open();
    assert(xsh != NULL);
    
    ret = xenbus_request_connection(dom_id, &gref, &evtchn, &dgref);
    printf("db_attach ret=%d\n", ret);
    //assert(ret == 0);
    if (ret != 0) return ret;
    printf("Connected to the debugging backend (gref=%d, evtchn=%d, dgref=%d).\n",
            gref, evtchn, dgref);

    gnth = xc_gnttab_open();
    assert(gnth != -1);
    /* Map the page and create frontend ring */ 
    sring = xc_gnttab_map_grant_ref(gnth, dom_id, gref, PROT_READ | PROT_WRITE);
    FRONT_RING_INIT(&ring, sring, PAGE_SIZE);
    /* Map the data page */
    data_page =  xc_gnttab_map_grant_ref(gnth, dom_id, dgref, PROT_READ | PROT_WRITE);
    evth = xc_evtchn_open();
    assert(evth != -1);

    local_evtchn = xc_evtchn_bind_interdomain(evth, dom_id, evtchn);
    
     
    return ret;
}

int db_detach(void)
{
    int implemented = 0;

    printf("Detach is %simplemented.\n", (implemented ? "": "not "));
    assert(implemented);

    /* Close the connection to XenStore when we are finished */
    xc_evtchn_close(evth);
    xc_gnttab_close(gnth);
    xs_daemon_close(xsh);

    return 0;
}

