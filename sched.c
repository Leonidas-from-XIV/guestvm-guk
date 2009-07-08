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
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: sched.c
 *      Author: Grzegorz Milos
 *     Changes: Robert Kaiser
 *     Changes: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 *              Harald Roeck, Sun Microsystems, Inc., summer intern 2008
 *               - split runqueue into multiple queues: ready, zombie, wait
 *               - make them static to this file
 *               - use of upcalls
 *              Mick Jordan, Sun Microsystems, Inc. - misc changes
 *
 *        Date: Aug 2005 onwards
 *
 * Environment: GuestVM microkernel - evolved from Xen Minimal OS (mini-os)
 * Description: simple scheduler for GuestVM microkernel
 *
 * The scheduler is preemptive, and schedules with a Round Robin algorithm.
 *
 ****************************************************************************
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
#include <guk/appsched.h>
#include <guk/db.h>
#include <guk/spinlock.h>

#include <maxine_ls.h>
#include <list.h>
#include <types.h>
#include <lib.h>

#ifdef SCHED_DEBUG
#define DEBUG(_f, _a...) \
    tprintk("GUK(file=sched.c, line=%d) " _f "\n", __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

#define APPSCHED_OPTION   "-XX:GUKAS"
#define TIMESLICE_OPTION   "-XX:GUKTS"
#define DEFAULT_TIMESLICE_MS 10
#define TRACE_CPU_OPTION "-XX:GUKCT"

/* 
 * The choice of cpu for a newly created thread is random and threads
 * may switch between cpus depending on which cpu happens to be executing schedule
 * when they are at the head of the ready queue.
 *
 * However, the application scheduler will typically enforce sticky CPUs.
 */

static sched_call scheduler_upcall = NULL;
static desched_call deschedule_upcall = NULL;
static wake_call wake_upcall = NULL;
static block_call block_upcall = NULL;
static attach_call attach_upcall = NULL;
static detach_call detach_upcall = NULL;
static pick_cpu_call pick_cpu_upcall = NULL;
static runnable_call runnable_upcall = NULL;

static int upcalls_active = 0;
static int scheduler_upcalls_allowed = 0;

/* default timeslice for a thread. Can be changed globally and per thread */
static long timeslice = MILLISECS(DEFAULT_TIMESLICE_MS);  // default

/*
 * This is an experimental option to reserve a CPU for executing diagnostic code
 * when the scheduler seems deadlocked, e.g. print the state of the threads.
 * It must be highest CPU number available to the domain and reduces the number
 of available CPUs for scheduling by 1.
 */
static int trace_cpu;
static int num_sched_cpus = 0;

/* takes into account possible tracing CPU */
int guk_sched_num_cpus(void) {
  if (num_sched_cpus > 0) return num_sched_cpus;
  num_sched_cpus= smp_num_active();
  if (trace_cpu > 0) {
    num_sched_cpus--;
  }
  return num_sched_cpus;
}

/*
 * For consistent when tracing, we do not simply allocate thread id's serially.
 * The first MAX_VIRT_CPUS ids, starting from 0, are reserved for the idle
 * thread for that CPU, whether present or not. The two id's MAX_VIRT_CPUS,
 * MAX_VIRT_CPUS+1 are reserved for the debug handler threads, whether present or not.
 * Additional threads are allocated ids serially from there.
 */
#define DEBUG_THREAD_ID MAX_VIRT_CPUS
static uint16_t debug_thread_id = DEBUG_THREAD_ID;
static uint16_t thread_id = DEBUG_THREAD_ID + 2;

struct thread *guk_current()
{
	return current;
}

/* Queues */

/*
 * threads ready to run are in the ready_queue
 */
static LIST_HEAD(ready_queue);
static DEFINE_SPINLOCK(ready_lock);

/*
 * sleep_queue objects are in the sleep_queue ordered by wake up time
 */
static LIST_HEAD(sleep_queue);
static DEFINE_SPINLOCK(sleep_lock);

/*
 * zombie threads ready to be collect
 */
static LIST_HEAD(zombie_queue);
static DEFINE_SPINLOCK(zombie_lock);

/*
 * keep all threads regardless of their state in this list
 */
DEFINE_SPINLOCK(thread_list_lock);
LIST_HEAD(thread_list);

struct thread *get_thread_by_id(uint16_t id)
{
    struct thread *thread;
    struct list_head *list_head;

    spin_lock(&thread_list_lock);
    list_for_each(list_head, &thread_list) {
        thread = list_entry(list_head, struct thread, thread_list);
        if(thread->id == id) {
            spin_unlock(&thread_list_lock);
            return thread;
        }
    }
    spin_unlock(&thread_list_lock);

    return NULL;
}

static void add_thread_list(struct thread *thread)
{
    spin_lock(&thread_list_lock);
    list_add_tail(&thread->thread_list, &thread_list);
    spin_unlock(&thread_list_lock);
}

static void del_thread_list(struct thread *thread)
{
    spin_lock(&thread_list_lock);
    list_del(&thread->thread_list);
    spin_unlock(&thread_list_lock);
}

void print_runqueue_specific(int all, printk_function_ptr printk_function)
{
    struct list_head *it;
    struct thread *th;
    long flags;
    printk_function("%ld: ready_queue %lx, ready_lock %lx\n", NOW(), &ready_queue, &ready_lock);

    th = current;
    printk_function("   current \"%s\", id=%d, flags %x\n", th->name, th->id, th->flags);
    spin_lock_irqsave(&ready_lock, flags);
    list_for_each(it, &ready_queue)
    {
        th = list_entry(it, struct thread, ready_list);
        if (all || !is_ukernel(th)) {
            printk_function("   Thread \"%s\", id=%d, flags %x, cpu %d\n", th->name, th->id, th->flags, th->cpu);
        }
    }
    printk_function("\n");
    spin_unlock_irqrestore(&ready_lock, flags);

    print_sleep_queue_specific(all, printk_function);

    printk_function("all threads in the system:\n");
    int i = 0;
    spin_lock(&thread_list_lock);
    list_for_each(it, &thread_list)
    {
        th = list_entry(it, struct thread, thread_list);
        if (all || !is_ukernel(th)) {
            printk_function("%d\tThread \"%s\", id=%d, flags %x preempt %d cpu %d\n",
		    ++i, th->name, th->id, th->flags, th->preempt_count, th->cpu);
        }
    }
    spin_unlock(&thread_list_lock);
}

void print_sleep_queue_specific(int ukernel, printk_function_ptr printk_function) {
    struct list_head *it;
    struct thread *th;
    struct sleep_queue *sq;
    long flags;
    printk_function("%ld: sleep_queue %lx, sleep_queue_lock %lx\n", NOW(), &sleep_queue, &sleep_lock);
    spin_lock_irqsave(&sleep_lock, flags);
    list_for_each(it, &sleep_queue)
    {
        sq = list_entry(it, struct sleep_queue, list);
	th = sq->thread;
        if (ukernel || !is_ukernel(th)) {
            printk_function("   Thread \"%s\", id=%d, flags %x, wakeup %ld, \n", th->name, th->id, th->flags, sq->wakeup_time);
//	    backtrace(*(void **)th->sp, 0);
        }
    }
    printk_function("\n");
    spin_unlock_irqrestore(&sleep_lock, flags);

}

void guk_print_runqueue(void)
{
    print_runqueue_specific(1, tprintk);
}

int guk_current_id(void) {
    struct thread *thread = current;
    return thread != NULL ? thread->id : -1;
}

u32 get_flags(struct thread *thread)
{
    return thread->flags;
}

int guk_register_upcalls(sched_call sched_func, desched_call desched_func, wake_call wake_func,
		     block_call block_func, attach_call attach_func, detach_call detach_func,
		     pick_cpu_call pick_cpu_func, runnable_call runnable_func)
{

    if (scheduler_upcalls_allowed && !upcalls_active) {
	scheduler_upcall = sched_func;
	deschedule_upcall = desched_func;
	wake_upcall = wake_func;
	block_upcall = block_func;
	attach_upcall = attach_func;
	detach_upcall = detach_func;
	pick_cpu_upcall = pick_cpu_func;
	runnable_upcall = runnable_func;

	upcalls_active = 1;
	return 0;
    }
    return 1;
}

void guk_attach_to_appsched(struct thread *thread, uint16_t id)
{
    if (upcalls_active) {
        preempt_disable();
        block(thread); /* remove thread from microkernel runqueue */
	set_appsched(thread);
	set_runnable(thread);
	thread->appsched_id = id;
        if (trace_sched()) ttprintk("UA %d\n", thread->id);
	attach_upcall(thread->appsched_id, thread->cpu, smp_processor_id());
	preempt_enable();
    }
}

void guk_detach_from_appsched(struct thread *thread)
{
    if (upcalls_active) {
	preempt_disable();
	detach_upcall(thread->appsched_id, smp_processor_id());
	clear_appsched(thread);
	thread->appsched_id = -1;
	clear_runnable(thread);
	wake(thread); /* add thread to microkernel runqueue */
	preempt_enable();
    }
}

#define MAX_SLEEP 10
/* Find the time when the next timeout expires for the give CPU.
 * For Java threads, we only check those on this CPU.
 * 10s if no thread's waiting to be woken up. 
 */
s_time_t blocking_time(int cpu)
{
    s_time_t wakeup_time;
    s_time_t orig;
    struct sleep_queue *sq;
    long flags;

    orig = NOW();
    wakeup_time = NOW() + SECONDS(MAX_SLEEP);

    /* sleep queue needs to be protected */
    spin_lock_irqsave(&sleep_lock, flags);
    if (!list_empty(&sleep_queue)) {
	sq = list_entry(sleep_queue.next, struct sleep_queue, list);
	if (is_ukernel(sq->thread) || sq->thread->cpu == cpu) {
	  wakeup_time = sq->wakeup_time;
	}
    }
    spin_unlock_irqrestore(&sleep_lock, flags);
    if (wakeup_time < orig) {
      // some thread timer has expired
      if (trace_sched()) ttprintk("BU %d %ld %ld\n", cpu, orig, wakeup_time);
      wakeup_time = -1; // this will cause idle thread to re-invoke scheduler
    }
    return wakeup_time;
}


/* Notify given CPU that a new thread is created (the CPUs might be
 * currently sleeping, IPI will wake them up, so that they can start running
 * the new thread */
void guk_kick_cpu(int cpu) {
    if(cpu >= 0 && smp_init_completed) {
        if (trace_sched()) ttprintk("KC %d\n", cpu);
	smp_signal_cpu(cpu);
    }
}

/* Wake up all threads with expired timeouts. */
static void wake_expired(void)
{
    struct list_head *iterator;
    struct thread *thread;
    struct sleep_queue *sq;
    long flags;
    s_time_t now = NOW();

    spin_lock_irqsave(&sleep_lock, flags);
    /* Thread list needs to be protected */
    list_for_each(iterator, &sleep_queue)
    {

	sq = list_entry(iterator, struct sleep_queue, list);

	if (is_expired(sq))
	    continue;

	if(sq->wakeup_time <= now) {
	    thread = sq->thread;
	    if (trace_sched()) {
	      ttprintk("WE %d %ld\n", thread->id, sq->wakeup_time);
	    }
	    set_expired(sq);
	    wake(thread);
	} else {
	    /* sleep queue is ordered */
	    break;
	}
    }
    spin_unlock_irqrestore(&sleep_lock, flags);
}

/*
 * block current thread until joinee is exits
 *    returns -1 if interrupted
 */
int guk_join_thread(struct thread *joinee)
{
    struct thread *this_thread = current;

    /* syncronize agains exit_thread and reap_dead */
    spin_lock(&zombie_lock);
    if (!is_dying(this_thread)) {
        this_thread->regs = NULL;
	block(this_thread);
	set_joining(this_thread);
	list_add_tail(&this_thread->ready_list, &joinee->joiners);
	spin_unlock(&zombie_lock);
	schedule();
	if (is_interrupted(this_thread)) {
	    clear_interrupted(this_thread);
	    return -1;
	}
    } else {
	spin_unlock(&zombie_lock);
    }

    return 0;
}

/*
 * wake up all threads waiting for joinee to exit
 *
 * caller should hold zombie_lock
 */
static void wake_joiners(struct thread *joinee)
{
    struct list_head *iterator, *tmp;
    struct thread *thread;

    list_for_each_safe(iterator, tmp, &joinee->joiners)
    {
        thread = list_entry(iterator, struct thread, ready_list);
	clear_joining(thread);
	list_del(&thread->ready_list);
	wake(thread);
    }
}

__attribute__((weak)) void guk_free_thread_stack(void *specific, void *stack, unsigned long stack_size) {
    crash_exit_msg("dummy free_thread_stack called!");
}

/*
 * collect zombie threads on this cpu
 */
static void reap_dead(void)
{
    struct list_head *iterator, *tmp;
    struct thread *thread;

    spin_lock(&zombie_lock);
    list_for_each_safe(iterator, tmp, &zombie_queue) {
        thread = list_entry(iterator, struct thread, ready_list);
        /* Don't reap running thread, don't reap on wrong CPU */
        if(unlikely(is_dying(thread))) {
	    if(!is_running(thread) && (thread->cpu == smp_processor_id())) {
		if (trace_sched() || trace_mm())
		    ttprintk("DT %d\n", thread->id);
		list_del(&thread->ready_list);

		if(!list_empty(&thread->joiners))
		    wake_joiners(thread);

		if (thread->guk_stack_allocated)
		    free_pages(thread->stack, STACK_SIZE_PAGE_ORDER);
		else {
		    guk_free_thread_stack(thread->specific, thread->stack, thread->stack_size);
		}

		del_thread_list(thread);
		free(thread);
	    }
	}
    }
    spin_unlock(&zombie_lock);
}

void switch_thread_in(struct thread *prev)
{
    clear_running(prev);
    local_irq_enable();
}

/*
 * return thread to run next
 *  - upcalls into the VM if upcalls are active and registered
 */
static inline struct thread *pick_thread(struct thread *prev, int cpu)
{
    struct thread *next, *thread;
    struct list_head *t;
    long flags;

    next = NULL;
    spin_lock_irqsave(&ready_lock, flags);
    // trace_ready_lock(1);
    /* If we are scheduling all threads (i.e. upcalls not active)
     * or current is not a Java thread, then unless current is the idle
     * thread, move from the front to the back of the ready list.
     */
    if(!upcalls_active || !is_appsched(prev)) {
	if(is_runnable(prev) && prev != this_cpu(idle_thread)) {
	    list_del(&prev->ready_list);
	    list_add_tail(&prev->ready_list, &ready_queue);
	}
    }

    /* If there is a runnable thread that is not running on some other
     * CPU, then that is the next to run.
     */
    list_for_each(t, &ready_queue) {
	thread = list_entry(t, struct thread, ready_list);
	if(is_runnable(thread)
		&& !(is_running(thread) && thread->cpu != cpu)) {
	  /* if Java thread we never switch threads between cpus (here) */
	  if (!upcalls_active || is_ukernel(thread) || thread->cpu == cpu) {
	    next = thread;
	    break;
	  }
	}
    }
    if (next != NULL) {
	if (is_running(next) && next->cpu != cpu) {
	    xprintk("ESR %d %s\n", next->id);
	    BUG();
	}
	set_running(next);
	spin_unlock_irqrestore(&ready_lock, flags);
	// trace_ready_lock(0);

	if (upcalls_active && is_appsched(prev))
	    /* This the (unusual) case of a Java thread being pre-empted by a ukernel thread */
	    deschedule_upcall(cpu);
	return next;
    }
    spin_unlock_irqrestore(&ready_lock, flags);
    // trace_ready_lock(0);

    /* No ukernel threads to run, so if upcalls active, try the Java scheduler. */

    if (upcalls_active) {
	next = scheduler_upcall(cpu);
    }

    /* If no other runnable thread is found run Idle */
    if (next == NULL)
	next = this_cpu(idle_thread);
    else {
	if (is_running(next) && next->cpu != cpu) {
	    xprintk("ESR %d \n", next->id);
	    BUG();
	}
    }
    set_running(next);
    return next;
}

/*
 * main scheduler function
 */
void guk_schedule(void)
{
    struct thread *prev, *next;
    int cpu;

    BUG_ON(in_irq());
    BUG_ON(irqs_disabled());

    /* the previous thread is the currently running thread */
    prev = current;

    BUG_ON(in_spinlock(prev));
    BUG_ON(!is_preemptible(prev));

    s_time_t start = NOW();
    /* Check if there are any threads that need to be woken up.
     * The overhead of checking it every schedule could be avoided if
     * we check it from the timer interrupt handler */
    wake_expired();
    preempt_disable();

    cpu = prev->cpu;

    /* schedule the idle thread, while the cpu is down */
    if(per_cpu(cpu, cpu_state) != CPU_UP)
	next = per_cpu(cpu, idle_thread);
    else {
	/* find next thread to run; will look for java threads too */
	next = pick_thread(prev, cpu);
	if (!is_runnable(next) && next != per_cpu(cpu, idle_thread)) {
            xprintk("ESNR %d %x %d %x\n", next->id, next->flags, prev->id, prev->flags);
	    BUG();
	}
    }
    /* Cannot schedule dying thread */
    BUG_ON(is_dying(next));

    if (next->cpu != cpu) next->cpu = cpu;
    if(!is_stepped(prev))
        clear_need_resched(prev);

    next->resched_running_time = get_running_time() + next->timeslice;
    set_timer_interrupt(timeslice);

    /* Interrupting the switch is equivalent to having the next thread
       interrupted at the return instruction. And therefore at safe point. */
    if(prev != next) {
      if (trace_sched()) {
        ttprintk("TS %d %d %ld %ld\n",
                prev->id, next->id, next->resched_running_time, start);
      }
        /* Setting current_thread in the cpu private structure needs to be
         * atomic with respect to interrupts */
        BUG_ON(irqs_disabled());
        local_irq_disable();
        this_cpu(current_thread) = next;
	if (!is_ukernel(prev)) {
	  struct fp_regs *fpregs = prev->fpregs;
	  asm (save_fp_regs_asm : : [fpr] "r" (fpregs));	  
	}
	if (!is_ukernel(next)) {
	  struct fp_regs *fpregs = next->fpregs;
	  asm (restore_fp_regs_asm : : [fpr] "r" (fpregs));	  
	}
        switch_threads(prev, next, prev);

        /* We running on the new thread's stack now.
         * Be careful about using local variables here.
         * NOTE: switch_thread_in is also called by thread_starter when a new
         * thread gets to run the first time. Make sure you don't put any code
         * here that should be executed by new threads as well. Put it in
         * switch_thread_in instead.
	 *
	 * enables interrupts
         */
        switch_thread_in(prev);
    } else {
      if (trace_sched()) {
        ttprintk("TS %d %d %ld %ld\n",
                prev->id, next->id, next->resched_running_time, start);
      }
    }

    preempt_enable();
}

/*
 * this is the entry point to schedule() from preemption off of preempt_enable
 */
void preempt_schedule(void)
{
    struct thread *ti = current;
    /*
     * If there is a non-zero preempt_count or interrupts are disabled,
     * we do not want to preempt the current task.  Just return..
     */
    if (unlikely(ti->preempt_count || irqs_disabled()))
	return;

need_resched:
    BUG_ON(!need_resched(ti));
    /* the regs is not valid anymore, so point it to zero */
    ti->regs = NULL;

    add_preempt_count(current, PREEMPT_ACTIVE);
    schedule();
    sub_preempt_count(current, PREEMPT_ACTIVE);

    BUG_ON(ti->preempt_count != 0);
    BUG_ON(ti->regs != NULL);
    /* If the thread is being single stepped, we should block and reschedule:
     * this alows the controler to gain control over thread's execution */
    if(is_req_debug_suspend(ti)) {
	clear_need_resched(ti);
	clear_req_debug_suspend(ti);
	set_debug_suspend(ti);
	block(ti);
	schedule();
	if(is_stepped(ti))
	    asm volatile ("pushfq; popq %%rax; bts $8,%%rax; pushq %%rax; popfq": : : "rax");
    }

    /* we could miss a preemption opportunity between schedule and now */
    barrier();
    if (unlikely(need_resched(ti)))
	goto need_resched;
}

/*
 * this is the entry point to schedule() from preemption off of irq context.
 * Note, that this is called and return with irqs disabled. This will
 * protect us against recursive calling from irq.
 */
void preempt_schedule_irq(void)
{
    struct thread *ti = current;
    if (trace_sched()) {
	ttprintk("PSI %d\n", ti->id);
    }

    /* Catch callers which need to be fixed */
    BUG_ON(ti->preempt_count || !irqs_disabled());

need_resched:
    add_preempt_count(current, PREEMPT_ACTIVE);
    local_irq_enable();
    schedule();
    local_irq_disable();
    sub_preempt_count(current, PREEMPT_ACTIVE);

    BUG_ON(ti->preempt_count != 0);
    /* If the thread is being single stepped, we should block and reschedule:
     * this allows the controller to gain control over thread's execution */
    if(is_req_debug_suspend(ti))
    {
	clear_need_resched(ti);
	clear_req_debug_suspend(ti);
	set_debug_suspend(ti);
	block(ti);
	local_irq_enable();
	schedule();
	local_irq_disable();
    }
    /* we could miss a preemption opportunity between schedule and now */
    barrier();
    if (unlikely(need_resched(ti))) {
        if (trace_sched()) ttprintk("PSI2 %d\n", ti->id);
	goto need_resched;
    }
}
/* Basic thread creation method */
static struct thread* create_thread_with_id_stack(char *name,
                                        void (*function)(void *),
					int flags,
                                        void *stack,
                                        unsigned long stack_size,
                                        void *data,
					uint16_t id)
{
    struct thread *thread;

    /* Call architecture specific setup. */
    thread = arch_create_thread(name, function, stack, stack_size, data);
    /* Not runnable, not exited, not sleeping, maybe ukernel thread */
    thread->flags = flags;
    thread->regs = NULL;
    thread->fpregs = xmalloc(struct fp_regs);
    /* stack != NULL means Java Thread */
    if (stack == NULL || !upcalls_active) {
      thread->cpu = -1;
    } else {
      thread->cpu = pick_cpu_upcall();
    }
    thread->preempt_count = 0;
    thread->resched_running_time = 0;
    thread->timeslice = timeslice;
    thread->lock_count = 0;
    thread->appsched_id = -1;
    clear_running(thread);
    INIT_LIST_HEAD(&thread->joiners);
    INIT_LIST_HEAD(&thread->ready_list);
    INIT_LIST_HEAD(&thread->thread_list);
    INIT_LIST_HEAD(&thread->aux_thread_list);

    thread->id = id;
    add_thread_list(thread);
    /* Bug on overflow */
    BUG_ON(thread->id == 0);

    if (trace_sched() || trace_mm() || trace_startup())
	ttprintk("CT %d %s %d %x %lx\n", 
		thread->id, name, thread->cpu, thread->flags, thread->sp);

    wake(thread);

    return thread;
}

struct thread* guk_create_thread_with_stack(char *name,
                                        void (*function)(void *),
					int flags,
                                        void *stack,
                                        unsigned long stack_size,
                                        void *data)
{
    return create_thread_with_id_stack(name, function, flags, stack, stack_size, 
				       data, thread_id++);
}

struct thread* create_thread(char *name,
                             void (*function)(void *),
			     int flags,
                             void *data)
{
    return guk_create_thread_with_stack(name, function, flags, NULL, 0, data);
}


struct thread* create_debug_thread(char *name, void (*function)(void *), void * data) {
  return create_thread_with_id_stack(name, function, UKERNEL_FLAG, NULL,
				     0, data, debug_thread_id++);
}

struct thread* create_idle_thread(unsigned int cpu)
{
    char buf[256];
    struct thread *thread;

    sprintf(buf, "Idle%d", cpu);
    /* Call architecture specific setup. */
    thread = arch_create_thread(strdup(buf),
                                idle_thread_fn,
                                NULL,
                                0,
                                (void *)(unsigned long)cpu);
    /* Not runnable, not exited, not sleeping */
    thread->flags = UKERNEL_FLAG;
    thread->regs = NULL;
    thread->cpu = cpu;
    thread->preempt_count = 1;
    thread->resched_running_time = 0;
    thread->lock_count = 0;
    thread->appsched_id = -1;
    INIT_LIST_HEAD(&thread->joiners);
    thread->id = cpu;
    if (trace_sched() || trace_mm())
        ttprintk("CT %d %s %d %x %lx\n", thread->id, buf, thread->cpu, thread->flags, thread->sp);
    set_running(thread);

    return thread;
}

void exit_thread(void)
{
    struct thread *thread = current;

    /* The synchronisation works as following:
     *  - exit_thread is allways executed by current, it must therefore be the
     *    case that RUNNING_FLAG is set. This guarantees that no other CPU will
     *    schedule us in the process,
     *  - we disable preemption, so that we can complete the whole process
     *    uninterrupted,
     *  - once DYING_FLAG is set schedule() will take care of cleaning thread
     *    structure up, however:
     *    > the thread cannot be destroyed until ctxt switch is completed,
     *    > if we allowed other CPUs to destroy the thread, we would have a race
     *      (very difficult to solve, since we don't hold any locks when
     *      the final thread switch happens),
     *    > we solve that problem by only allowing current CPU to destroy the
     *      thread. On the first call to schedule() RUNNING_FLAG will be
     *      cleared, but the thread will prevail. On the second call on the
     *      _same_ CPU DYING_FLAG is set, RUNNING_FLAG is cleared, and we can
     *      safely destroy the thread.
     */
    preempt_disable();
    BUG_ON(!is_running(thread));
    block(thread);

    if (trace_sched() || trace_startup()) ttprintk("TX %d\n", thread->id);

    spin_lock(&zombie_lock);
    set_dying(thread);
    list_add_tail(&thread->ready_list, &zombie_queue);
    /* wake up any joiners now rather than wait for reap when idle  */
    if(!list_empty(&thread->joiners)) wake_joiners(thread);

    spin_unlock(&zombie_lock);

    preempt_enable();

    schedule();
}

/*
 * mark thread as not runnable, thread is usually the current thread. make sure it is in 
 * some waiting queue before calling this, because after this call it could be preempted
 * immediately and not rescheduled again
 */
void block(struct thread *thread)
{
    if (is_runnable(thread)) {
	long flags;
	if (trace_sched()) ttprintk("BK %d %lx\n", thread->id, thread->flags);
	if (upcalls_active && is_appsched(thread)) {
	    preempt_disable();
	    clear_runnable(thread);
	    block_upcall(thread->appsched_id, smp_processor_id());
	    preempt_enable();
	} else {
	    spin_lock_irqsave(&ready_lock, flags);
	    clear_runnable(thread);
	    list_del(&thread->ready_list);
	    spin_unlock_irqrestore(&ready_lock, flags);
	}
    } else {
	xprintk("WARNING: try to block a non runnable thread %d %s\n", thread->id, thread->name);
	backtrace(get_bp(), 0);
    }
}

/* This is a variant of wake in that does not check that we are already suspended
 * in the debugger
 */
void db_wake(struct thread *thread)
{
    if (trace_sched()) ttprintk("WK %d\n", thread->id);
    if (upcalls_active && is_appsched(thread)) {
	/* for java threads we have to upcall into the vm scheduler
	 * and *NOT* insert the thread into the ready_queue
	 */
	preempt_disable();
	set_runnable(thread);
	wake_upcall((int)thread->appsched_id, smp_processor_id());
	preempt_enable();
    } else {
	long flags;
	spin_lock_irqsave(&ready_lock, flags);
	set_runnable(thread);
	list_add_tail(&thread->ready_list, &ready_queue);
	spin_unlock_irqrestore(&ready_lock, flags);
    }
    /* cpu running this thread may be blocked in idle thread, so kick it */
    if (thread->cpu != smp_processor_id()) kick_cpu(thread->cpu);
}

/*
 * marks a thread runnable and inserts it into the run queue.
 * the thread to wake up may not be in another queue (e.g. sleep queue or zombie queue)
 */
void guk_wake(struct thread *thread)
{
    /* regs is only valid when the thread is currently interrupted */
    thread->regs = NULL;

    if (is_debug_suspend(thread)) {
	/* suspended in debugger; debugger must handle wakeups itself */
    } else if (is_req_debug_suspend(thread)) {
	/* control is in debugger */
	clear_req_debug_suspend(thread);
	set_debug_suspend(thread);
    } else {
	BUG_ON(is_dying(thread));
	if (!is_runnable(thread) || (upcalls_active && is_appsched(thread))) {
	    db_wake(thread);
	} else {
	    //xprintk("WARNING: waking a runnable thread %d %s %x\n", thread->id, thread->name, thread->flags);
	    //print_backtrace(current);
	}
    }
}

/*
 * add a sleep queue object to the sleep queue
 */
void guk_sleep_queue_add(struct sleep_queue *sq)
{
    long flags;
    struct list_head *iterator;
    struct sleep_queue *entry;

    /* Setting wakeup time needs to be serialised with respect to blocking_time
     * reading the value off */
    spin_lock_irqsave(&sleep_lock, flags);

    /* keep the sleep queue ordered by wake up time */
    list_for_each(iterator, &sleep_queue) {
	entry = list_entry(iterator, struct sleep_queue, list);
	if (entry->wakeup_time > sq->wakeup_time)
	    break;
    }

    list_add_tail(&sq->list, iterator);
    set_active(sq);
    spin_unlock_irqrestore(&sleep_lock, flags);
}

/*
 * remove a sleep queue object from the sleep queue
 */
void guk_sleep_queue_del(struct sleep_queue *sq)
{
    long flags;
    spin_lock_irqsave(&sleep_lock, flags);
    list_del(&sq->list);
    clear_active(sq);
    spin_unlock_irqrestore(&sleep_lock, flags);
}

void *guk_create_timer(void)
{
    struct sleep_queue *sq = (struct sleep_queue *)xmalloc(struct sleep_queue);
    init_sleep_queue(sq);
    return sq;
}

void guk_delete_timer(struct sleep_queue *sq)
{
    free(sq);
}

void guk_add_timer(struct sleep_queue *sq, s_time_t timeout)
{
    sq->thread = current;
    sq->wakeup_time = NOW() + timeout*1000000;
    guk_sleep_queue_add(sq);
}

int guk_remove_timer(struct sleep_queue *sq)
{
    int result = 0;
    guk_sleep_queue_del(sq);
    if (is_expired(sq)) {
	clear_expired(sq);
	result = 1;
    }

    return result;
}
/*
 * delay current thread for nanosecs nanoseconds
 */
int guk_nanosleep(u64 nanosecs)
{
    struct thread *thread = current;
    DEFINE_SLEEP_QUEUE(sq);

    preempt_disable();
    block(thread);

    sq.wakeup_time = NOW()  + nanosecs;
    set_sleeping(thread);
    guk_sleep_queue_add(&sq);

    preempt_enable();
    schedule();

    if (is_sleeping(thread)) {
	clear_sleeping(thread);
	guk_sleep_queue_del(&sq);
    }
    clear_expired(&sq);

    if(is_interrupted(thread)) {
	clear_interrupted(thread);
	return -1;
    } else {
	return 0;
    }
}

int guk_sleep(u32 millisecs)
{
  return nanosleep(MILLISECS(millisecs));
}

/*
 * interrupt sleeping, joining or waiting thread
 */
void guk_interrupt(struct thread *thread)
{
    if (!(is_runnable(thread) || is_running(thread) || is_dying(thread))) {
	if (is_joining(thread)) {
	    spin_lock(&zombie_lock);
	    list_del(&thread->ready_list);
	    clear_joining(thread);
	    spin_unlock(&zombie_lock);
	}

	set_interrupted(thread); /* the interrupted thread has to check this on
				  * return from schedule() */
	wake(thread);
    }
}

/*
 * Set timeslice for given thread
 */
int guk_set_timeslice(struct thread *thread, int timeslice) {
    int previous = thread->timeslice;
    thread->timeslice = MILLISECS(timeslice);
    return previous;
}

static inline void ht_pause(void)
{
    __asm__ __volatile__("pause");
}

unsigned long guk_local_irqsave(void) {
	unsigned long flags = 0;
	local_irq_save(flags);
	return flags;
}

void guk_local_irqrestore(unsigned long flags) {
	local_irq_restore(flags);
}

/*
 * a CPU marked as not UP will block in here
 */
static void check_suspend(int cpu)
{
    if (per_cpu(cpu, cpu_state) != CPU_UP) {
	if (trace_sched())
	    ttprintk("CD %d\n", cpu);
	preempt_disable();
	/* loops until cpu is set to CPU_RESUMING */
	while(1) {
	    smp_cpu_safe(cpu); /* will restart the idle thread on CPU_RESUMING */
	    ht_pause();
	}
	preempt_enable();
	/* we should never return here; when the cpu is activated again the idle thread
	 * starts at the beginning */
	BUG();
    }
}

/*
 * Checks for runnable threads in the system,
 * cpu is the cpu of the idle thread making the call.
 */
static int runnable_threads(int cpu)
{
    struct thread *thread;
    struct list_head *iterator;
    int retval = 0;
    long flags;
    spin_lock_irqsave(&ready_lock, flags);
    list_for_each(iterator, &ready_queue) {
	thread = list_entry(iterator, struct thread, ready_list);
	if (is_runnable(thread) && !is_running(thread)) {
	    if (is_ukernel(thread) || thread->cpu == cpu) {
		retval = 1;
		break;
	    }
	}
    }
    spin_unlock_irqrestore(&ready_lock, flags);
    if (retval == 0 && upcalls_active) {
      preempt_disable();
      retval = runnable_upcall(cpu);
      preempt_enable();
    }
    return retval;
}

__attribute__((weak)) void guk_trace_cpu_fn(void) {
}

/* This function is run by the tracing cpu if enabled */
void trace_cpu_idle(int cpu) {
  for(;;) {
    check_suspend(cpu);
    local_irq_disable();
    block_domain(NOW() + SECONDS(1)); /* enables interrupts */
    guk_trace_cpu_fn();
  } 
}

extern void timer_handler(evtchn_port_t ev, void *ign);
/*
 * the idle thread executes this function
 */
void idle_thread_fn(void *data)
{
    unsigned long cpu = (unsigned long)data;

    BUG_ON(cpu != smp_processor_id());
    if (trace_sched()) ttprintk("RI\n");

    bind_virq(VIRQ_TIMER, cpu, timer_handler, NULL);
    per_cpu(cpu, cpu_state) = CPU_UP;
    /* Initialise trap table for all cpus but the initial */
    if(cpu > 0)
        trap_init();
    __sti();

    if (trace_cpu == cpu) {
      trace_cpu_idle(cpu);
    }

    preempt_enable();
    for(;;)
    {
        schedule();

	check_suspend(cpu);
        /* Try to reap dead threads, just to make sure we aren't hogging up
         * resources while sleeping */
        reap_dead();

        /* block until the next timeout expires, or for 10 secs, whichever comes
         * first.
         * IRQs need to be disabled, to guarantee that Idle thread is not
         * preempted in the middle, which could lead to setting incorrect
         * timeout.
         * IRQs are reenabled in block_domain */
        local_irq_disable();
	s_time_t until = blocking_time(cpu);

	/* a thread could be made runnable by an interrupt that happened
	 * between schedule() and local_irq_disable(). so
	 * check for ready threads again */
	if (until > 0 && !runnable_threads(cpu)) {
            if (trace_sched()) ttprintk("BI %ld\n", until);
            
	    block_domain(until); /* enables interrupts */

            if (trace_sched()) ttprintk("WI\n");
	    /* check the CPU state again, because we could have been woken up by an IPI */
	    check_suspend(cpu);
	} else {
	    /* thread is ready, enable interrupts and continue to top of the loop */
	    local_irq_enable();
	}
    }
}

extern int num_option(char *cmd_line, char *option);
void init_sched(char *cmd_line)
{
    int opt_value;
    if (trace_sched()) ttprintk("IS %lx %lx %lx %lx\n",
				&ready_lock, &sleep_lock, &zombie_lock, &thread_list_lock);
    init_maxine();
    scheduler_upcalls_allowed = strstr(cmd_line, APPSCHED_OPTION) != NULL;
    trace_cpu = num_option(cmd_line, TRACE_CPU_OPTION);

    /* Check for Time Slice setting */
    opt_value = num_option(cmd_line, TIMESLICE_OPTION);
    if (opt_value > 0) {
      if (trace_sched()) ttprintk("ST %d\n", timeslice);
      timeslice = MILLISECS(opt_value);
    }

}

