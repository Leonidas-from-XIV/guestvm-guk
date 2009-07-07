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

#ifndef __ARCH_ASM_SPINLOCK_H
#define __ARCH_ASM_SPINLOCK_H

#define ARCH_SPIN_LOCK_UNLOCKED (spinlock_t) { 1 }

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define arch_spin_is_locked(x)	(*(volatile signed char *)(&(x)->slock) <= 0)
#define arch_spin_can_lock(lock) (!arch_spin_is_locked(lock)) 

#define cpu_relax_string    "rep;nop"
static inline void cpu_relax(void)
{
	__asm__ __volatile__(cpu_relax_string: : :"memory");
}

#ifdef CONFIG_SMP
#define LOCK "lock ; "
#else
#define LOCK ""
#endif

#define spin_lock_string \
        "1:\n" \
	LOCK \
	"decb %0\n\t" \
	"jns 3f\n" \
	"2:\t" \
    cpu_relax_string \
	"cmpb $0,%0\n\t" \
	"jle 2b\n\t" \
	"jmp 1b\n" \
	"3:\n\t"

/*
 * This works. Despite all the confusion.
 * (except on PPro SMP or if we are using OOSTORE)
 * (PPro errata 66, 92)
 */

#define spin_unlock_string \
	"xchgb %b0, %1" \
		:"=q" (oldval), "=m" (lock->slock) \
		:"0" (oldval) : "memory"

static inline void _raw_spin_unlock(spinlock_t *lock)
{
	char oldval = 1;
	__asm__ __volatile__(
		spin_unlock_string
	);
}

static inline int _raw_spin_trylock(spinlock_t *lock)
{
	char oldval;
	__asm__ __volatile__(
		"xchgb %b0,%1\n"
		:"=q" (oldval), "=m" (lock->slock)
		:"0" (0) : "memory");
	return oldval > 0;
}

static inline void _raw_spin_lock(spinlock_t *lock)
{
	__asm__ __volatile__(
		spin_lock_string
		:"=m" (lock->slock) : : "memory");
}

#endif
