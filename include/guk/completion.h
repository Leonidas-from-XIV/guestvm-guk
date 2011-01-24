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
 * Completion interface
 *
 * Author: Harald Roeck
 *
 */

#include <guk/sched.h>
#include <guk/wait.h>
#include <guk/spinlock.h>

/* multiple threads can wait for the same completion event */
struct completion {
    int done;
    struct wait_queue_head wait;
};

#define COMPLETION_INITIALIZER(work) \
	{ 0, __WAIT_QUEUE_HEAD_INITIALIZER((work).wait)}

#define COMPLETION_INITIALIZER_ONSTACK(work) \
	({ init_completion(&work); work; })

#define DECLARE_COMPLETION(work) \
	struct completion work = COMPLETION_INITIALIZER(work)

static inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_waitqueue_head(&x->wait);
}

/* block current thread until completion is signaled */
extern void guk_wait_for_completion(struct completion *);

/* release all waiting thread */
extern void guk_complete_all(struct completion *);

/* release the first thread waiting for the complete event */
extern void guk_complete(struct completion *x);

extern struct completion *guk_create_completion(void);
extern void guk_delete_completion(struct completion *c);

#define INIT_COMPLETION(x)	((x).done = 0)

#define wait_for_completion guk_wait_for_completion
#define complete_all guk_complete_all
#define complete guk_complete
