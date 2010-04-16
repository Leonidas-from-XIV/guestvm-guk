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
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2002-2003 - Keir Fraser - University of Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 * (C) 2006 - Robert Kaiser - FH Wiesbaden
 ****************************************************************************
 *
 *        File: time.c
 *      Author: Rolf Neugebauer and Keir Fraser
 *     Changes: Grzegorz Milos
 *              Mick Jordan, Sun Microsystems Inc.
 *
 * Description: Simple time and timer functions
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <guk/os.h>
#include <guk/traps.h>
#include <guk/hypervisor.h>
#include <guk/events.h>
#include <guk/time.h>
#include <guk/smp.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <guk/spinlock.h>
#include <x86/arch_sched.h>

#include <xen/vcpu.h>

#include <types.h>
#include <lib.h>
/************************************************************************
 * Time functions
 *************************************************************************/

static struct timespec shadow_ts;
static u32 shadow_ts_version;
static u64 suspend_time;
int suspended = 0;
s64 time_addend = 0;
DEFINE_SPINLOCK(wallclock_lock);

#define HANDLE_USEC_OVERFLOW(_tv)          \
    do {                                   \
        while ( (_tv)->tv_usec >= 1000000 ) \
        {                                  \
            (_tv)->tv_usec -= 1000000;      \
            (_tv)->tv_sec++;                \
        }                                  \
    } while ( 0 )

static inline int time_values_up_to_date(void)
{
    int cpu = smp_processor_id();
    struct vcpu_time_info   *src = &HYPERVISOR_shared_info->vcpu_info[cpu].time;
    struct shadow_time_info *shadow = &per_cpu(cpu, shadow_time);

	return (shadow->version == src->version);
}


/*
 * Scale a 64-bit delta by scaling and multiplying by a 32-bit fraction,
 * yielding a 64-bit result.
 */
static inline u64 scale_delta(u64 delta, u32 mul_frac, int shift)
{
	u64 product;
#ifdef __i386__
	u32 tmp1, tmp2;
#endif

	if ( shift < 0 )
		delta >>= -shift;
	else
		delta <<= shift;

#ifdef __i386__
	__asm__ (
		"mul  %5       ; "
		"mov  %4,%%eax ; "
		"mov  %%edx,%4 ; "
		"mul  %5       ; "
		"add  %4,%%eax ; "
		"xor  %5,%5    ; "
		"adc  %5,%%edx ; "
		: "=A" (product), "=r" (tmp1), "=r" (tmp2)
		: "a" ((u32)delta), "1" ((u32)(delta >> 32)), "2" (mul_frac) );
#else
	__asm__ (
		"mul %%rdx ; shrd $32,%%rdx,%%rax"
		: "=a" (product) : "0" (delta), "d" ((u64)mul_frac) );
#endif

	return product;
}


static unsigned long get_nsec_offset(void)
{
    u64 now, delta;
    struct shadow_time_info *shadow = &this_cpu(shadow_time);

    rdtscll(now);
    delta = now - shadow->tsc_timestamp;
    return scale_delta(delta, shadow->tsc_to_nsec_mul, shadow->tsc_shift);
}


static void get_time_values_from_xen(void)
{
    int cpu = smp_processor_id();
    struct vcpu_time_info    *src = &HYPERVISOR_shared_info->vcpu_info[cpu].time;
    struct shadow_time_info  *shadow = &per_cpu(cpu, shadow_time);

    do {
	shadow->version = src->version;
	rmb();
	shadow->tsc_timestamp     = src->tsc_timestamp;
	shadow->system_timestamp  = src->system_time;
	shadow->tsc_to_nsec_mul   = src->tsc_to_system_mul;
	shadow->tsc_shift         = src->tsc_shift;
	rmb();
    }
    while ((src->version & 1) | (shadow->version ^ src->version));

    shadow->tsc_to_usec_mul = shadow->tsc_to_nsec_mul / 1000;
}

/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
u64 guk_monotonic_clock(void)
{
    u64 time;
    u32 local_time_version;
    struct shadow_time_info  *shadow = &this_cpu(shadow_time);

    /* Disable preemption, otherwise we might have to starve updating to newer
     * time values from Xen */
    preempt_disable();
    do {
	local_time_version = shadow->version;
	rmb();
	time = shadow->system_timestamp + get_nsec_offset();
	if (!time_values_up_to_date())
	    get_time_values_from_xen();
	rmb();
    } while (local_time_version != shadow->version);
    preempt_enable();

    return time;
}

