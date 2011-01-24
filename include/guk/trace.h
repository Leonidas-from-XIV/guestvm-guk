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
#ifndef _TRACE_H_
#define _TRACE_H_

/* Values that control tracing in the microkernel
 * the command line strings must match those in VEVMProgramArguments.java
 * All strings are of the form -XX:GVMTrace[:subsystem], where [:subsystem] is optional
 * If no subsystem is specified, tracing is enabled, and presumed to be switched on
 * per subsystem during execution.
 *
 * Author: Mick Jordan
 */

#define TRACE_HEADER "-XX:GUKTrace"

#define TRACE_VAR(name) \
extern int trace_state_##name; 

TRACE_VAR(sched)
TRACE_VAR(startup)
TRACE_VAR(blk)
TRACE_VAR(db_back)
TRACE_VAR(fs_front)
TRACE_VAR(gnttab)
TRACE_VAR(mm)
TRACE_VAR(mmpt)
TRACE_VAR(net)
TRACE_VAR(service)
TRACE_VAR(smp)
TRACE_VAR(xenbus)
TRACE_VAR(traps)

#define trace_sched() trace_state_sched
#define trace_startup() trace_state_startup
#define trace_blk() trace_state_blk
#define trace_db_back() trace_state_db_back
#define trace_fs_front() trace_state_fs_front
#define trace_gnttab() trace_state_gnttab
#define trace_mm() trace_state_mm
#define trace_mmpt() trace_state_mmpt
#define trace_net() trace_state_net
#define trace_service() trace_state_service
#define trace_smp() trace_state_smp
#define trace_xenbus() trace_state_xenbus
#define trace_traps() trace_state_traps

/* Support for runtime enabling/disabling and inquiry from non-C code.
   trace_var_ord is based on order of above TRACE_VAR decls, starting at zero.
 */
extern int guk_set_trace_state(int trace_var_ord, int value);
extern int guk_get_trace_state(int trace_var_ord);

void init_trace(char *cmd_line);

/* Every use of tprintk (or ttprintk) should be preceded by a guard "if (trace_xxx())"
 * where xxx is one of the above. Other output should use the console methods,
 * printk or xprintk.
 */
void guk_tprintk(const char *fmt, ...);

/* ttprink prefixes the time, cpu number and thread to the trace */
#define guk_ttprintk(_f, _a...) \
  tprintk("%ld %d %d " _f, NOW(),  smp_processor_id(), guk_current_id(), ## _a)

#define tprintk guk_tprintk
#define ttprintk guk_ttprintk

/* If the trace is buffered, this call will flush it out
 */
void flush_trace(void);

#endif /* _TRACE_H */
