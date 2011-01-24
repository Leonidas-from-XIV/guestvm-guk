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
 *        File: events.c
 *      Author: Rolf Neugebauer
 *     Changes: Grzegorz Milos
 *              Mick Jordan
 *
 *        Date: Jul 2003, changes Jun 2005, 2007-9
 *
 * Environment: Xen Minimal OS
 * Description: Deals with events recieved on event channels
 *
 ****************************************************************************
 */

#include <guk/os.h>
#include <guk/mm.h>
#include <guk/hypervisor.h>
#include <guk/events.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/smp.h>

#include <lib.h>
#define NR_EVS 1024

/* this represents a event handler. Chaining or sharing is not allowed */
typedef struct _ev_action_t {
    int cpu;
    evtchn_handler_t handler;
    void *data;
    u32 count;
#ifdef EVTCHN_DEBUG
    u32 ignored;
#endif
} ev_action_t;

static ev_action_t ev_actions[NR_EVS];
static void default_handler(evtchn_port_t port, void *data);

static unsigned long bound_ports[NR_EVS/(8*sizeof(unsigned long))];

struct virq_info {
    int used;
    uint32_t cpu;
    evtchn_handler_t handler;
    void *data;
    int evtchn[MAX_VIRT_CPUS];
};

static struct virq_info virq_table[NR_VIRQS];

void unbind_all_ports(void)
{
    int i;
    for (i = 0; i < NR_EVS; i++) {
	if (test_and_clear_bit(i, bound_ports)) {
	    struct evtchn_close close;
	    mask_evtchn(i);
	    close.port = i;

	    HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);
	    unbind_evtchn(i);
	}
    }
}

/*
 * Demux events to different handlers.
 */
void do_event(evtchn_port_t port)
{
    ev_action_t  *action;
    int cpu = smp_processor_id();

    add_preempt_count(current, IRQ_ACTIVE);
    mask_evtchn(port);
    clear_evtchn(port);
    if (port >= NR_EVS) {
	printk("Port number too large: %d\n", port);
	goto out;
    }

    action = &ev_actions[port];
    action->count++;
    if((action->cpu != ANY_CPU) && (action->cpu != cpu)) {
#ifdef EVTCHN_DEBUG
	action->ignored++;
	if(action->ignored % 100 == 1) {
	    printk("%d events on port %d, %d ignored\n",
		    action->count, port, action->ignored);
	}
#endif
	goto out;
    }
    //xprintk("DEe %d %x %d\n", this_cpu(current_thread)->id, this_cpu(current_thread)->preempt_count, port);
    //print_backtrace(current);
    /* call the handler */
    if(action->handler)
	action->handler(port, action->data);

out:
    unmask_evtchn(port);
    sub_preempt_count(current, IRQ_ACTIVE);
    //xprintk("DEx %d %x %d\n", this_cpu(current_thread)->id, this_cpu(current_thread)->preempt_count, port);
}

evtchn_port_t bind_evtchn(evtchn_port_t port,
	int cpu,
	evtchn_handler_t handler,
	void *data)
{
    if(ev_actions[port].handler != default_handler)
	xprintk("WARN: Handler for port %d already registered, replacing\n",
		port);
    ev_actions[port].data = data;
    ev_actions[port].cpu = cpu;
    ev_actions[port].count = 0;
#ifdef EVTCHN_DEBUG
    ev_actions[port].ignored = 0;
    printk("Binding port=%d, cpu=%d, handler %p\n", port, cpu, handler);
#endif
    wmb();
    ev_actions[port].handler = handler;

    /* Finally unmask the port */
    unmask_evtchn(port);

    return port;
}

void unbind_evtchn(evtchn_port_t port)
{
	if (ev_actions[port].handler == default_handler)
		xprintk("WARN: No handler for port %d when unbinding\n", port);
	ev_actions[port].handler = default_handler;
	wmb();
	ev_actions[port].data = NULL;
}

int bind_virq(uint32_t virq,
              int cpu,
              evtchn_handler_t handler,
              void *data)
{
    evtchn_bind_virq_t op;

    /* Try to bind the virq to a port */
    op.virq = virq;
    op.vcpu = smp_processor_id();
    if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq, &op) != 0) {
	xprintk("Failed to bind virtual IRQ %d\n", virq);
	BUG();
    }
    set_bit(op.port, bound_ports);
    bind_evtchn(op.port, cpu, handler, data);
    return 0;
}

/*
 * Initially all events are without a handler and disabled
 */
void init_events(void)
{
    int i;

    /* inintialise event handler */
    for ( i = 0; i < NR_EVS; i++ ) {
        ev_actions[i].handler = default_handler;
        mask_evtchn(i);
    }

    for (i=0; i< NR_VIRQS; ++i)
	virq_table[i].used = 0;

}

static void default_handler(evtchn_port_t port, void *ignore)
{
    printk("[Port %d] - event received\n", port);
}

/* Create a port available to the pal for exchanging notifications.
   Returns the result of the hypervisor call. */

/* Unfortunate confusion of terminology: the port is unbound as far
   as Xen is concerned, but we automatically bind a handler to it
   from inside guk. */

int evtchn_alloc_unbound(domid_t pal, evtchn_handler_t handler, int cpu,
						 void *data, evtchn_port_t *port)
{
    evtchn_alloc_unbound_t op;
    op.dom = DOMID_SELF;
    op.remote_dom = pal;
    int err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &op);
    if (err)
		return err;
    *port = bind_evtchn(op.port, cpu, handler, data);
    return err;
}

/* Connect to a port so as to allow the exchange of notifications with
   the pal. Returns the result of the hypervisor call. */

int evtchn_bind_interdomain(domid_t pal, evtchn_port_t remote_port,
			                evtchn_handler_t handler, int cpu, void *data,
			                evtchn_port_t *local_port)
{
    evtchn_bind_interdomain_t op;
    op.remote_dom = pal;
    op.remote_port = remote_port;
    int err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain, &op);
    if (err)
		return err;
    set_bit(op.local_port, bound_ports);
	evtchn_port_t port = op.local_port;
    clear_evtchn(port);	      /* Without, handler gets invoked now! */
    *local_port = bind_evtchn(port, cpu, handler, data);
    return err;
}

extern void timer_handler(evtchn_port_t ev, void *ign);
void evtchn_resume(void)
{
    bind_virq(VIRQ_TIMER, smp_processor_id(), timer_handler, NULL);
}

void evtchn_suspend(void)
{
    unbind_all_ports();
}

evtchn_port_t evtchn_alloc_ipi(evtchn_handler_t handler, int cpu, void *data)
{
    struct evtchn_bind_ipi bind_ipi;

    bind_ipi.vcpu = cpu;
    BUG_ON(HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi, &bind_ipi));

    set_bit(bind_ipi.port, bound_ports);
    bind_evtchn(bind_ipi.port, cpu, handler, data);

    return bind_ipi.port;
}

void evtchn_bind_to_cpu(int port, int cpu)
{
    int err;
    struct evtchn_bind_vcpu op;
    op.port = port;
    op.vcpu = cpu;
    err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_vcpu, &op);
//    if (err) {
	xprintk("error in bind_vcpu %d\n", err);
  //  }
}
