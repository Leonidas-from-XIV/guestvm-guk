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
#define REQ_SUSPEND_THREADS         15

struct dbif_read_u64_request {
    unsigned long address;
};

struct dbif_write_u64_request {
    unsigned long address;
    uint64_t value;
};

struct dbif_readbytes_request {
    unsigned long address;
    uint16_t n;
};

struct dbif_writebytes_request {
    unsigned long address;
    uint16_t n;
};

struct dbif_gather_threads_request {
};

struct dbif_suspend_request {
    uint16_t thread_id;
};

struct dbif_resume_request {
    uint16_t thread_id;
};

struct dbif_get_regs_request {
    uint16_t thread_id;
};

struct dbif_single_step_request {
    uint16_t thread_id;
};

struct dbif_set_ip_request {
    uint16_t thread_id;
    unsigned long ip;
};

struct dbif_get_stack_request {
    uint16_t thread_id;
};

struct dbif_app_specific_request {
    uint64_t arg;
};

struct dbif_db_debug_request {
    int level;
};

struct dbif_db_signoff_request {
};

struct dbif_suspend_threads {
};


/* DB request */
struct dbif_request {
    uint8_t type;                 /* Type of the request                  */
    uint16_t id;                  /* Request ID, copied to the response   */
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
        struct dbif_suspend_threads       suspend_threads;
    } u;
};

typedef struct dbif_request dbif_request_t;

struct db_thread {
    int id;
    int flags;
    unsigned long stack;
    unsigned long stack_size;
};
struct db_regs {
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long rbp;
	unsigned long rbx;
 	unsigned long r11;
	unsigned long r10;	
	unsigned long r9;
	unsigned long r8;
	unsigned long rax;
	unsigned long rcx;
	unsigned long rdx;
	unsigned long rsi;
	unsigned long rdi;
	unsigned long rip;
	unsigned long rsp; 
};


#define DB_RSP_BUFFER 256
/* DB response */
struct dbif_response {
    uint16_t id;
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
