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
 * Completion interface
 *
 * Author: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
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
