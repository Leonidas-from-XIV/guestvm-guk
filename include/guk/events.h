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
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Reseach Cambridge
 ****************************************************************************
 *
 *        File: events.h
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: Grzegorz Milos (gm281@cam.ac.uk)
 *              
 *        Date: Jul 2003, changes Jun 2005
 * 
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: Deals with events on the event channels
 *
 ****************************************************************************
 */

#ifndef _EVENTS_H_
#define _EVENTS_H_

#include <guk/traps.h>
#include <xen/event_channel.h>

void init_events(void);
void evtchn_suspend(void);
void evtchn_resume(void);

typedef void (*evtchn_handler_t)(evtchn_port_t, void *);

/* prototypes */
void do_event(evtchn_port_t port);

int bind_virq(uint32_t virq, 
              int cpu, 
              evtchn_handler_t handler, 
              void *data);

evtchn_port_t bind_evtchn(evtchn_port_t port, 
                          int cpu,
                          evtchn_handler_t handler,
			  void *data);
void unbind_evtchn(evtchn_port_t port);

int evtchn_alloc_unbound(domid_t pal, 
                         evtchn_handler_t handler,
                         int cpu,
			 void *data, 
                         evtchn_port_t *port);
int evtchn_bind_interdomain(domid_t pal, 
                            evtchn_port_t remote_port,
				evtchn_handler_t handler, 
                            int cpu,
                            void *data,
							evtchn_port_t *local_port);
evtchn_port_t evtchn_alloc_ipi(evtchn_handler_t handler, int cpu, void *data);
void unbind_all_ports(void);

void evtchn_bind_to_cpu(int port, int cpu);

static inline int notify_remote_via_evtchn(evtchn_port_t port)
{
    evtchn_send_t op;
    op.port = port;
    return HYPERVISOR_event_channel_op(EVTCHNOP_send, &op);
}

#endif /* _EVENTS_H_ */
