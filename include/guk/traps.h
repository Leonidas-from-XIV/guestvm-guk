#ifndef _TRAPS_H_
#define _TRAPS_H_

struct pt_regs;

typedef void (*fault_handler_t)(int fault, unsigned long address, struct pt_regs *regs);

/* register a call back fault handler for given fault */
void guk_register_fault_handler(int fault, fault_handler_t fault_handler);

typedef void (*printk_function_ptr)(const char *fmt, ...);

/* dump 32 words from sp using printk_function */
void guk_dump_sp(unsigned long *sp, printk_function_ptr printk_function);
#define dump_sp guk_dump_sp

/* dump the registers and sp and the C stack (for a xenos thread) */
void dump_regs_and_stack(struct pt_regs *regs, printk_function_ptr printk_function);

#endif /* _TRAPS_H_ */
