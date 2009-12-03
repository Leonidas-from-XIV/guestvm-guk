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
#include <guk/sched.h>
#include <guk/arch_sched.h>

#include <xen/io/console.h>

#include <lib.h>
#include <types.h>

/* Low level functions defined in xencons_ring.c */
extern int xencons_ring_init(void);
extern int xencons_ring_send(const char *data, unsigned len);
extern int xencons_ring_send_no_notify(const char *data, unsigned len);
extern int xencons_ring_avail(void);
extern struct wait_queue_head console_queue;
extern int xencons_ring_recv(char *data, unsigned len);
extern void xencons_notify_daemon(void);

static int console_initialised = 0;
static int console_dying = 0;
static int (*xencons_ring_send_fn)(const char *data, unsigned len) =
                    xencons_ring_send_no_notify;
static DEFINE_SPINLOCK(xencons_lock);

static char *init_overflow =
    "[BUG: too much printout before console is initialised!]\r\n";

/* We really don't want to lost any console output, so we busy wait
   if the ring is full. No doubt we could, with more effort, arrange to
   wait for an event indicating that the ring had space, but does it matter?
*/
static void checked_print(char *data, int length) {
    int printed = 0;

    while (length > 0) {
      printed = xencons_ring_send_fn(data, length);
      data += printed;
      length -= printed;
    }
}

/* Prints to the dom0 console. */
static void console_print(char *data, int length)
{
    char *curr_char, saved_char;
    int part_len;

    if (console_dying) {
        /* suppress further output */
        return;
    }

    /* If the console is not initialized and the ring is full we crash */
    if(!console_initialised &&
        (xencons_ring_avail() - length < strlen(init_overflow)))
    {
        console_dying = 1;
        (void)HYPERVISOR_console_io(CONSOLEIO_write,
                                    strlen(init_overflow),
                                    init_overflow);
        xencons_ring_send(init_overflow, strlen(init_overflow));
        BUG();
    }

    /* Evidently we do need to convert \n into \r\n, although it seems only needed
       for some terminal environments. Here we search for embedded \n chars, leaving
       any trailing \n until later.
     */
    for(curr_char = data; curr_char < data+length-1; curr_char++) {
        if (*curr_char == '\n') {
            saved_char = *(curr_char+1);
	    *curr_char = '\r';
            *(curr_char+1) = '\n';
            part_len = curr_char - data + 2;
            checked_print(data, part_len);
            *(curr_char+1) = saved_char;
	    *curr_char = '\n';
            data = curr_char+1;
            length -= part_len - 1;
        }
    }
    
    /* At this point, either there we no embedded \n chars or we have printed
     * everything except the characters after the last \n we found. Note, therefore,
     * that there still may be a trailing \n that has to be dealth with.
     */

    if(data[length-1] == '\n') {
      checked_print(data, length - 1);
      checked_print("\r\n", 2);
    } else {
      checked_print(data, length);
    }

}

/* Atomic print to dom0 console. */
void guk_printbytes(char *buf, int length)
{
    unsigned long flags;
    spin_lock_irqsave(&xencons_lock, flags);
    console_print(buf, length);
    spin_unlock_irqrestore(&xencons_lock, flags);
}

/* Prints either to the hypervisor console or the dom0 console,
 * depending on tohyp. Output to dom0 console is atomic.
 */
void guk_cprintk(int tohyp, const char *fmt, va_list args)
{
    static char   buf[1024];
    unsigned long flags;
    int err;

    flags = 0;
    if (!tohyp) {
        spin_lock_irqsave(&xencons_lock, flags);
    }
    err = vsnprintf(buf, sizeof(buf), fmt, args);

    if (tohyp) {
        (void)HYPERVISOR_console_io(CONSOLEIO_write, strlen(buf), buf);
    } else {
        console_print(buf, strlen(buf));
    }

    if(!tohyp) {
        spin_unlock_irqrestore(&xencons_lock, flags);
    }
}

/* Prints to the dom0 console */
void guk_printk(const char *fmt, ...)
{
    va_list       args;
    va_start(args, fmt);
    guk_cprintk(0, fmt, args);
    va_end(args);
}

/* Prints to the hypervsior console */
void guk_xprintk(const char *fmt, ...)
{
    va_list       args;
    va_start(args, fmt);
    cprintk(1, fmt, args);
    va_end(args);
}

int guk_console_readbytes(char *buf, unsigned len) {
    unsigned long flags;
    int result;
    spin_lock_irqsave(&xencons_lock, flags);
    //xprintk("guk_getbytes\n");
    if (xencons_ring_avail()) {
      //xprintk("xencons_ring_avail=true\n");
      result = xencons_ring_recv(buf, len);
      spin_unlock_irqrestore(&xencons_lock, flags);
    } else {
      spin_unlock_irqrestore(&xencons_lock, flags);
      //xprintk("waiting\n");
      DEFINE_WAIT(w);
      /* this calls block(), so schedule another thread and wait for input */
      add_waiter(w, console_queue);
      /* evidently (typically one) spurious wakeup with no data can happen, hence the loop */
      while (1) {
        schedule();
        /* now read the data off the ring */
        spin_lock_irqsave(&xencons_lock, flags);
        result = xencons_ring_recv(buf, len);
        spin_unlock_irqrestore(&xencons_lock, flags);
        //xprintk("read %d\n", result);
        if (result) break;
        block(current);
      }
      remove_waiter(w);
    }
    return result;
}

void init_console(void)
{
    if (trace_startup()) tprintk("Initialising console ... ");
    xencons_ring_init();
    console_initialised = 1;
    /* switch over to notifying send */
    xencons_ring_send_fn = xencons_ring_send;
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
