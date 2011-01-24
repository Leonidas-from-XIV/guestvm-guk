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
 *
 * Author: Grzegorz Milos
 * Changes: Harald Roeck
 *          Mick Jordan
 */

#include <guk/os.h>
#include <guk/mm.h>
#include <guk/smp.h>
#include <guk/time.h>
#include <guk/events.h>
#include <xen/xen.h>
#include <xen/vcpu.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/spinlock.h>

#include <errno.h>

/* TODO: move this away */
#define X86_EFLAGS_IF   0x00000200	/* Interrupt Flag */

int smp_init_completed = 0;
int smp_active = 1; // CPU 0 is always active
struct cpu_private percpu[MAX_VIRT_CPUS];
/* Assembler interface fns in entry.S. */
void hypervisor_callback(void);
void failsafe_callback(void);
extern void idle_thread_starter(void);

static void ipi_handler(evtchn_port_t port, void *unused)
{
//    tprintk("IPI recieved on CPU=%d\n", smp_processor_id());
    if(per_cpu(smp_processor_id(), cpu_state) == CPU_SUSPENDING) {
	set_need_resched(current);
    }
}

static void init_cpu_pda(unsigned int cpu)
{
#if defined(__x86_64__)
    unsigned long *irqstack;
    irqstack = (unsigned long *)alloc_pages(STACK_SIZE_PAGE_ORDER);
    if (cpu == 0) {
	asm volatile ("movl %0,%%fs ; movl %0,%%gs"::"r" (0));
	wrmsrl(0xc0000101, &(percpu[cpu]));	/* 0xc0000101 is MSR_GS_BASE */
    }
    per_cpu(cpu, irqcount) = -1;
    per_cpu(cpu, irqstackptr) = (unsigned long)irqstack + STACK_SIZE;
    per_cpu(cpu, idle_thread) = create_idle_thread(cpu);
    /*
     * Save pointer to idle thread onto the irq stack. This is used by
     * current macro (and related, smp_processor_id)
     */
    BUG_ON(!per_cpu(cpu, idle_thread));
    irqstack[0] = (unsigned long)per_cpu(cpu, idle_thread);
#endif
    per_cpu(cpu, current_thread) = per_cpu(cpu, idle_thread);
    per_cpu(cpu, cpunumber) = cpu;
    per_cpu(cpu, upcall_count) = 0;
    memset(&per_cpu(cpu, shadow_time), 0, sizeof(struct shadow_time_info));
    per_cpu(cpu, ipi_port) = evtchn_alloc_ipi(ipi_handler, cpu, NULL);
    per_cpu(cpu, cpu_state) = cpu == 0 ? CPU_UP : CPU_SLEEPING;
    per_cpu(cpu, db_support) = NULL;
}

void smp_signal_cpu(int cpu)
{
    if (smp_init_completed) {
	if(cpu != smp_processor_id() && per_cpu(cpu, cpu_state) == CPU_SLEEPING)
	    notify_remote_via_evtchn(per_cpu(cpu, ipi_port));
    }
}

static DEFINE_SPINLOCK(cpu_lock);
static int suspend_count = 0;

/* check for suspend and resume events on this cpu and change cpu state
 * only called by the idle thread
 */
void smp_cpu_safe(int cpu)
{
    struct thread *idle;
    switch (per_cpu(cpu, cpu_state)) {
	case CPU_SUSPENDING: /* cpu is marked to suspend */
	    spin_lock(&cpu_lock);
	    --suspend_count;
	    spin_unlock(&cpu_lock);
	    per_cpu(cpu, cpu_state) = CPU_DOWN; /* mark cpu as down */
	    spin_lock(&cpu_lock);
	    smp_active--;
	    spin_unlock(&cpu_lock);
	    break;
	case CPU_RESUMING: /* cpu is marked to resume */
	    local_irq_disable();
	    idle = per_cpu(cpu, idle_thread);
	    spin_lock(&cpu_lock);
	    smp_active++;
	    spin_unlock(&cpu_lock);

	    /* restart idle thread */
	    /* the idle thread will set the state of the CPU to UP again */
	    restart_idle_thread((long)cpu,
		   (long)idle->stack + idle->stack_size,
		   (long)idle_thread_starter,
		   idle_thread_fn);
	    /* will never return */
	    BUG();
	    break;
	case CPU_DOWN:
	case CPU_UP:
	case CPU_SLEEPING:
	    break;
	default:
	    xprintk("WARNING: unknown CPU state\n");
    }
}

