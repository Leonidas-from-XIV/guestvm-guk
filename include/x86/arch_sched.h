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

