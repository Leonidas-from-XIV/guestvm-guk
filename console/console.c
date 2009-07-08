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
 * (C) 2006 - Grzegorz Milos - Cambridge University
 ****************************************************************************
 *
 *        File: console.h
 *      Author: Grzegorz Milos
 *     Changes:
 *
 *        Date: Mar 2006
 *
 * Environment: Guest VM microkernel volved from Xen Minimal OS
 * Description: Console interface.
 *
 * Handles console I/O. Defines printk.
 *
 ****************************************************************************
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
#include <guk/wait.h>
#include <guk/mm.h>
#include <guk/hypervisor.h>
#include <guk/events.h>
#include <guk/xenbus.h>
#include <guk/trace.h>
#include <guk/blk_front.h>

#include <xen/io/console.h>

#include <lib.h>
#include <types.h>

/* Copies all print output to the Xen emergency console apart
   of standard dom0 handled console */
#define USE_XEN_CONSOLE

/* If SPINNING_CONSOLE is defined guestvm-ukernel will busy-wait for Dom0 to make space
 * in the console ring. Bad for performance, good for debugging */
#define SPINNING_CONSOLE

/* Low level functions defined in xencons_ring.c */
extern int xencons_ring_init(void);
extern int xencons_ring_send(const char *data, unsigned len);
extern int xencons_ring_send_no_notify(const char *data, unsigned len);
extern int xencons_ring_avail(void);


/* If console not initialised the printk will be sent to xen (and then to serial
 * line if Xen is built with "verbose" enabled, see: Config.mk & xen/Rules.mk).
 *
 * As soon as the console is initialised the same printout will appear in Dom0
 * too (Dom0 will be notified of the presence of console data).
 */
static int console_initialised = 0;
static int console_dying = 0;
static int (*xencons_ring_send_fn)(const char *data, unsigned len) =
                    xencons_ring_send_no_notify;
static DEFINE_SPINLOCK(xencons_lock);

void xencons_rx(char *buf, unsigned len)
{
    if(len > 0)
    {
        /* Just repeat what's written */
        buf[len] = '\0';
        printk("%s", buf);

        if(buf[len-1] == '\r') {
	    print_runqueue();
	}
    }
}

void xencons_tx(void)
{
    /* Do nothing, handled by _rx */
}


/* Output data needs to be encoded by adding '\r' after each '\n' */
#define CONS_BUFF_SIZE  1024
static char *encode_printout(char *data, int *length)
{
    static char encoded[CONS_BUFF_SIZE];
    int orig_idx, enc_idx;

    for(orig_idx=0, enc_idx=0;
        (orig_idx < *length) && (enc_idx < CONS_BUFF_SIZE - 1);
        orig_idx++, enc_idx++)
    {
        BUG_ON(enc_idx >= CONS_BUFF_SIZE);
        encoded[enc_idx] = data[orig_idx];
        if(data[orig_idx] == '\n')
        {
            enc_idx++;
            BUG_ON(enc_idx >= CONS_BUFF_SIZE);
            encoded[enc_idx] = '\r';
        }
    }
    /* If the printk's are too long, print a message notyfing the user that we
     * cannot handle it */
    if(orig_idx < *length)
    {
        char *too_long = "[printk too long: increase CONS_BUFF_SIZE]\n\r";

        strcpy(encoded + enc_idx - strlen(too_long), too_long);
        BUG_ON(!((enc_idx == CONS_BUFF_SIZE - 1) ||
               (enc_idx == CONS_BUFF_SIZE)));
    }

    *length = enc_idx;

    return encoded;
}


static char *init_overflow =
    "[BUG: too much printout before console is initialised!]\n\r";
void console_print(char *data, int length)
{
    char *encoded;
    int printed;

    if(console_dying)
        return;

    encoded = encode_printout(data, &length);
    if(!console_initialised &&
        (xencons_ring_avail() - length < strlen(init_overflow)))
    {
        console_dying = 1;
#ifndef USE_XEN_CONSOLE
        (void)HYPERVISOR_console_io(CONSOLEIO_write,
                                    strlen(init_overflow),
                                    init_overflow);
#endif
        xencons_ring_send(init_overflow, strlen(init_overflow));
        BUG();
    }
#ifdef SPINNING_CONSOLE
again:
#endif
    printed = xencons_ring_send_fn(encoded, length);
#ifdef SPINNING_CONSOLE
    if(printed != length)
    {
        encoded += printed;
        length -= printed;
        goto again;
    }
#endif
}

void guk_printbytes(char *buf, int length)
{
    unsigned long flags;
    spin_lock_irqsave(&xencons_lock, flags);
    console_print(buf, length);
    spin_unlock_irqrestore(&xencons_lock, flags);
}

/* Prints string to Dom0 ring and optionally to Xen emergency console */
void guk_cprintk(int direct, const char *fmt, va_list args)
{
    static char   buf[1024];
    unsigned long flags;
    int err;

    flags = 0;
    if(!direct)
        spin_lock_irqsave(&xencons_lock, flags);
    err = vsnprintf(buf, sizeof(buf), fmt, args);

    if(direct)
    {
        (void)HYPERVISOR_console_io(CONSOLEIO_write, strlen(buf), buf);
        return;
    } else {
#ifndef USE_XEN_CONSOLE
	if(!console_initialised)
	    (void)HYPERVISOR_console_io(CONSOLEIO_write, strlen(buf), buf);
#endif
        console_print(buf, strlen(buf));
    }
    if(!direct)
        spin_unlock_irqrestore(&xencons_lock, flags);
}

void guk_printk(const char *fmt, ...)
{
    va_list       args;
    va_start(args, fmt);
    cprintk(0, fmt, args);
    va_end(args);
}

void guk_xprintk(const char *fmt, ...)
{
    va_list       args;
    va_start(args, fmt);
    cprintk(1, fmt, args);
    va_end(args);
}

void init_console(void)
{
    if (trace_startup()) tprintk("Initialising console ... ");
    xencons_ring_init();
    console_initialised = 1;
    /* We can now notify Dom0 */
    xencons_ring_send_fn = xencons_ring_send;
    /* NOTE: this printk will notify Dom0.
     * Otherwise we would have to wait till the next printk before we see all
     * previous print output in Dom0 */
    if (trace_startup()) tprintk("done.\n");
}

extern void xencons_resume(void);
extern void xencons_suspend(void);


void console_suspend(void)
{
    xencons_suspend();
}
void console_resume(void)
{
    xencons_resume();
}
