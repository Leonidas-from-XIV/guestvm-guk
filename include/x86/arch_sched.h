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
#ifndef __ARCH_SCHED_H__
#define __ARCH_SCHED_H__

#include <guk/sched.h>
#include <x86/bug.h>

/* NOTE: when you change this, change the corresponding value in x86_[32/64].S
 * */
#define STACK_SIZE_PAGE_ORDER    2
#define STACK_SIZE               (PAGE_SIZE * (1 << STACK_SIZE_PAGE_ORDER))

#ifdef __i386__
#define arch_switch_threads(prev, next, last) do {                      \
    unsigned long esi,edi;                                              \
    __asm__ __volatile__("pushfl\n\t"                                   \
                         "pushl %%ebp\n\t"                              \
                         "movl %%esp,%0\n\t"         /* save ESP */     \
                         "movl %5,%%esp\n\t"        /* restore ESP */   \
                         "movl $1f,%1\n\t"          /* save EIP */      \
                         "pushl %6\n\t"             /* restore EIP */   \
                         "ret\n\t"                                      \
                         "1:\t"                                         \
                         "popl %%ebp\n\t"                               \
                         "popfl"                                        \
                         :"=m" (prev->sp),"=m" (prev->ip),              \
                          "=a" (last), "=S" (esi),"=D" (edi)            \
                         :"m" (next->sp),"m" (next->ip),                \
                          "2" (prev), "d" (next));                      \
} while (0)

#error "currently x86_32bit not suported: missing definition of current thread"

#elif __x86_64__
/* TODO - verify that the clobber list contains all callee saved regs */
/* asm complains about RCX, RDX, RBX */
#define CLOBBER_LIST  \
    ,"r8","r9","r10","r11","r12","r13","r14","r15"
#define arch_switch_threads(prev, next, last) do {                      \
    unsigned long rsi,rdi;                                              \
    __asm__ __volatile__("pushfq\n\t"                                   \
                         "pushq %%rbp\n\t"                              \
                         "movq %%rsp,%0\n\t"         /* save RSP */     \
                         "movq %5,%%rsp\n\t"        /* restore RSP */   \
                         "movq $1f,%1\n\t"          /* save RIP */      \
                         "pushq %6\n\t"             /* restore RIP */   \
                         "ret\n\t"                                      \
                         "1:\t"                                         \
                         "popq %%rbp\n\t"                               \
                         "popfq\n\t"                                    \
                         :"=m" (prev->sp),"=m" (prev->ip),              \
                          "=a"(last), "=S" (rsi),"=D" (rdi)             \
                         :"m" (next->sp),"m" (next->ip),                \
                          "2" (prev), "d" (next)                        \
                         :"memory", "cc" CLOBBER_LIST);                 \
} while (0)

#define current ({                                         \
        struct thread *_current;                           \
        asm volatile ("mov %%gs:24,%0" : "=r"(_current));  \
        _current;                                          \
        })


/*
 * restart idle thread on restore event after the guest was previously suspended
 */
static inline void restart_idle_thread(long cpu,
	unsigned long sp,
	unsigned long ip,
	void (*idle_thread_fn)(void *data))
{

    __asm__ __volatile__("mov %1,%%rsp\n\t"
	                 "push %3\n\t"
	                 "push %0\n\t"
	                 "push %2\n\t"
			 "ret"
	    : "=m" (cpu)
	    : "r" (sp), "r" (ip), "r" (idle_thread_fn)
	    );
}

#endif /* __x86_64__ */

#endif /* __ARCH_SCHED_H__ */

