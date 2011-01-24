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
 * Handle shutdown and suspend events
 *
 * Author: Harald Roeck
 */


#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/service.h>
#include <guk/init.h>
#include <guk/gnttab.h>
#include <guk/shutdown.h>
#include <guk/xenbus.h>
#include <guk/xmalloc.h>
#include <guk/trace.h>
#include <guk/p2m.h>
#include <guk/hypervisor.h>
#include <guk/os.h>
#include <guk/events.h>

#include <lib.h>
#define CONTROL_DIRECTORY "control"
#define CONTROL_FILE      "shutdown"
#define SHUTDOWN_PATH     CONTROL_DIRECTORY "/" CONTROL_FILE

#define SHUTDOWN_SHUTDOWN  1
#define SHUTDOWN_REBOOT    2
#define SHUTDOWN_SUSPEND   3
#define WATCH_TOKEN        "shutdown"

static int check_shutdown(void)
{
    char *path;
    int retval;
    char *value;
    char *err;
    xenbus_transaction_t xbt;
    int retry;

    path = xenbus_read_watch(WATCH_TOKEN);

    err = xenbus_read(XBT_NIL, path, &value);
    if (err) {
	if (trace_startup())
	    tprintk("WARNING: %s %d problems reading xenbus: %s\n", __FILE__, __LINE__, err);
	free(err);
	return 0;
    }
    retval = 0;

    if (value != NULL) {
	if (strcmp(value, "poweroff") == 0 || strcmp(value, "halt") == 0)
	    retval =  SHUTDOWN_SHUTDOWN;
	else if (strcmp(value, "suspend") == 0)
	    retval =  SHUTDOWN_SUSPEND;
	else if (strcmp(value, "reboot") == 0)
	    retval =  SHUTDOWN_REBOOT;
	else {
	    if (trace_startup())
		tprintk("ERROR: unknown shutdown value %s\n", value);
	}
	free(value);
    }

    if(retval) {
again:
	value = xenbus_transaction_start(&xbt);
	if (!value) {
	    xenbus_write(xbt, path, "");
	    err = xenbus_transaction_end(xbt, 0, &retry);
	    if(retry)
		goto again;
	    else if (err)
		free(err);
	}
    }
    free(path);
    return retval;
}


static void do_shutdown(void)
{
    shutdown_services();

    for( ;; ) {
        struct sched_shutdown sched_shutdown = { .reason = SHUTDOWN_poweroff };
        HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
    }
}

static void do_suspend(void)
{
    int err;
    unsigned long start_info_mfn;
    long long now;

    /* get the start info mfn, used as argument to the suspend hypercall */
    start_info_mfn = virt_to_mfn(xen_info);

    /* suspend all services; each service should release its grant references and event channels */
    suspend_services();

    /* now we can safely suspend xenbus, the event channel mechanism */
    xenbus_suspend();

    /* on return we run on CPU 0 and all other CPUs are down, we need the event channels
     * to notify the other cpus */
    smp_suspend();

    preempt_disable();
    evtchn_suspend();

    /* suspend grant table mechanism, subsystems that used grant pages should have released
     * their grant references already */
    gnttab_suspend();

    BUG_ON(smp_processor_id() != 0);

    local_irq_disable();
    time_suspend();

    console_suspend();

    /* remember the time when we suspended */
    now = NOW();

    /* unmap shared_info page, and map it to the original shared info */
    BUG_ON(HYPERVISOR_update_va_mapping(
		(uintptr_t)HYPERVISOR_shared_info,
		__pte((virt_to_mfn(shared_info)<<L1_PAGETABLE_SHIFT)| L1_PROT), UVMF_INVLPG));

    /* translate last mfns to pfns */
    xen_info->store_mfn = machine_to_phys_mapping[xen_info->store_mfn];
    xen_info->console.domU.mfn = machine_to_phys_mapping[xen_info->console.domU.mfn];

    if (trace_startup())
	tprintk("suspend at %lu\n", now);

    /***************** suspend Guest ****************/
    /* call blocks until a restore is initiated from the user */
    err = HYPERVISOR_suspend((unsigned long)start_info_mfn);
    BUG_ON(err);

    /* restore shared_info mapping */
    BUG_ON(HYPERVISOR_update_va_mapping((uintptr_t)shared_info, __pte(xen_info->shared_info | 7), UVMF_INVLPG));

    HYPERVISOR_shared_info = (shared_info_t *)shared_info;

    if (trace_startup())
	tprintk("suspended at %ld returned at %ld (delta %ld) with state %d\n",
	    now, NOW(), NOW() - now, err);

    /* copy the new start info to our safe place */
    memcpy(&start_info, xen_info, sizeof(*xen_info));

    /* resume the suspended subsystems in reverse order */
    console_resume();

    time_resume();

    local_irq_enable();

    /* rebuild the pseudo physical page to machine page mapping
     * note: there is no matching suspend call
     */
    arch_rebuild_p2m();

    gnttab_resume();

    evtchn_resume();

    preempt_enable();
    /* restart previously suspended CPUs */
    smp_resume();

    xenbus_resume();
    /* resume services after xenbus, since most of them use xenbus to get to their backends */
    resume_services();

}

static void do_reboot(void)
{
    for( ;; ) {
        struct sched_shutdown sched_shutdown = { .reason = SHUTDOWN_reboot };
        HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
    }
}

static void shutdown_thread(void *args)
{
    int shutdown;
    xenbus_watch_path(XBT_NIL, CONTROL_DIRECTORY"/"CONTROL_FILE, WATCH_TOKEN);
    for(;;) {
	shutdown = check_shutdown();
	switch(shutdown) {
	    case SHUTDOWN_SHUTDOWN:
		if (trace_startup())
		    tprintk("got shutdown event\n");
		do_shutdown();
		break;
	    case SHUTDOWN_SUSPEND:
		if (trace_startup())
		    tprintk("got suspend event\n");
		do_suspend();
		break;
	    case SHUTDOWN_REBOOT:
		if (trace_startup())
		    tprintk("got reboot event\n");
		do_reboot();
		break;
	    default:
		if (trace_startup())
		    tprintk("unknown shutdown state %d\n", shutdown);
	}
    }
}

static int init_shutdown(void)
{
    create_thread("shutdown_daemon", shutdown_thread, UKERNEL_FLAG, NULL);
    return 0;
}
DECLARE_INIT(init_shutdown);
