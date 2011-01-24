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
/******************************************************************************
 * kernel.c
 *
 * Assorted startup goes here, including the initial C entry point, jumped at
 * from head.S.
 *
 * Copyright (c) 2002-2003, K A Fraser & R Neugebauer
 * Copyright (c) 2005, Grzegorz Milos, Intel Research Cambridge
 * Copyright (c) 2006, Robert Kaiser, FH Wiesbaden
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* 
 * Changes: Grzegorz Milos
 *          Harald Roeck
 *          Mick Jordan
*/

#include <guk/os.h>
#include <guk/init.h>
#include <guk/service.h>
#include <guk/hypervisor.h>
#include <guk/mm.h>
#include <guk/events.h>
#include <guk/time.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/smp.h>
#include <guk/xenbus.h>
#include <guk/shutdown.h>
#include <guk/gnttab.h>
#include <guk/db.h>
#include <guk/trace.h>
#include <xen/features.h>
#include <xen/version.h>

#include <types.h>
#include <lib.h>

uint8_t xen_features[XENFEAT_NR_SUBMAPS * 32];

void setup_xen_features(void)
{
    xen_feature_info_t fi;
    int i, j;

    for (i = 0; i < XENFEAT_NR_SUBMAPS; i++)
    {
        fi.submap_idx = i;
        if (HYPERVISOR_xen_version(XENVER_get_features, &fi) < 0)
            break;

        for (j=0; j<32; j++)
            xen_features[i*32+j] = !!(fi.submap & 1<<j);
    }
}

void test_xenbus(void);


static void db_thread(void *data)
{
    init_db_backend((struct app_main_args *)data);
}

static void start_db_debugging(struct app_main_args *aargs)
{
  create_debug_thread("db-backend", db_thread, aargs);
}

static USED unsigned long get_r14(void)
{
    unsigned long r14;
    __asm__ __volatile__ ("movq %%r14, %0\n\t"
	    : "=r" (r14)
	    :
	    );
    return r14;
}

static void periodic_thread(void *unused)
{
    for(;;) {
	xprintk("time: %ld %s\n", NOW(), __FUNCTION__);
	printk("time: %ld %s\n", NOW(), __FUNCTION__);
	//printk("r14 %p; local_space %p\n", get_r14(), fake_local_space);
	sleep(500);
    }
}

/* This should be overridden by the application we are linked against. */
__attribute__((weak)) int guk_app_main(struct app_main_args *aargs)
{
    printk("Dummy main: command_line=%s, in debug_mode=%d\n",
            aargs->cmd_line, guk_debugging());
    create_thread("periodic-thread", periodic_thread, UKERNEL_FLAG, NULL);
    return 0;
}

static void remove_page_atva0(void)
{
    if (guk_unmap_page(0))
	printk("va_mapping failed\n");

    if (trace_startup()) tprintk("page at va 0 unmapped\n");
}

/*
 * call all init functions declared by DECLARE_INIT
 */
static void invoke_inits(void)
{
    int i, num_inits;
    num_inits = __init_end - __init_start;

    for (i=0; i < num_inits; ++i) {
	__init_start[i]();
    }
}

static void run_main(struct app_main_args *args)
{
  if (1/*trace_startup()*/) {
    /*t*/printk("Starting app_main for Guest VM microkernel, cmd_line=%s, debug_mode=%d\n",
		args->cmd_line, guk_debugging());
  }
  guk_app_main(args);
}

/*
 * INITIAL C ENTRY POINT.
 */
