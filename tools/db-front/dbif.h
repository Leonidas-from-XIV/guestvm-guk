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
/******************************************************************************
 * 
 * This defines the packets that traverse the ring between the debug frontend and backend.
 * 
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 *         Mick Jordan, Sun Microsystems, Inc.
 */

#ifndef __DBIF_H__
#define __DBIF_H__

#include <xen/io/ring.h>

#define REQ_READ_U64                1 
#define REQ_WRITE_U64               2 
#define REQ_GATHER_THREAD           3 
#define REQ_SUSPEND_THREAD          4
#define REQ_RESUME_THREAD           5
#define REQ_SINGLE_STEP_THREAD      6
#define REQ_GET_REGS                7
#define REQ_SET_IP                  8
#define REQ_GET_THREAD_STACK        9
#define REQ_APP_SPECIFIC1           10
#define REQ_READBYTES               11 
#define REQ_WRITEBYTES              12
#define REQ_DB_DEBUG                13
#define REQ_SIGNOFF                 14
#define REQ_SUSPEND_ALL             15
#define REQ_RESUME_ALL              16
#define REQ_ACTIVATE_WP             17
#define REQ_DEACTIVATE_WP           18
#define REQ_WP_INFO                 19

struct dbif_read_u64_request {
    uint64_t address;
};

struct dbif_write_u64_request {
    uint64_t address;
    uint64_t value;
};

struct dbif_readbytes_request {
    uint64_t address;
    uint64_t n;
};

struct dbif_writebytes_request {
    uint64_t address;
    uint64_t n;
};

struct dbif_gather_threads_request {
};

struct dbif_suspend_request {
    uint16_t thread_id;
};

struct dbif_resume_request {
    uint64_t thread_id;
};

struct dbif_get_regs_request {
    uint64_t thread_id;
};

struct dbif_single_step_request {
    uint64_t thread_id;
};

struct dbif_set_ip_request {
    uint64_t thread_id;
    uint64_t ip;
};

struct dbif_get_stack_request {
    uint64_t thread_id;
};

struct dbif_app_specific_request {
    uint64_t arg;
};

struct dbif_db_debug_request {
    uint64_t level;
};

struct dbif_db_signoff_request {
};

struct dbif_suspend_resume_threads {
};

struct dbif_watchpoint_request {
    uint64_t address;
    uint64_t size;
    uint16_t kind;
    uint16_t pad1;
    uint32_t pad2;
};

struct dbif_watchpoint_info_request {
    uint64_t thread_id;
};

/* DB request */
struct dbif_request {
    uint16_t type;                 /* Type of the request                  */
    uint16_t id;                  /* Request ID, copied to the response   */
    uint32_t pad;
    union {
        struct dbif_read_u64_request      read_u64;
        struct dbif_write_u64_request     write_u64;
        struct dbif_readbytes_request     readbytes;
        struct dbif_writebytes_request    writebytes;
        struct dbif_gather_threads_request gather;
        struct dbif_suspend_request       suspend;
        struct dbif_resume_request        resume;
        struct dbif_single_step_request   step;
        struct dbif_get_regs_request      get_regs;
        struct dbif_set_ip_request        set_ip;
        struct dbif_get_stack_request     get_stack;
        struct dbif_app_specific_request  app_specific;
        struct dbif_db_debug_request      db_debug;
        struct dbif_db_signoff_request    db_signoff;
        struct dbif_suspend_resume_threads suspend_resume_threads;
	struct dbif_watchpoint_request    watchpoint_request;
	struct dbif_watchpoint_info_request    watchpoint_info_request;
    } u;
};

typedef struct dbif_request dbif_request_t;

struct db_thread {
    uint16_t id;
    uint16_t pad;
    uint32_t flags;
    uint64_t stack;
    uint64_t stack_size;
};

struct db_regs {
        uint64_t xmm0;
        uint64_t xmm1;
        uint64_t xmm2;
        uint64_t xmm3;
        uint64_t xmm4;
        uint64_t xmm5;
        uint64_t xmm6;
        uint64_t xmm7;
        uint64_t xmm8;
        uint64_t xmm9;
        uint64_t xmm10;
        uint64_t xmm11;
        uint64_t xmm12;
        uint64_t xmm13;
        uint64_t xmm14;
        uint64_t xmm15;
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbp;
	uint64_t rbx;
 	uint64_t r11;
	uint64_t r10;	
	uint64_t r9;
	uint64_t r8;
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rip;
        uint64_t flags;
	uint64_t rsp; 
};


#define DB_RSP_BUFFER 256
/* DB response */
struct dbif_response {
    uint16_t id;
    uint16_t pad1;
    uint32_t pad2;
    /* TODO: these should really be typed (i.e. define response structures
     * instead of using ret_val, ret_val2 only */
    uint64_t ret_val;
    uint64_t ret_val2;
    union {
        struct db_regs regs;
    } u;
};

typedef struct dbif_response dbif_response_t;


DEFINE_RING_TYPES(dbif, struct dbif_request, struct dbif_response);

#endif /* __XEN_PUBLIC_DBIF_H__ */
