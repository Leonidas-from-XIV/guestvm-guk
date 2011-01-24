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
/* SMP support
 * Author: Grzegorz Milos
 * Changes: Harald Roeck
 *          Mick Jordan
 */

#ifndef _SMP_H_
#define _SMP_H_

#include <guk/os.h>
#include <guk/time.h>

struct cpu_private 
{
#if defined(__x86_64__)
    int           irqcount;       /* offset 0 (used in x86_64.S) */
    unsigned long irqstackptr;    /*        8 */
#endif
    struct thread *idle_thread;   
    struct thread *current_thread; /* offset 24 (for x86_64) */
    int    cpunumber;              /*        32 */
    int    upcall_count;
    struct shadow_time_info shadow_time;
    int    cpu_state;
    evtchn_port_t ipi_port;
    void *db_support;
};
/* per cpu private data */
extern struct cpu_private percpu[];

#define per_cpu(_cpu, _variable)   ((percpu[(_cpu)])._variable)
#define this_cpu(_variable)        per_cpu(smp_processor_id(), _variable)

extern int guk_smp_cpu_state(int cpu);

void init_smp(void);

/* bring down all CPUs except the first one 
 * always returns on CPU 0
 */
void smp_suspend(void);

/* bring up all other CPUs */
void smp_resume(void);

/* send an event to CPU cpu */
void smp_signal_cpu(int cpu);

/* check for suspend and resume events on this cpu and change cpu state
 * only called by the idle thread
 */
void smp_cpu_safe(int cpu);

/* number of active CPUs */
int smp_num_active(void);

#define ANY_CPU                    (-1)

/* possible cpu states */
#define CPU_DOWN        0 /* cpu is down and will not run threads */
#define CPU_UP          1 /* cpu is up and runs threads */
#define CPU_SUSPENDING  2 /* cpu is marked to go down at the next possible point */
#define CPU_RESUMING    3 /* cpu is marked to resume after a suspend */
#define CPU_SLEEPING    4 /* cpu is blocked in Xen because no threads are ready run */

#endif /* _SMP_H_ */
