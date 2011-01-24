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

/* Completion methods
 *
 * Author: Harald Roeck
 */

#include <guk/os.h>
#include <guk/hypervisor.h>
#include <guk/time.h>
#include <guk/mm.h>
#include <guk/xmalloc.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/smp.h>
#include <guk/events.h>
#include <guk/trace.h>
#include <guk/completion.h>
#include <guk/spinlock.h>

#include <list.h>
#include <lib.h>
#include <types.h>
/*
 * block current thread until comp is posted
 */
void guk_wait_for_completion(struct completion *comp)
{
    unsigned long flags;
    spin_lock_irqsave(&comp->wait.lock, flags);
    rmb();
    if(!comp->done) {
	DEFINE_WAIT(wait);
	add_wait_queue(&comp->wait, &wait);
	do {
	    block(current);
	    spin_unlock_irqrestore(&comp->wait.lock, flags);
	    schedule();
	    spin_lock_irqsave(&comp->wait.lock, flags);
	    rmb();
	} while(!comp->done);
	remove_wait_queue(&wait);
    }
    comp->done--;
    spin_unlock_irqrestore(&comp->wait.lock, flags);
}

/*
 * post completion comp; release all threads waiting on comp
 */
void guk_complete_all(struct completion *comp)
{
    unsigned long flags;
    spin_lock_irqsave(&comp->wait.lock, flags);
    comp->done = UINT_MAX/2;
    wmb();
    __wake_up(&comp->wait);
    spin_unlock_irqrestore(&comp->wait.lock, flags);
}

/*
 * post completion comp; release only the first thread waiting on comp
 */
void guk_complete(struct completion *comp)
{
    unsigned long flags;
    spin_lock_irqsave(&comp->wait.lock, flags);

    /*
     * instead of incrementing we set it to one here. i.e. the waiter only sees
     * the last notify. this is different to the linux version of complete
     */
    comp->done = 1;
    wmb();
    __wake_up(&comp->wait);
    spin_unlock_irqrestore(&comp->wait.lock, flags);

}

struct completion *guk_create_completion(void) {
    struct completion *comp = (struct completion *)xmalloc(struct completion);
    init_completion(comp);
    return comp;
}

void guk_delete_completion(struct completion *completion) {
    free(completion);
}

