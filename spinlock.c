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

#include <guk/spinlock.h>
#include <guk/os.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/xmalloc.h>

/*
 * This could be a long-held lock. We both prepare to spin for a long
 * time (making _this_ CPU preemptable if possible), and we also signal
 * towards that other CPU that it should break the lock ASAP.
 *
 * (We do this in a function because inlining it would be excessive.)
 */

#define SPIN_LOCK_MAX 100000000

#define BUILD_LOCK_OPS(op, locktype)					\
void guk_##op##_lock(locktype##_t *lock)			                \
{									\
	for (;;) {							\
		preempt_disable();					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		preempt_enable();					\
									\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!op##_can_lock(lock) && (lock)->break_lock)	\
                {                                                       \
                        (lock)->spin_count++;                           \
                        if ((lock)->spin_count > SPIN_LOCK_MAX) {       \
                          struct thread *t = current;                   \
                          xprintk("stuck spinlock %lx, thread %d\n", (lock), t->id); \
                          crash_exit_backtrace();                                    \
                        }                                               \
			cpu_relax();					\
                }                                                       \
	}								\
	(lock)->break_lock = 0;						\
        (lock)->spin_count = 0;                                         \
        current->lock_count++;                                      \
}									\
									\
									\
unsigned long guk_##op##_lock_irqsave(locktype##_t *lock)	\
{									\
	unsigned long flags;						\
									\
	for (;;) {							\
		preempt_disable();					\
		local_irq_save(flags);					\
		if (likely(_raw_##op##_trylock(lock)))			\
			break;						\
		local_irq_restore(flags);				\
		preempt_enable();					\
									\
		if (!(lock)->break_lock)				\
			(lock)->break_lock = 1;				\
		while (!op##_can_lock(lock) && (lock)->break_lock)	\
                {                                                       \
                        (lock)->spin_count++;                           \
                        if ((lock)->spin_count > SPIN_LOCK_MAX) {       \
                          struct thread *t = current;                   \
                          xprintk("stuck spinlock %ld, %lx, thread %d, owner %d\n", NOW(), (lock), t->id, (lock)->owner->id); \
                          xprintk("owner stack %lx\n", (lock)->owner->sp); \
			  dump_sp((unsigned long *)(lock)->owner->sp, xprintk); \
                          crash_exit_backtrace();                                    \
                        }                                               \
			cpu_relax();					\
                }                                                       \
	}								\
	(lock)->break_lock = 0;						\
        (lock)->spin_count = 0;                                         \
        (lock)->owner = current;                                       \
        current->lock_count++;                                          \
	return flags;							\
}									\

/*
 * Build preemption-friendly versions of the following
 * lock-spinning functions:
 *
 *         _[spin]_lock()
 *         _[spin]_lock_irqsave()
 */
BUILD_LOCK_OPS(spin, spinlock);

void guk_spin_unlock(spinlock_t *lock)
{
    current->lock_count--;
    _raw_spin_unlock(lock);
#ifdef DEBUG_LOCKS
    if (current->lock_count < 0) {
	xprintk("spin lock count is negative; unlocking a not locked spinlock?\n");
	BUG();
    }
#endif
    preempt_enable();
}

void guk_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
    current->lock_count--;
    _raw_spin_unlock(lock);
    local_irq_restore(flags);
#ifdef DEBUG_LOCKS
    if (current->lock_count < 0) {
	xprintk("spin lock count is negative; unlocking a not locked spinlock?\n");
	BUG();
    }
#endif
    preempt_enable();
}

spinlock_t *guk_create_spin_lock(void) {
    struct spinlock *lock = (struct spinlock *)xmalloc(struct spinlock);
    spin_lock_init(lock);
    return lock;
}

void guk_delete_spin_lock(spinlock_t *lock) {
    free(lock);
}


