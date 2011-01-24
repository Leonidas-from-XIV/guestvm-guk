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
/*
 * VM upcall interface
 *
 * Author: Harald Roeck
 * Changes:  Mick Jordan
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
