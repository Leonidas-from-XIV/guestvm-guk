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

/* Completion methods
 *
 * Author: Harald Roeck, Sun Microsystems, Inc., summer intern 2008
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

