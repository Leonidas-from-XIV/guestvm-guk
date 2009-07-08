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
#ifndef __WAIT_H__
#define __WAIT_H__

#include <guk/sched.h>
#include <guk/os.h>

#include <spinlock.h>
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
