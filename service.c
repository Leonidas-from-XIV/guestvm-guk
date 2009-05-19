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
/*
 * service/driver framework
 *
 * Author: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 * Changes: Mick Jordan, Sun Microsystems, Inc..
 */


#include <service.h>
#include <os.h>
#include <list.h>
#include <sched.h>
#include <trace.h>

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
