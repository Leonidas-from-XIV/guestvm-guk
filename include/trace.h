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
#ifndef _TRACE_H_
#define _TRACE_H_

/* Values that control tracing in the microkernel
 * the command line strings must match those in GuestVMVMProgramArguments.java
 * All strings are of the form -XX:GVMTrace[:subsystem], where [:subsystem] is optional
 * If no susbsystem is specified, tracing is enabled, and presumed to be switched on
 * per subsystem during execution.
 *
 * Author: Mick Jordan, Sun Microsystems, Inc..
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

/* Support for runtime enabling/disabling.
   trce_var_ord is based on order of above TRACE_VAR decls, starting at zero.
 */
extern int guk_set_trace_state(int trace_var_ord, int value);

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
