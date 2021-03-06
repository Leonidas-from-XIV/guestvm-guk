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
 * hypervisor.c
 *
 * Communication to/from hypervisor.
 *
 * Copyright (c) 2002-2003, K A Fraser
 * Copyright (c) 2005, Grzegorz Milos, gm281@cam.ac.uk,Intel Research Cambridge
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

#include <guk/os.h>
#include <guk/hypervisor.h>
#include <guk/events.h>
#include <guk/sched.h>
#include <guk/smp.h>

#define active_evtchns(idx)              \
    (HYPERVISOR_shared_info->evtchn_pending[idx] &                \
     ~HYPERVISOR_shared_info->evtchn_mask[idx])

void do_hypervisor_callback(void)
{
    u32 	       l1, l2;
    unsigned int   l1i, l2i, port;
    int            cpu, count;
    vcpu_info_t   *vcpu_info;

    cpu = smp_processor_id();
    vcpu_info = &HYPERVISOR_shared_info->vcpu_info[cpu];
//    BUG_ON(vcpu_info->evtchn_upcall_mask == 0);
//    BUG_ON(current->tmp == 1);
    do {
        vcpu_info->evtchn_upcall_pending = 0;
        /* Nested invocations bail immediately. */
        if (unlikely(this_cpu(upcall_count)++))
                return;
        /* NB. No need for a barrier here -- XCHG is a barrier on x86. */
        l1 = xchg(&vcpu_info->evtchn_pending_sel, 0);
        while ( l1 != 0 )
        {
            l1i = __ffs(l1);
            l1 &= ~(1 << l1i);

            while ( (l2 = active_evtchns(l1i)) != 0 )
            {
                l2i = __ffs(l2);
                l2 &= ~(1 << l2i);

                port = (l1i << 5) + l2i;
		do_event(port);
            }
        }
        count = this_cpu(upcall_count);
        this_cpu(upcall_count) = 0;
        if(count != 1)
            printk("=========> WARN: untested: event set during IRQ.\n");
    } while (count != 1);
}


inline void mask_evtchn(u32 port)
{
    shared_info_t *s = HYPERVISOR_shared_info;
    synch_set_bit(port, &s->evtchn_mask[0]);
}

inline void unmask_evtchn(u32 port)
{
    shared_info_t *s = HYPERVISOR_shared_info;
    vcpu_info_t *vcpu_info = &s->vcpu_info[smp_processor_id()];

    synch_clear_bit(port, &s->evtchn_mask[0]);

    /*
     * The following is basically the equivalent of 'hw_resend_irq'. Just like
     * a real IO-APIC we 'lose the interrupt edge' if the channel is masked.
     */
    if (  synch_test_bit        (port,    &s->evtchn_pending[0]) &&
         !synch_test_and_set_bit(port>>5, &vcpu_info->evtchn_pending_sel) )
    {
        vcpu_info->evtchn_upcall_pending = 1;
        if ( !vcpu_info->evtchn_upcall_mask )
            force_evtchn_callback();
    }
}

inline void clear_evtchn(u32 port)
{
    shared_info_t *s = HYPERVISOR_shared_info;
    synch_clear_bit(port, &s->evtchn_pending[0]);
}
