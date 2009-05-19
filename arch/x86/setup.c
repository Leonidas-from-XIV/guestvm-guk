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

#include <os.h>
#include <trace.h>
#include <sched.h>


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