void start_kernel(start_info_t *si)
{
    static char hello[] = "Guest VM microkernel bootstrapping...\n";
    struct app_main_args aargs;

    (void)HYPERVISOR_console_io(CONSOLEIO_write, strlen(hello), hello);

    arch_init(si);
    /* printk can be used from here on */
    init_initial_context();
    trap_init();

    init_trace((char *)si->cmd_line);

    if (trace_startup())
    {
        /* print out some useful information  */
        tprintk("Guest VM microkernel\n");
        tprintk("start_info:   %p\n",    si);
        tprintk("  nr_pages:   %lu",     si->nr_pages);
        tprintk("  shared_inf: %08lx\n", si->shared_info);
        tprintk("  pt_base:    %p",      (void *)si->pt_base);
        tprintk("  mod_start:  0x%lx\n", si->mod_start);
        tprintk("  mod_len:    %lu\n",   si->mod_len);
        tprintk("  flags:      0x%x\n",  (unsigned int)si->flags);
        tprintk("  cmd_line:   %s\n",
               si->cmd_line ? (const char *)si->cmd_line : "NULL");

    }

    /* Set up events. */
    init_events();

    arch_print_info();

    setup_xen_features();

    /* Init memory management. */
    init_mm((char *)si->cmd_line);

    /* Init time and timers. */
    init_time();

    /* Init the console driver. */
    init_console();

    /* Init grant tables */
    init_gnttab();

    /* Init scheduler. */
    init_sched((char *)si->cmd_line);

    /* Init other CPUs */
    init_smp();

    /* Init XenBus */
    init_xenbus();

    remove_page_atva0();

    invoke_inits();

    start_services();

    guk_set_debugging((char *)si->cmd_line);
    /* Call app_main, but only if we aren't in the debug mode */
    aargs.cmd_line = (char *)si->cmd_line;
    aargs.si_info = si;
    if (guk_db_debugging()) {
	start_db_debugging(&aargs);
    } else {
        run_main(&aargs);
    }

    /* Everything initialised, start idle thread */
    run_idle_thread();
}

/*
 * crash_exit: 
 * This will generally be because the system has got itself into
 * a bad state. It must be killed, but if debugging, try debugger first.
 */
static int crashing = 0;
void crash_exit_msg(char *msg) {
    // System is unstable, so just hypervisor console IO message
    if (crashing == 0) {
      crashing = 1;
      xprintk("crash_exit: %s\n", msg);
      flush_trace();
      if (guk_debugging()) {
        // force a breakpoint
        guk_crash_to_debugger();
      }
    } else {
      xprintk("recursive entry to crash_exit\n");
      // recursive or concurrent crash
      if (crashing == 2) {
          // shutdown already initiated (by another cpu)
          while (1) {
	    cpu_relax();
	  }
      }
    }

    crashing = 2;
    for( ;; ) {
        struct sched_shutdown sched_shutdown = { .reason = SHUTDOWN_crash };
        HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
    }
}

void crash_exit(void) {
  crash_exit_msg("no message");
}

#define print_backtrace(thread) \
    if(thread) { \
	void *ip;\
	void **bp;\
	xprintk("Current Thread: %s, %d, CPU=%d\n", thread->name, thread->id, thread->cpu);\
	bp = get_bp(); \
	ip = *bp; \
        dump_sp((unsigned long*)get_sp(), xprintk); \
	backtrace(bp, 0); \
    }

void crash_exit_backtrace(void) {
	print_backtrace(current);
	crash_exit();
}

void ok_exit(void) {
    if (trace_startup()) tprintk("Guest VM microkernel, ok exit\n");

    flush_trace();
    guk_db_exit_notify_and_wait();
    for( ;; ) {
        struct sched_shutdown sched_shutdown = { .reason = SHUTDOWN_poweroff };
        HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
    }
}

int num_option(char *cmd_line, char *option) {
    int result = -1;
    char *tsarg = strstr(cmd_line, option);
    if (tsarg != NULL) {
      tsarg += strlen(option);
      if (*tsarg == '=') {
        tsarg++;
	result = 0;
        while (*tsarg != ' ' && *tsarg != 0) {
	  result = result * 10 + (*tsarg++ - '0');
	}
      }
    }
    return result;
}

int guk_domain_id(void) {
  return xenbus_get_self_id();
}
