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
 * service/driver framework
 *
 * Author: Harald Roeck
 * Changes: Mick Jordan
 */


#include <guk/service.h>
#include <guk/os.h>
#include <guk/sched.h>
#include <guk/trace.h>

#include <list.h>
static LIST_HEAD(services);

#define SERV_STOP    0
#define SERV_STARTED 1
#define SERV_SUSPEND 2
#define SERV_RESUME  3
static int services_state = SERV_STOP;


void register_service(struct service *s)
{
    BUG_ON(services_state != SERV_STOP);
    list_add(&s->list, &services);
}

void start_services(void)
{
    struct list_head *next;
    struct service *s;
    services_state = SERV_STARTED;

    if(trace_service())
	tprintk("start services ...\n");
    list_for_each(next, &services) {
	s = list_entry(next, struct service, list);

	if(trace_service())
	    tprintk("start %s\n", s->name);
	s->init(s->arg);
    }
}

int suspend_services(void)
{
    struct list_head *next;
    struct service *s;
    int retval;

    services_state = SERV_SUSPEND;
    if(trace_service())
	tprintk("suspend services ...\n");
    retval = 0;
    list_for_each(next, &services) {
	s = list_entry(next, struct service, list);

	if (trace_service())
	    tprintk("suspend %s\n", s->name);
	retval += s->suspend();
	if (trace_service())
	    tprintk("suspended %s\n", s->name);
    }

    if (retval)
	tprintk("%s %d WARNING: services are not suspended correctly\n", __FILE__, __LINE__);
    return retval;
}

int resume_services(void)
{
    struct list_head *next;
    struct service *s;
    services_state = SERV_RESUME;
    int retval;

    if (trace_service())
	tprintk("resume services ...\n");
    retval = 0;
    list_for_each(next, &services) {
	s = list_entry(next, struct service, list);

	if (trace_service())
	    tprintk("resume %s\n", s->name);
	retval += s->resume();
	if (trace_service())
	    tprintk("resumed %s\n", s->name);
    }
    services_state = SERV_STARTED;
    return retval;
}


int shutdown_services(void)
{
    struct list_head *next;
    struct service *s;
    int retval;

    if (trace_service())
	tprintk("shutdown services ...\n");
    retval = 0;
    list_for_each(next, &services) {
	s = list_entry(next, struct service, list);

	if (trace_service())
	    tprintk("shutdown %s\n", s->name);
	retval += s->shutdown();
    }
    services_state = SERV_STOP;
    return retval;
}