int smp_num_active(void) {
    int result;
    spin_lock(&cpu_lock);
    result = smp_active;
    spin_unlock(&cpu_lock);
    return result;
}

int guk_smp_cpu_state(int cpu) {
    return per_cpu(cpu, cpu_state);
}


static void cpu_initialize_context(unsigned int cpu)
{
    vcpu_guest_context_t ctxt;
    struct thread *idle_thread;

    init_cpu_pda(cpu);

    idle_thread = per_cpu(cpu, idle_thread);
    memset(&ctxt, 0, sizeof(ctxt));
    ctxt.flags = VGCF_IN_KERNEL;
    ctxt.user_regs.ds = __KERNEL_DS;
    ctxt.user_regs.es = 0;
    ctxt.user_regs.fs = 0;
    ctxt.user_regs.gs = 0;
    ctxt.user_regs.ss = __KERNEL_SS;
    ctxt.user_regs.eip = idle_thread->ip;
    ctxt.user_regs.eflags = X86_EFLAGS_IF | 0x1000;	/* IOPL_RING1 */
    memset(&ctxt.fpu_ctxt, 0, sizeof(ctxt.fpu_ctxt));
    ctxt.ldt_ents = 0;
    ctxt.gdt_ents = 0;
#ifdef __i386__
    ctxt.user_regs.cs = __KERNEL_CS;
    ctxt.user_regs.esp = idle_thread->sp;
    ctxt.kernel_ss = __KERNEL_SS;
    ctxt.kernel_sp = ctxt.user_regs.esp;
    ctxt.event_callback_cs = __KERNEL_CS;
    ctxt.event_callback_eip = (unsigned long)hypervisor_callback;
    ctxt.failsafe_callback_cs = __KERNEL_CS;
    ctxt.failsafe_callback_eip = (unsigned long)failsafe_callback;
    ctxt.ctrlreg[3] = xen_pfn_to_cr3(virt_to_mfn(start_info.pt_base));
#else /* __x86_64__ */
    ctxt.user_regs.cs = __KERNEL_CS;
    ctxt.user_regs.esp = idle_thread->sp;
    ctxt.kernel_ss = __KERNEL_SS;
    ctxt.kernel_sp = ctxt.user_regs.esp;
    ctxt.event_callback_eip = (unsigned long)hypervisor_callback;
    ctxt.failsafe_callback_eip = (unsigned long)failsafe_callback;
    ctxt.syscall_callback_eip = 0;
    ctxt.ctrlreg[3] = xen_pfn_to_cr3(virt_to_mfn(start_info.pt_base));
    ctxt.gs_base_kernel = (unsigned long)&percpu[cpu];
#endif
    int err = HYPERVISOR_vcpu_op(VCPUOP_initialise, cpu, &ctxt);
    if (err) {
	char *str;

	switch (err) {
	    case -EINVAL:
		/*
		 * This interface squashes multiple error sources
		 * to one error code.  In particular, an X_EINVAL
		 * code can mean:
		 *
		 * -	the vcpu id is out of range
		 * -	cs or ss are in ring 0
		 * -	cr3 is wrong
		 * -	an entry in the new gdt is above the
		 *	reserved entry
		 * -	a frame underneath the new gdt is bad
		 */
		str = "something is wrong :(";
		break;
	    case -ENOENT:
		str = "no such cpu";
		break;
	    case -ENOMEM:
		str = "no mem to copy ctxt";
		break;
	    case -EFAULT:
		str = "bad address";
		break;
	    case -EEXIST:
		/*
		 * Hmm.  This error is returned if the vcpu has already
		 * been initialized once before in the lifetime of this
		 995 			 * domain.  This is a logic error in the kernel.
		 996 			 */
		str = "already initialized";
		break;
	    default:
		str = "<unexpected>";
		break;
	}

	xprintk("vcpu%d: failed to init: error %d: %s",
		cpu, -err, str);
    }
}

