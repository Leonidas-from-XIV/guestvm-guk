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
 * xenbus interface for Guest VM microkernel exec (xm create) support
 *
 * Author: Grzegorz Milos
 *         Mick Jordan
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
#define WRITE_REQUEST_NODE   WATCH_NODE"/%d/%d/write"
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
