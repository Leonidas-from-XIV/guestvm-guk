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
 * VM upcall interface
 *
 * Author: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 * Changes:  Mick Jordan, Sun Microsystems, Inc.
 *
 */

#ifndef VMCALLS_H
#define _APPSCHED_H_

/* Application scheduler upcalls:
 *
 *   Must be executable in interrupt context
 *
 *   Preemption is disabled
 *
 *   May only use spinlocks for synchronization
 */

struct thread;
/*
 * Reschedule the current application thread and return a thread that should
 * run next. Shall return NULL if no threads are available to run
 *
 */
typedef struct thread *(*sched_call)(int cpu);

/* Deschedule the current application thread.
 * Invoked if the current thread was an application thread and 
 * the microkernel will run a microkernel thread
 */
typedef void (*desched_call)(int cpu);

/*
 * Wake up a thread, i.e., make it runnable
 */
typedef int (*wake_call)(int id, int cpu);

/*
 * Block a thread , i.e. make it not runnable
 */
typedef int (*block_call)(int id, int cpu);

/* Attach a new application thread */
typedef int (*attach_call)(int id, int tcpu, int cpu);

/* Detach a dying thread */
typedef int (*detach_call)(int id, int cpu);

/* Pick an initial cpu for a new thread */
typedef int (*pick_cpu_call)(void);

/* Is there a runnable thread for cpu */
typedef int (*runnable_call)(int cpu);

/* register scheduler upcalls */
extern int guk_register_upcalls(
	sched_call sched_func,
	desched_call desched_func,
	wake_call wake_func,
	block_call block_func,
	attach_call attach_func,
	detach_call detach_func,
        pick_cpu_call pick_cpu_func,
	runnable_call runnable_func);

extern void guk_attach_to_appsched(struct thread *, uint16_t);
extern void guk_detach_from_appsched(struct thread *);

#endif /* _APPSCHED_H_ */
