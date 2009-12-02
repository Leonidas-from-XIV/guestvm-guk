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
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: sched.c
 *      Author: Grzegorz Milos
 *     Changes: Robert Kaiser
 *     Changes: Harald Roeck, Sun Microsystems Inc., summer intern 2008.
 *
 *        Date: Aug 2005
 *
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: Arch specific part of scheduler
 *
 *
 ****************************************************************************
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <guk/os.h>
#include <guk/hypervisor.h>
#include <guk/time.h>
#include <guk/mm.h>
#include <guk/xmalloc.h>
#include <guk/sched.h>
#include <guk/smp.h>
#include <guk/trace.h>

#include <x86/arch_sched.h>
#include <maxine_ls.h>
#include <list.h>
#include <types.h>
#include <lib.h>

/*
 * print a backtrace of the native code; use base frame register to find the stack frames
 *
 * prints the address of the functions; to get to the code use comamnd line
 *  % gaddr2line -e guestvm <addr> 
 * this will print the filename and the line in the file the code address belongs to
 */
void guk_backtrace(void **bp, void *ip)
{
    int i;

    if(bp[1] < (void *)0x1000)
	return;

    if(ip == NULL)
	    ip = bp[1];

    xprintk("backtrace: \n");
    for (i=0; i < 16; ++i) {
	if (!bp || !ip)
	    break;
	xprintk("\t%016lx\n", ip);

	if ((void **)bp[0] < bp)
	    break;
	if(bp[1] < (void *)0x1000)
	    return;
	ip = bp[1];
	bp = (void **)bp[0];
    }

}

void dump_stack(struct thread *thread)
{
    unsigned long *bottom = (unsigned long *)(thread->stack + 2*4*1024);
    unsigned long *pointer = (unsigned long *)thread->sp;
    int count;
    if(thread == current)
    {
#ifdef __i386__
        asm("movl %%esp,%0"
            : "=r"(pointer));
#else
        asm("movq %%rsp,%0"
            : "=r"(pointer));
#endif
    }
    xprintk("The stack for \"%s\"\n", thread->name);
    for(count = 0; count < 25 && pointer < bottom; count ++)
    {
        xprintk("[0x%lx] 0x%lx\n", pointer, *pointer);
        pointer++;
    }

    if(pointer < bottom) xprintk(" ... continues.\n");
}

/* Gets run when a new thread is scheduled the first time ever,
   defined in x86_[32/64].S */
extern void thread_starter(void);
extern void idle_thread_starter(void);

/* Architecture specific setup of thread creation */
struct thread* arch_create_thread(char *name,
                                  void (*function)(void *),
                                  void *stack,
                                  unsigned long stack_size,
                                  void *data)
{
    struct thread *thread;

    thread = xmalloc(struct thread);
    /* Allocate 2^STACK_SIZE_PAGE_ORDER pages for stack,
     * stack will be aligned. Required for 'current' macro.
     * We also give the option to provide a stack allocated elsewhere */
    if(stack != NULL)
    {
        thread->stack = (char *)stack;
        thread->guk_stack_allocated = 0;
        thread->stack_size = stack_size;
    }
    else
    {
        thread->stack = (char *)alloc_pages(STACK_SIZE_PAGE_ORDER);
        thread->guk_stack_allocated = 1;
        thread->stack_size = STACK_SIZE;
    }
    thread->specific = NULL;
    thread->name = name;
    thread->sp = (unsigned long)thread->stack + thread->stack_size;

    /* push the local space onto the stack, this value will be put into register R14 
     * by the startup assembler code  */
    stack_push(thread, (unsigned long) get_local_space());
    stack_push(thread, (unsigned long) function);
    stack_push(thread, (unsigned long) data);
    if(function == idle_thread_fn)
        thread->ip = (unsigned long) idle_thread_starter;
    else
        thread->ip = (unsigned long) thread_starter;
    return thread;
}


struct thread initial_context;
/* Initialises a fake thread context for the initial boot. This sets up the
 * environment so that basic components don't need to be made aware that no
 * proper thread context is present. In particular this allows for the current
 * macro to work properly. Furthermore, preemption and spinlocks can be used in
 * the usual way too. */
void init_initial_context(void)
{
    initial_context.name = "boot-context";
    initial_context.stack = stack;
    initial_context.flags = 0;
    initial_context.cpu = 0;
    /* Disable preemption: we don't want calls to the scheduler in the initial
     * context because: a) scheduler isn't set up (no other threads present
     * either), b) initial_context is a fake thread */
    initial_context.preempt_count = 0x123;
    initial_context.resched_running_time = 0;
    INIT_LIST_HEAD(&initial_context.joiners);
    initial_context.id = -1;
    per_cpu(0, current_thread) = &initial_context;
}

void run_idle_thread(void)
{
    /* Switch stacks and run the thread */
#if defined(__i386__)
    __asm__ __volatile__("mov %0,%%esp\n\t"
                         "push %1\n\t"
                         "ret"
                         :"=m" (per_cpu(0, idle_thread)->sp)
                         :"m" (per_cpu(0, idle_thread)->ip));
#elif defined(__x86_64__)
    __asm__ __volatile__("mov %0,%%rsp\n\t"
                         "push %1\n\t"
                         "ret"
                         :"=m" (per_cpu(0, idle_thread)->sp)
                         :"m" (per_cpu(0, idle_thread)->ip));
#endif
}
