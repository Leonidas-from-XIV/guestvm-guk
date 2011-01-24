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