void init_smp(void)
{
    unsigned int cpu;
    int res;

    memset(percpu, 0, sizeof(struct cpu_private) * MAX_VIRT_CPUS);
    init_cpu_pda(0);
    /*
     * Init of CPU0 is completed, smp_init_completed must be set before we
     * initialise remaining CPUs, because smp_proccessor_id macro will not
     * work properly
     */
    smp_init_completed = 1;
    /*
     * We have now completed the init of cpu0 
     */
    if (trace_smp())
	tprintk("Initing SMP cpus.\n");

    for (cpu = 1; cpu < MAX_VIRT_CPUS; cpu++) {
	per_cpu(cpu, cpu_state) = CPU_DOWN;
	res = HYPERVISOR_vcpu_op(VCPUOP_is_up, cpu, NULL);
	if (res >= 0) {
	    if (trace_smp())
		tprintk("Bringing up CPU=%d\n", cpu);
	    cpu_initialize_context(cpu);
	    BUG_ON(HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL));
	    spin_lock(&cpu_lock);
	    smp_active++;
	    spin_unlock(&cpu_lock);
	}
    }
    if (trace_smp()) {
      tprintk("SMP: %d CPUs active\n", smp_active);
      for (cpu = 0; cpu < MAX_VIRT_CPUS; cpu++) {
        tprintk("SMP: cpu_state %d %d\n", cpu, per_cpu(cpu, cpu_state));
      }
    }
    if (trace_sched()) ttprintk("SMP %d\n", smp_active);
}

void smp_suspend(void)
{
    int cpu;

    spin_lock(&cpu_lock);
    suspend_count = 0;

    for (cpu = 1; cpu < MAX_VIRT_CPUS; cpu++) {
	if (per_cpu(cpu, cpu_state) == CPU_UP || per_cpu(cpu, cpu_state) == CPU_SLEEPING) {
	    if (trace_smp())
		tprintk("mark CPU %d to go down\n", cpu);
	    ++suspend_count;
	    per_cpu(cpu, cpu_state) = CPU_SUSPENDING;
	    wmb();
	    smp_signal_cpu(cpu);
	}
    }
    while(suspend_count) {
	spin_unlock(&cpu_lock);
	sleep(1);
	spin_lock(&cpu_lock);
    }
    spin_unlock(&cpu_lock);
}

void smp_resume(void)
{
    int cpu;
    int res;
    struct thread *idle;

    cpu = 0; //smp_processor_id();
    BUG_ON(cpu != 0);
    /* get the ipi handler for this cpu */
    per_cpu(0, ipi_port) = evtchn_alloc_ipi(ipi_handler, cpu, NULL);

    for (cpu = 1; cpu < MAX_VIRT_CPUS; cpu++) {
	res = HYPERVISOR_vcpu_op(VCPUOP_is_up, cpu, NULL);
	if (res >= 0) {
	    if (trace_smp())
		tprintk("Bringing up CPU=%d\n", cpu);
	    per_cpu(cpu, cpu_state) = CPU_RESUMING;
	    idle = per_cpu(cpu, idle_thread);

	    if (idle) { /* reinitalize idle thread to restart */
		idle->sp = (unsigned long)idle->stack + idle->stack_size;
		stack_push(idle, (unsigned long) idle_thread_fn);
		stack_push(idle, (unsigned long) cpu);
		idle->ip = (unsigned long) idle_thread_starter;
	    }
	    per_cpu(cpu, ipi_port) = evtchn_alloc_ipi(ipi_handler, cpu, NULL);
	    BUG_ON(HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL));
	}
    }
}
