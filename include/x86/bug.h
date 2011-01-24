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
#ifndef _BUG_H_
#define _BUG_H_

#include <guk/console.h>

#if __x86_64__
static inline void **get_bp(void)
{
    void **bp;
    asm ("movq %%rbp, %0" : "=r"(bp));
    return bp;
}

static inline void **get_sp(void)
{
    void **sp;
    asm ("movq %%rsp, %0" : "=r"(sp));
    return sp;
}

#else
static inline void **get_bp(void)
{
    void **bp;
    asm ("movl %%ebp, %0" : "=r"(bp));
    return bp;
}

#endif

extern void guk_crash_exit(void);
extern void guk_crash_exit_backtrace(void);
extern void guk_crash_exit_msg(char *msg);
extern void guk_ok_exit(void);

#define crash_exit guk_crash_exit
#define crash_exit_backtrace guk_crash_exit_backtrace
#define ok_exit guk_ok_exit
#define crash_exit_msg guk_crash_exit_msg

#define BUG_ON(x) do { \
    if (x) {xprintk("BUG at %s:%d\n", __FILE__, __LINE__); crash_exit_backtrace(); } \
} while (0)

#define BUG() BUG_ON(1)

#endif
