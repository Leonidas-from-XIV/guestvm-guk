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
#ifndef __DB_H__
#define __DB_H__

#define DEBUG_CMDLINE   "-XX:GUKDebug"

void init_db_backend(char *cmd_line);
struct app_main_args
{
    char *cmd_line;
};

int guk_debugging(void);
void guk_set_debugging(int state);

void guk_db_exit_notify_and_wait(void);

extern unsigned long db_back_handler[3];

void jmp_db_back_handler(void* handler_addr);
int db_is_watchpoint(unsigned long addr);
int db_watchpoint_step(void);
int db_is_dbaccess_addr(unsigned long addr);
int db_is_dbaccess(void);


#endif
