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
#ifndef __WAIT_H__
#define __WAIT_H__

#include <guk/sched.h>
#include <guk/os.h>
#include <guk/spinlock.h>

#include <list.h>
#include <lib.h>

struct wait_queue
{
    struct thread *thread;
    struct list_head thread_list;
};

struct wait_queue_head
{
    spinlock_t lock;
    struct list_head thread_list;
};

#define __WAIT_QUEUE_HEAD_INITIALIZER(name) {                           \
    .lock             = SPIN_LOCK_UNLOCKED,                             \
    .thread_list      = { &(name).thread_list, &(name).thread_list } }

#define DECLARE_WAIT_QUEUE_HEAD(name)                                   \
   struct wait_queue_head name = __WAIT_QUEUE_HEAD_INITIALIZER(name)


#define DEFINE_WAIT(name)                               \
struct wait_queue name = {                              \
    .thread       = current,                            \
    .thread_list  = LIST_HEAD_INIT((name).thread_list), \
}


static inline void init_waitqueue_head(struct wait_queue_head *h)
{
    spin_lock_init(&h->lock);
    INIT_LIST_HEAD(&h->thread_list);
}

static inline void init_waitqueue_entry(struct wait_queue *q, struct thread *thread)
{
    q->thread = thread;
}

static inline void add_wait_queue(struct wait_queue_head *h, struct wait_queue *q)
/* Must be called with the queue head lock held */
{
    if (list_empty(&q->thread_list))
        list_add_tail(&q->thread_list, &h->thread_list);
}

static inline void remove_wait_queue(struct wait_queue *q)
/* Must be called with the queue head lock held */
{
    list_del(&q->thread_list);
}


static inline void __wake_up(struct wait_queue_head *head)
{
    struct list_head *tmp, *next;

    list_for_each_safe(tmp, next, &head->thread_list)
    {
         struct wait_queue *curr;
         curr = list_entry(tmp, struct wait_queue, thread_list);
         wake(curr->thread);
    }
}

static inline void wake_up(struct wait_queue_head *head)
{
    unsigned long flags;
    spin_lock_irqsave(&head->lock, flags);
    __wake_up(head);
    spin_unlock_irqrestore(&head->lock, flags);
}
#define add_waiter(w, wq) do {                \
    unsigned long flags;                      \
    spin_lock_irqsave(&wq.lock, flags);       \
    add_wait_queue(&wq, &w);                  \
    block(current);                           \
    spin_unlock_irqrestore(&wq.lock, flags);  \
} while (0)

#define remove_waiter(w) do {   \
    unsigned long flags;        \
    local_irq_save(flags);      \
    remove_wait_queue(&w);      \
    local_irq_restore(flags);   \
} while (0)

#define wait_event(wq, condition) do{             \
    unsigned long flags;                          \
    if(condition)                                 \
        break;                                    \
    DEFINE_WAIT(__wait);                          \
    for(;;)                                       \
    {                                             \
        /* protect the list */                    \
        spin_lock_irqsave(&wq.lock, flags);       \
        if(list_empty(&__wait.thread_list))       \
            add_wait_queue(&wq, &__wait);         \
        block(current);                           \
        spin_unlock_irqrestore(&wq.lock, flags);  \
        if(condition) {                           \
	    wake(current);                        \
            break;                                \
	}                                         \
        schedule();                               \
    }                                             \
    spin_lock_irqsave(&wq.lock, flags);           \
    if(!list_empty(&__wait.thread_list))          \
        remove_wait_queue(&__wait);               \
    spin_unlock_irqrestore(&wq.lock, flags);      \
} while(0)

#endif /* __WAIT_H__ */
