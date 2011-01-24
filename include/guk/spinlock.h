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

#include <guk/arch_spinlock.h>

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
