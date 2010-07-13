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
#include <guk/os.h>
#include <guk/traps.h>
#include <guk/hypervisor.h>
#include <guk/mm.h>
#include <guk/sched.h>
#include <guk/db.h>
#include <guk/trace.h>

#include <x86/arch_sched.h>
#include <x86/traps.h>

#include <lib.h>
/*
 * These are assembler stubs in entry.S.
 * They are the actual entry points for virtual exceptions.
 */
void divide_error(void);
void debug(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void simd_coprocessor_error(void);
void alignment_check(void);
void spurious_interrupt_bug(void);
void machine_check(void);

void dump_regs(struct pt_regs *regs, printk_function_ptr printk_function) {
#ifdef __i386__
    (*printk_function)("EIP: %x, EFLAGS %x.\n", regs->eip, regs->eflags);
    (*printk_function)("EBX: %08x ECX: %08x EDX: %08x\n",
	   regs->ebx, regs->ecx, regs->edx);
    (*printk_function)("ESI: %08x EDI: %08x EBP: %08x EAX: %08x\n",
	   regs->esi, regs->edi, regs->ebp, regs->eax);
    (*printk_function)("DS: %04x ES: %04x orig_eax: %08x, eip: %08x\n",
	   regs->xds, regs->xes, regs->orig_eax, regs->eip);
    (*printk_function)("CS: %04x EFLAGS: %08x esp: %08x ss: %04x\n",
	   regs->xcs, regs->eflags, regs->esp, regs->xss);
#else
    (*printk_function)("RIP: %04lx:[<%016lx>] ", regs->cs & 0xffff, regs->rip);
    (*printk_function)("\nRSP: %04lx:%016lx  EFLAGS: %08lx\n",
           regs->ss, regs->rsp, regs->eflags);
    (*printk_function)("RAX: %016lx RBX: %016lx RCX: %016lx\n",
           regs->rax, regs->rbx, regs->rcx);
    (*printk_function)("RDX: %016lx RSI: %016lx RDI: %016lx\n",
           regs->rdx, regs->rsi, regs->rdi);
    (*printk_function)("RBP: %016lx R08: %016lx R09: %016lx\n",
           regs->rbp, regs->r8, regs->r9);
    (*printk_function)("R10: %016lx R11: %016lx R12: %016lx\n",
           regs->r10, regs->r11, regs->r12);
    (*printk_function)("R13: %016lx R14: %016lx R15: %016lx\n",
           regs->r13, regs->r14, regs->r15);
#endif
}

void dump_regs_and_stack(struct pt_regs *regs, printk_function_ptr printk_function)
{
    struct thread *thread = current;
    if (thread)
        (*printk_function)("Thread: %s, %d, CPU=%d\n", thread->name, thread->id, thread->cpu);

    dump_regs(regs, printk_function);

    dump_sp((unsigned long*)regs->rsp, printk_function);

    if (thread && is_ukernel(thread)) {
	(*printk_function)("stack backtrace:\n");
	backtrace((void **)regs->rbp, (void *)regs->rip);
    }
}

void guk_dump_sp(unsigned long *sp, printk_function_ptr printk_function) {
    (*printk_function)("[RSP++]\n");
    {
      int i = 0;
      for (i = 1; i <= 64; i++) {
        if (validate((unsigned long)sp)) {
	  (*printk_function)("%016lx ", *sp++);
	  if ((i % 4) == 0) (*printk_function)("\n");
	} else {
	  (*printk_function)("(SP) is not mapped\n");
	  break;
	}
      }
    }
}

static void do_trap(int trapnr, char *str, struct pt_regs * regs, unsigned long error_code)
{
    struct thread *thread = current;
    if (thread && guk_debugging()) {
      struct fp_regs *fpregs = thread->fpregs;
      asm (save_fp_regs_asm : : [fpr] "r" (fpregs));
      BUG_ON(!is_preemptible(thread));
      set_req_debug_suspend(thread);
      set_need_resched(thread);
    } else {
      xprintk("FATAL:  Unhandled Trap %d (%s), error code=0x%lx\n", trapnr, str, error_code);
      xprintk("Regs address %p\n", regs);
      dump_regs_and_stack(regs, xprintk);
      crash_exit();
    }
}

#define DO_ERROR(trapnr, str, name) \
void do_##name(struct pt_regs * regs, unsigned long error_code) \
{ \
	do_trap(trapnr, str, regs, error_code); \
}

#define DO_ERROR_INFO(trapnr, str, name, sicode, siaddr) \
void do_##name(struct pt_regs * regs, unsigned long error_code) \
{ \
	do_trap(trapnr, str, regs, error_code); \
}

static fault_handler_t fault_handler_table[] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

#define CHECK_ERROR_INFO(trapnr, str, fname) \
fault_handler_t check_##fname(struct pt_regs * regs, unsigned long error_code) \
{ \
    struct thread *thread = current; \
    fault_handler_t f = fault_handler_table[trapnr]; \
    if (!f || !thread || is_ukernel(thread)) { \
	do_trap(trapnr, str, regs, error_code); \
    } \
    return f; \
}

CHECK_ERROR_INFO( 0, "divide error", divide_error)
CHECK_ERROR_INFO( 0, "general protection", general_protection)
CHECK_ERROR_INFO( 0, "page fault", page_fault)
DO_ERROR( 4, "overflow", overflow)
DO_ERROR( 5, "bounds", bounds)
DO_ERROR_INFO( 6, "invalid operand", invalid_op, ILL_ILLOPN, regs->eip)
DO_ERROR( 7, "device not available", device_not_available)
DO_ERROR( 9, "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, "invalid TSS", invalid_TSS)
DO_ERROR(11, "segment not present", segment_not_present)
DO_ERROR(12, "stack segment", stack_segment)
DO_ERROR_INFO(17, "alignment check", alignment_check, BUS_ADRALN, 0)
DO_ERROR(18, "machine check", machine_check)

#define read_cr2() \
        (HYPERVISOR_shared_info->vcpu_info[smp_processor_id()].arch.cr2)

void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
    unsigned long addr = read_cr2();
    if (trace_traps()) {
      tprintk("PF @ %p\n", addr); dump_regs_and_stack(regs, tprintk);
    }
    if (guk_debugging()) {
      /* If db-back initiated this access, we want to report failure and recover */
      if (db_is_dbaccess_addr(addr)) {
        jmp_db_back_handler(db_back_handler);
      } else if (db_is_watchpoint(addr, regs)) {
	return;
      }
    }
    /* A real trap of some kind */
    fault_handler_t f = check_page_fault(regs, error_code);
    (*f)(GENERAL_PROTECTION_FAULT, addr, regs);
}

void do_general_protection(struct pt_regs *regs, long error_code)
{
    /* If db-back initiated this access, we want to report failure and recover */
    if (guk_debugging() && db_is_dbaccess()) {
        jmp_db_back_handler(db_back_handler);
    } else {
      if (trace_traps()) {
	tprintk("GPF\n"); dump_regs_and_stack(regs, tprintk);
      }
      fault_handler_t f = check_general_protection(regs, error_code);
      (*f)(GENERAL_PROTECTION_FAULT, regs->rip, regs);
      }
}

void do_divide_error(struct pt_regs * regs, unsigned long error_code) {
  fault_handler_t f = check_divide_error(regs, error_code);
  (*f)(DIVIDE_ERROR, regs->rip, regs);
}


void do_coprocessor_error(struct pt_regs * regs)
{
    xprintk("Copro error\n");
    dump_regs_and_stack(regs, xprintk);
    crash_exit();
}

void simd_math_error(void *eip)
{
    xprintk("SIMD error\n");
    crash_exit();
}

void do_simd_coprocessor_error(struct pt_regs * regs)
{
    xprintk("SIMD copro error\n");
    dump_regs_and_stack(regs, xprintk);
    crash_exit();
}

void do_spurious_interrupt_bug(struct pt_regs * regs)
{
}

void do_int3(struct pt_regs *regs)
{
    struct thread *thread = current;
    struct fp_regs *fpregs = thread->fpregs;
    asm (save_fp_regs_asm : : [fpr] "r" (fpregs));
    if (trace_traps()) {
      tprintk("INT3 trap executed for thread %d, regs %lx, flags %x.\n", thread->id, thread->regs, thread->flags);
    }
    BUG_ON(!is_preemptible(thread));
    set_req_debug_suspend(thread);
    set_need_resched(thread);
}

void do_debug(struct pt_regs *regs)
{
    struct thread *thread = current;
    struct fp_regs *fpregs = thread->fpregs;
    if (guk_debugging() && db_watchpoint_step(regs)) {
      return;
    }
    asm (save_fp_regs_asm : : [fpr] "r" (fpregs));
    /* Bug if regs aren't saved properly */
    BUG_ON(thread->regs != regs);
    //BUG_ON(thread->regs->eflags > 0xFFF);

    /* Stepping controler should have set the stepped flag for this thread, make
     * sure that's the case */
    BUG_ON(!is_stepped(thread));
    BUG_ON(!is_debug_suspend(thread));
    /* Resched the thread: setting the resched flag only has the benefit that
     * that preemption disabled code blocks will execute atomically */
    set_req_debug_suspend(thread);
    set_need_resched(thread);
    /* Clear the traps flag */
    regs->eflags &= ~0x00000100;
}

/*
 * Submit a virtual IDT to the hypervisor. This consists of tuples
 * (interrupt vector, privilege ring, CS:EIP of handler).
 * The 'privilege ring' field specifies the least-privileged ring that
 * can trap to that vector using a software-interrupt instruction (INT).
 */
static trap_info_t trap_table[] = {
    {  0, 0, __KERNEL_CS, (unsigned long)divide_error                },
    {  1, 0, __KERNEL_CS, (unsigned long)debug                       },
    {  3, 3, __KERNEL_CS, (unsigned long)int3                        },
    {  4, 3, __KERNEL_CS, (unsigned long)overflow                    },
    {  5, 3, __KERNEL_CS, (unsigned long)bounds                      },
    {  6, 0, __KERNEL_CS, (unsigned long)invalid_op                  },
    {  7, 0, __KERNEL_CS, (unsigned long)device_not_available        },
    {  9, 0, __KERNEL_CS, (unsigned long)coprocessor_segment_overrun },
    { 10, 0, __KERNEL_CS, (unsigned long)invalid_TSS                 },
    { 11, 0, __KERNEL_CS, (unsigned long)segment_not_present         },
    { 12, 0, __KERNEL_CS, (unsigned long)stack_segment               },
    { 13, 0, __KERNEL_CS, (unsigned long)general_protection          },
    { 14, 0, __KERNEL_CS, (unsigned long)page_fault                  },
    { 15, 0, __KERNEL_CS, (unsigned long)spurious_interrupt_bug      },
    { 16, 0, __KERNEL_CS, (unsigned long)coprocessor_error           },
    { 17, 0, __KERNEL_CS, (unsigned long)alignment_check             },
    { 19, 0, __KERNEL_CS, (unsigned long)simd_coprocessor_error      },
    {  0, 0,           0, 0                           }
};

void guk_register_fault_handler(int fault, fault_handler_t fault_handler) {
  fault_handler_table[fault] = fault_handler;
  if (fault == GENERAL_PROTECTION_FAULT) {
    fault_handler_table[PAGE_FAULT] = fault_handler;
  }
}

void trap_init(void)
{
    HYPERVISOR_set_trap_table(trap_table);
}

