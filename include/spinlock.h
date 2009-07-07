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
#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

/*
 * lock debugging:
 * count locks and unlocks of a thread; 
 * overhead: one branch to every unlock call
 */
#define DEBUG_LOCKS

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */
typedef struct spinlock {
	volatile unsigned int slock;
#if defined(CONFIG_PREEMPT) && defined(CONFIG_SMP)
	unsigned int break_lock;
        unsigned long spin_count;
        struct thread *owner;
#endif
} spinlock_t;

#include "arch_spinlock.h"

#define SPIN_LOCK_UNLOCKED ARCH_SPIN_LOCK_UNLOCKED

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define spin_is_locked(x)	arch_spin_is_locked(x)
#define spin_can_lock(x)	arch_spin_can_lock(x)

extern spinlock_t *guk_create_spin_lock(void);
extern void guk_delete_spin_lock(spinlock_t *lock);
extern void guk_spin_lock(spinlock_t *lock);
extern void guk_spin_unlock(spinlock_t *lock);
extern unsigned long guk_spin_lock_irqsave(spinlock_t *lock);
extern void guk_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);

#define spin_lock(lock)     guk_spin_lock(lock)
#define spin_unlock(lock)   guk_spin_unlock(lock)

#define spin_lock_irqsave(lock, flags)  flags = guk_spin_lock_irqsave(lock)
#define spin_unlock_irqrestore(lock, flags) guk_spin_unlock_irqrestore(lock, flags)

#define DEFINE_SPINLOCK(x) spinlock_t x = SPIN_LOCK_UNLOCKED

#endif /* _SPINLOCK_H_ */
