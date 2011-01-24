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
/******************************************************************************
 * common.c
 * 
 * Common stuff special to x86 goes here.
 * 
 * Copyright (c) 2002-2003, K A Fraser & R Neugebauer
 * Copyright (c) 2005, Grzegorz Milos, Intel Research Cambridge
 * 
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
 *
 */

#include <guk/os.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <x86/arch_sched.h>


/*
 * Shared page for communicating with the hypervisor.
 * Events flags go here, for example.
 */
shared_info_t *HYPERVISOR_shared_info;

/*
 * This structure contains start-of-day info, such as pagetable base pointer,
 * address of the shared_info structure, and things like that.
 */
union start_info_union start_info_union;

start_info_t *xen_info;
/*
 * Just allocate the kernel stack here. SS:ESP is set up to point here
 * in head.S.
 */
char stack[STACK_SIZE]  __attribute__ ((__aligned__(STACK_SIZE)));;

/* Assembler interface fns in entry.S. */
void hypervisor_callback(void);
void failsafe_callback(void);

static
shared_info_t *map_shared_info(unsigned long pa)
{
	if ( HYPERVISOR_update_va_mapping(
		(unsigned long)shared_info, __pte(pa | 7), UVMF_INVLPG) )
	{
		xprintk("Failed to map shared_info!!\n");
		crash_exit();
	}
	return (shared_info_t *)shared_info;
}

void
arch_init(start_info_t *si)
{
    unsigned long sp;

    /* why are we not using the si directly? */

    xen_info = si;
    /* Copy the start_info struct to a globally-accessible area. */
    /* WARN: don't do printk before here, it uses information from
       shared_info. Use xprintk instead. */
    memcpy(&start_info, si, sizeof(*si));

    
    /* set up minimal memory infos */
    phys_to_machine_mapping = (unsigned long *)start_info.mfn_list;

    /* Grab the shared_info pointer and put it in a safe place. */
    HYPERVISOR_shared_info = map_shared_info(start_info.shared_info);

    /* Set up event and failsafe callback addresses. */
#ifdef __i386__
    HYPERVISOR_set_callbacks(
	    __KERNEL_CS, (unsigned long)hypervisor_callback,
	    __KERNEL_CS, (unsigned long)failsafe_callback);
    asm volatile("movl %%esp,%0" : "=r"(sp));
#else
    HYPERVISOR_set_callbacks(
	    (unsigned long)hypervisor_callback,
	    (unsigned long)failsafe_callback, 0);
    asm volatile("movq %%rsp,%0" : "=r"(sp));
#endif
    /* Check stack alignment */
    BUG_ON((((unsigned long)stack) & (STACK_SIZE - 1)) != 0);
    /* Check if current sp is in the range. */
    BUG_ON((sp < (unsigned long)stack) || 
	    (sp > (unsigned long)stack + STACK_SIZE));
}

void
arch_print_info(void)
{
	if (trace_startup()) tprintk("  stack:      %p-%p\n", stack, stack + STACK_SIZE);
}


