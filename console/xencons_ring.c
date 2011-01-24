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
#include <guk/os.h>
#include <guk/wait.h>
#include <guk/mm.h>
#include <guk/hypervisor.h>
#include <guk/events.h>
#include <guk/xenbus.h>
#include <guk/smp.h>
#include <guk/wait.h>

#include <xen/io/console.h>

#include <lib.h>
#include <types.h>

DECLARE_WAIT_QUEUE_HEAD(console_queue);

static inline struct xencons_interface *xencons_interface(void)
{
    return mfn_to_virt(start_info.console.domU.mfn);
}

void xencons_notify_daemon(void)
{
    /* Use evtchn: this is called early, before irq is set up. */
    notify_remote_via_evtchn(start_info.console.domU.evtchn);
}

int xencons_ring_avail(void)
{
    struct xencons_interface *intf = xencons_interface();
    XENCONS_RING_IDX cons, prod;

    cons = intf->in_cons;
    prod = intf->in_prod;
    mb();
    BUG_ON((prod - cons) > sizeof(intf->in));

    return prod - cons;
}

int xencons_ring_send_no_notify(const char *data, unsigned len)
{
    int sent = 0;
    struct xencons_interface *intf = xencons_interface();
    XENCONS_RING_IDX cons, prod;
    cons = intf->out_cons;
    prod = intf->out_prod;
    mb();
    BUG_ON((prod - cons) > sizeof(intf->out));

    while ((sent < len) && ((prod - cons) < sizeof(intf->out)))
	intf->out[MASK_XENCONS_IDX(prod++, intf->out)] = data[sent++];

    wmb();
    intf->out_prod = prod;

    return sent;
}

int xencons_ring_send(const char *data, unsigned len)
{
    int sent;
    sent = xencons_ring_send_no_notify(data, len);
    xencons_notify_daemon();

    return sent;
}	



static void handle_input(evtchn_port_t port, void *ign)
{
        //xprintk("wake_up\n");
        wake_up(&console_queue);
}

int xencons_ring_recv(char *data, unsigned len)
{
	struct xencons_interface *intf = xencons_interface();
	XENCONS_RING_IDX cons, prod;
        unsigned filled = 0;

	cons = intf->in_cons;
	prod = intf->in_prod;
	mb();
	BUG_ON((prod - cons) > sizeof(intf->in));

        while (filled < len && cons + filled != prod) {
                data[filled] = *(intf->in + MASK_XENCONS_IDX(cons + filled, intf->in));
                filled++;
	}

	mb();
        intf->in_cons = cons + filled;

	xencons_notify_daemon();

        return filled;
}

int xencons_ring_init(void)
{
	int err;

	if (!start_info.console.domU.evtchn)
		return 0;

	err = bind_evtchn(start_info.console.domU.evtchn, 
                      ANY_CPU,
                      handle_input,
			          NULL);
	if (err <= 0) {
		xprintk("XEN console request chn bind failed %i\n", err);
		return err;
	}

	/* In case we have in-flight data after save/restore... */
	xencons_notify_daemon();

	return 0;
}

void xencons_resume(void)
{
//xprintk("xencons_resume\n");
    xencons_ring_init();
}

void xencons_suspend(void)
{
//xprintk("xencons_suspend %d\n", start_info.console.domU.evtchn);
    unbind_evtchn(start_info.console.domU.evtchn);
}
