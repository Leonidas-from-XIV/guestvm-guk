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
 * Frontend interface for Guest VM microkernel debugging support.
 * This stands alone and, therefore, contains copies of declarations from sched.h,
 * as it is used on the client front-end side in the debugger.
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 *         Mick Jordan, Sun Microsystems, Inc.
 */
#ifndef __LIB_GUK_DB__
#define __LIB_GUK_DB__
#include <inttypes.h>

struct db_thread {
    int id;
    int flags;
    unsigned long stack;
    unsigned long stack_size;
};

#define RUNNABLE_FLAG           0x00000001     /* Thread can be run on a CPU */
#define RUNNING_FLAG            0x00000002     /* Thread is currently runnig */
#define RESCHED_FLAG            0x00000004     /* Scheduler should be called at
                                                  the first opportunity.
                                                  WARN: flags used explicitly in
                                                  x86_64.S, don't change */
#define DYING_FLAG              0x00000008     /* Thread scheduled to die */

#define REQ_DEBUG_SUSPEND_FLAG  0x00000010     /* Thread is to be put to sleep in
                                                  response to suspend request/
                                                  breakpoint */
#define STEPPING_FLAG           0x00000020     /* Thread is to be single stepped */
#define DEBUG_SUSPEND_FLAG      0x00000040     /* Thread was actually put to sleep
						  because of REQ_DEBUG_SUSPEND */
#define INTERRUPTED_FLAG        0x00000080     /* Thread was interrupted during last wait */

#define XENOS_FLAG              0x00000100     /* Thread is a XenOS thread */

#define JOIN_FLAG               0x00000200     /* Thread is waiting for joinee */
#define AUX1_FLAG               0x00000400     /* spare flags */
#define AUX2_FLAG               0x00000800
#define TRAPPED_FLAG            0x00001000
#define SLEEP_FLAG              0x00002000     /* sleeping */
#define JAVA_FLAG               0x00004000     /* is this a java thread */


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

int db_attach(int domain_id);
int db_detach(void);
uint64_t read_u64(unsigned long address);
void write_u64(unsigned long address, uint64_t value);
uint16_t readbytes(unsigned long address, char *buffer, uint16_t n);
uint16_t writebytes(unsigned long address, char *buffer, uint16_t n);
uint16_t multibytebuffersize(void);
struct db_thread* gather_threads(int *num);
int suspend(uint16_t thread_id);
int suspend_threads(void);
int resume(uint16_t thread_id);
int single_step(uint16_t thread_id);
struct minios_regs* get_regs(uint16_t thread_id);
int set_ip(uint16_t thread_id, unsigned long ip);
int get_thread_stack(uint16_t thread_id, 
                     unsigned long *stack_start,
                     unsigned long *stack_size);
uint64_t app_specific1(uint64_t arg);
int db_debug(int level);
void db_signoff(void);

#endif /* __LIB_GUK_DB__ */
