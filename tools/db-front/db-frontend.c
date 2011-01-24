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
 * Frontend for Guest VM microkernel debugging support
 *
 * Author: Grzegorz Milos
 *         Mick Jordan
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
//#define TRACE_REQ

#ifdef DUMP_TRACE
static FILE *trace_file = NULL;
#define TRACE(_f, _a...) \
    fprintf(trace_file, "db-frontend, func=%s, line=%d: " _f "\n", __FUNCTION__,  __LINE__, ## _a); \
    fflush(trace_file)
#else
#define TRACE(_f, _a...)    ((void)0)
#endif

#ifdef TRACE_REQ
void trace_request(struct dbif_request *req) {
  printf("request: id %d, ", req->type, req->id);
  switch (req->type) {
    case REQ_APP_SPECIFIC1:
      printf("APP_SPECIFIC1");
      break;
    case REQ_READBYTES:
      printf("READBYTES address %lx, n %d", req->u.readbytes.address, req->u.readbytes.n);
    break;

  default:
      printf("UNKNOWN");
  }
  printf("\n");
}
#define REQ(_req) \
  trace_request(_req);
#else
#define REQ(_f, _a...)    ((void)0)
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
    xen_rmb();
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

uint64_t db_read_u64(uint64_t address)
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

void db_write_u64(uint64_t address, uint64_t value)
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

uint16_t db_multibytebuffersize(void)
{
  return PAGE_SIZE;
}

uint16_t db_readbytes(uint64_t address, char *buffer, uint16_t n)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    int i;
    RING_IDX idx;

    TRACE("address: %llx, n: %d", address, n);
    req = get_request(&idx);
    req->type = REQ_READBYTES;
    /* Magic number */
    req->id = 13;
    req->u.readbytes.address = address;
    req->u.readbytes.n = n;

    REQ(req);
    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("rsp: %d", (uint16_t)rsp->ret_val);

    for (i = 0; i < rsp->ret_val; i++) {
      buffer[i] = data_page[i];
    }

    return rsp->ret_val;
}

uint16_t db_writebytes(uint64_t address, char *buffer, uint16_t n)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;
    int i;

    if (signed_off) return n;
    TRACE("address: %llx, n: %d", address, n);
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

struct db_thread* db_gather_threads(int *num)
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
      TRACE("id %d, flags %x, stack %llx, stacksize %llx", thread_data->id, thread_data->flags,
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

int db_suspend(uint16_t thread_id)
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

int db_resume(uint16_t thread_id)
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

int db_suspend_all(void) {
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("suspend_threads");
    req = get_request(&idx);
    req->type = REQ_SUSPEND_ALL;
    /* Magic number */
    req->id = 13;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return (int)rsp->ret_val;
}

int db_resume_all(void) {
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("resume_threads");
    req = get_request(&idx);
    req->type = REQ_RESUME_ALL;
    /* Magic number */
    req->id = 13;

    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return (int)rsp->ret_val;
}

int db_single_step(uint16_t thread_id)
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


struct db_regs* db_get_regs(uint16_t thread_id)
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

    TRACE("Regs: r15=%llx, "
                "r14=%llx, "
                "r13=%llx, "
                "r12=%llx, "
                "rbp=%llx, "
                "rbx=%llx, "
                "r11=%llx, "
                "r10=%llx, "
                "r9=%llx, "
                "r8=%llx, "
                "rax=%llx, "
                "rcx=%llx, "
                "rdx=%llx, "
                "rsi=%llx, "
                "rdi=%llx, "
                "rip=%llx, "
                "flags=%llx, "
                "rsp=%llx.",
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
    TRACE("FPRegs: xmm0=%llx, "
	          "xmm1=%llx, "
	          "xmm2=%llx, "
	          "xmm3=%llx, "
	          "xmm4=%llx, "
	          "xmm5=%llx, "
	          "xmm6=%llx, "
	          "xmm7=%llx, "
	          "xmm8=%llx, "
	          "xmm9=%llx, "
	          "xmm10=%llx, "
	          "xmm11=%llx, "
	          "xmm12=%llx, "
	          "xmm13=%llx, "
	          "xmm14=%llx, "
	          "xmm15=%llx.",
                  db_regs->xmm0,
                  db_regs->xmm1,
                  db_regs->xmm2,
                  db_regs->xmm3,
                  db_regs->xmm4,
                  db_regs->xmm5,
                  db_regs->xmm6,
                  db_regs->xmm7,
                  db_regs->xmm8,
                  db_regs->xmm9,
                  db_regs->xmm10,
                  db_regs->xmm11,
                  db_regs->xmm12,
                  db_regs->xmm13,
                  db_regs->xmm14,
                  db_regs->xmm15);

    return regs;
}

int db_set_ip(uint16_t thread_id, uint64_t ip)
{
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("thread_id: %d, ip=%llx", thread_id, ip);
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

int db_get_thread_stack(uint16_t thread_id,
                     uint64_t *stack_start,
                     uint64_t *stack_size)
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


uint64_t db_app_specific1(uint64_t arg)
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

    REQ(req);
    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return rsp->ret_val;
}

int db_activate_watchpoint(uint64_t address, uint64_t size, int kind) {
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("address %lx, size %ld, kind %x", address, size, kind);
    req = get_request(&idx);
    req->type = REQ_ACTIVATE_WP;
    /* Magic number */
    req->id = 13;
    req->u.watchpoint_request.address = address;
    req->u.watchpoint_request.size = size;
    req->u.watchpoint_request.kind = kind;
    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return rsp->ret_val;
}

int db_deactivate_watchpoint(uint64_t address, uint64_t size) {
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("address %lx, size %ld", address, size);
    req = get_request(&idx);
    req->type = REQ_DEACTIVATE_WP;
    /* Magic number */
    req->id = 13;
    req->u.watchpoint_request.address = address;
    req->u.watchpoint_request.size = size;
    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx", rsp->ret_val);

    return rsp->ret_val;
}

uint64_t db_watchpoint_info(uint16_t thread_id, int *kind) {
    struct dbif_request *req;
    struct dbif_response *rsp;
    RING_IDX idx;

    TRACE("watchpoint info");
    req = get_request(&idx);
    req->type = REQ_WP_INFO;
    req->u.watchpoint_info_request.thread_id = thread_id;
    /* Magic number */
    req->id = 13;
    commit_request(idx);
    rsp = get_response();
    assert(rsp->id == 13);
    TRACE("ret_val: %lx, %x", rsp->ret_val, (int)rsp->ret_val2);
    *kind = (int) rsp->ret_val2;
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
    int ss1 = sizeof(struct dbif_request);
    int ss2 = sizeof(struct dbif_response);
#ifdef DUMP_TRACE
    char buffer[256];

    sprintf(buffer, "/tmp/db-front-trace-%d", domain_id);
    trace_file = fopen(buffer, "w");
#endif
    signed_off = 0;
    dom_id = domain_id;
    /* Open the connection to XenStore first */
    xsh = xs_domain_open();
    assert(xsh != NULL);
    ret = xenbus_request_connection(dom_id, &gref, &evtchn, &dgref);
    //printf("db_attach ret=%d\n", ret);
    //assert(ret == 0);
    if (ret != 0) return ret;
    //printf("Connected to the debugging backend (gref=%d, evtchn=%d, dgref=%d).\n", gref, evtchn, dgref);

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
