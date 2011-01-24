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
#ifndef __DB_H__
#define __DB_H__

#define DEBUG_CMDLINE   "-XX:GUKDebug"
#define DEBUG_DB   "-XX:GUKDebugDB"
#define DEBUG_XG   "-XX:GUKDebugXG"

struct app_main_args
{
    char *cmd_line;
    start_info_t *si_info;
};

void init_db_backend(struct app_main_args *aargs);
int guk_debugging(void);
int guk_db_debugging(void);
int guk_xg_debugging(void);
void guk_set_debugging(char *cmd_line);
void guk_crash_to_debugger(void);

void guk_db_exit_notify_and_wait(void);

extern unsigned long db_back_handler[3];

void jmp_db_back_handler(void* handler_addr);
int db_is_watchpoint(unsigned long addr, struct pt_regs *regs);
int db_watchpoint_step(struct pt_regs *regs);
int db_is_dbaccess_addr(unsigned long addr);
int db_is_dbaccess(void);

#endif
