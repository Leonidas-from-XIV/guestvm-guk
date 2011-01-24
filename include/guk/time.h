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
/*
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: time.h
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: Grzegorz Milos (gm281@cam.ac.uk)
 *              Robert Kaiser (kaiser@informatik.fh-wiesbaden.de)
 *              
 *        Date: Jul 2003, changes: Jun 2005, Sep 2006
 * 
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: Time and timer functions
 *
 ****************************************************************************
 */

#ifndef _TIME_H_
#define _TIME_H_



struct shadow_time_info {
	u64 tsc_timestamp;     /* TSC at last update of time vals.  */
	u64 system_timestamp;  /* Time, in nanosecs, since boot.    */
	u32 tsc_to_nsec_mul;
	u32 tsc_to_usec_mul;
	int tsc_shift;
	u32 version;
};

/*
 * System Time
 * 64 bit value containing the nanoseconds elapsed since boot time.
 * This value is adjusted by frequency drift.
 * NOW() returns the current time.
 * The other macros are for convenience to approximate short intervals
 * of real time into system time 
 */
typedef s64 s_time_t;
extern s64 guk_time_addend;

#define NOW()                   ((s_time_t)monotonic_clock() + time_addend)
#define SECONDS(_s)             (((s_time_t)(_s))  * 1000000000UL )
#define TENTHS(_ts)             (((s_time_t)(_ts)) * 100000000UL )
#define HUNDREDTHS(_hs)         (((s_time_t)(_hs)) * 10000000UL )
#define MILLISECS(_ms)          (((s_time_t)(_ms)) * 1000000UL )
#define MICROSECS(_us)          (((s_time_t)(_us)) * 1000UL )
#define Time_Max                ((s_time_t) 0x7fffffffffffffffLL)
#define FOREVER                 Time_Max
#define NSEC_TO_USEC(_nsec)     ((_nsec) / 1000UL)
#define NSEC_TO_SEC(_nsec)      ((_nsec) / 1000000000ULL)

/* wall clock time  */
#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL
typedef long time_t;
typedef long suseconds_t;
struct timeval {
	time_t		tv_sec;		/* seconds */
	suseconds_t	tv_usec;	/* microseconds */
};
#endif


struct timespec {
    time_t      ts_sec;
    long        ts_nsec;
};

typedef int clockid_t;

/* prototypes */
void     init_time(void);
u64      guk_monotonic_clock(void);
void     guk_gettimeofday(struct timeval *tv);
void     block_domain(s_time_t until);
void     check_need_resched(void);
u64      guk_get_cpu_running_time(int cpu);
void     set_timer_interrupt(u64 delta);

void     time_suspend(void);
void     time_resume(void);

#define monotonic_clock guk_monotonic_clock
#define time_addend guk_time_addend
#define gettimeofday guk_gettimeofday
#define get_running_time() guk_get_cpu_running_time(smp_processor_id())

#endif /* _TIME_H_ */
