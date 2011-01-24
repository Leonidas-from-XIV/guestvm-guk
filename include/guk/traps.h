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
