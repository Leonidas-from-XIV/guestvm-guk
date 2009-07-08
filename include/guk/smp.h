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
/* SMP support
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 * Changes: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 *          Mick Jordan Sun Microsystems, Inc.
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
