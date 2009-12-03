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