static void update_wallclock(void)
{
    shared_info_t *s = HYPERVISOR_shared_info;

    /* There is no need to use irq save with the spinlock, since this function
     * is only called from VIRQ_TIMER handler */
    spin_lock(&wallclock_lock);
    /* This should only be called from IRQ handler. However, there is an initial
     * call during initialisation, before interrupts are enabled. */
    BUG_ON(!in_irq() && smp_init_completed && !suspended);
    do {
	shadow_ts_version = s->wc_version;
	rmb();
	shadow_ts.ts_sec  = s->wc_sec;
	shadow_ts.ts_nsec = s->wc_nsec;
	rmb();
    }
    while ((s->wc_version & 1) | (shadow_ts_version ^ s->wc_version));
    spin_unlock(&wallclock_lock);
}

void guk_gettimeofday(struct timeval *tv)
{
    u64 nsec;
    unsigned long flags;

    /* IRQs need to be switched off to avoid races with timer interrupt */
    spin_lock_irqsave(&wallclock_lock, flags);
    nsec = monotonic_clock();
    nsec += shadow_ts.ts_nsec;

    BUG_ON(shadow_ts_version == 0);
    tv->tv_sec = shadow_ts.ts_sec;
    tv->tv_sec += NSEC_TO_SEC(nsec);
    tv->tv_usec = NSEC_TO_USEC(nsec % 1000000000UL);
    spin_unlock_irqrestore(&wallclock_lock, flags);
}

void set_timer_interrupt(u64 delta)
{
    /* Don't allow the delta to be too small */
    if(delta < MILLISECS(1))
        delta = MILLISECS(1);
    BUG_ON(HYPERVISOR_set_timer_op(monotonic_clock() + delta));
}


/* until is in system time not hypervisor time */
void block_domain(s_time_t until)
{
    /* The IRQs are disabled on entry, and need to be enabled on exit.
     * Xen enables default before returning from SCHEDOP_block */
    if(NOW() < until) {
        HYPERVISOR_set_timer_op(until - time_addend);
	this_cpu(cpu_state) = CPU_SLEEPING;
        HYPERVISOR_sched_op(SCHEDOP_block, 0);
	this_cpu(cpu_state) = CPU_UP;
    } else
        local_irq_enable();
    BUG_ON(irqs_disabled());
}

u64 guk_get_cpu_running_time(int cpu)
{
    vcpu_runstate_info_t runstate;

    if(!smp_init_completed)
        return 0ULL;
    BUG_ON(HYPERVISOR_vcpu_op(VCPUOP_get_runstate_info,
                              cpu,
                              &runstate));

    return runstate.time[RUNSTATE_running];
}

void check_need_resched(void)
{
    u64 resched_time, running_time;
    struct thread *current_thread = guk_not_idle_or_stepped();

    if(current_thread != NULL)
    {
        resched_time = current_thread->resched_running_time;
        if(resched_time == 0ULL)
        {
            tprintk("Resched running time is 0.\n");
            return;
        }
        running_time = get_running_time();
        if (running_time >= resched_time) {
	    set_need_resched(current_thread);
        }
        else
        {
            /* TODO: if we use an average pcpu usage for this vcpu into the
             * equation, we can do a better job at estimating when to interrupt
             * */
            set_timer_interrupt(resched_time - running_time);
        }
    }
}


/*
 * Updates system time.
 */
void timer_handler(evtchn_port_t ev, void *ign)
{
    //static int count;
    get_time_values_from_xen();
    update_wallclock();
    check_need_resched();
}

void init_time(void)
{
    if (trace_startup()) tprintk("Initialising timer interface\n");
    memset(&shadow_ts, 0, sizeof(struct timespec));
    time_addend = 0;
    shadow_ts_version = 0;
    update_wallclock();
}


void time_suspend(void)
{
    suspend_time = monotonic_clock();
    ++suspended;
}

void time_resume(void)
{
    long long delta;

    get_time_values_from_xen();
    update_wallclock();

    delta  = monotonic_clock() - suspend_time;

    if (delta < 0) {
      time_addend -= delta;
      if (trace_startup()) tprintk("time_addend changed to %ld\n", time_addend);
    }
    --suspended;
}

