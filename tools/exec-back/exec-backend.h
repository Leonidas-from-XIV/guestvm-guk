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
 * xenbus interface for Guest VM microkernel exec (xm create) support
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 *         Mick Jordan, Sun Microsystems, Inc.
 * 
 */

#ifndef __EXEC_BACKEND_H__
#define __EXEC_BACKEND_H__

#include <xs.h>
#include <xen/grant_table.h>
#include <xen/event_channel.h>
#include <xen/io/ring.h>
//#include "execif.h"

#define ROOT_NODE           "backend/exec"
#define WATCH_NODE          ROOT_NODE"/requests"
#define REQUEST_NODE_TM     WATCH_NODE"/%d/%d/%s"
#define EXEC_REQUEST_NODE   WATCH_NODE"/%d/%d/exec"
#define REQUEST_NODE        EXEC_REQUEST_NODE
#define WAIT_REQUEST_NODE   WATCH_NODE"/%d/%d/wait"
#define READ_REQUEST_NODE   WATCH_NODE"/%d/%d/read"
#define CLOSE_REQUEST_NODE   WATCH_NODE"/%d/%d/close"
#define FRONTEND_NODE       "/local/domain/%d/device/exec/%d"

#define TRACE_OPS 1
#define TRACE_OPS_NOISY 2
#define TRACE_RING 3

/* Handle to XenStore driver */
extern struct xs_handle *xsh;

struct request {
    int dom_id;
    int request_id;
    int status;
    int pid;
    int slot;
    int stdio[3];
};

bool xenbus_create_request_node(void);
int xenbus_get_watch_fd(void);
bool xenbus_printf(struct xs_handle *xsh,
                          xs_transaction_t xbt,
                          char* node,
                          char* path,
                          char* fmt,
                          ...);


#endif /* __EXEC_BACKEND_H__ */
