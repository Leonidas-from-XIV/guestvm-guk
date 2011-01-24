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
 * xenbus interface for Guest VM microkernel fs support
 *
 * Author: Grzegorz Milos
 *         Mick Jordan
 * 
 */

#ifndef __LIB_FS_BACKEND__
#define __LIB_FS_BACKEND__

#include <aio.h>
#include <xs.h>
#include <xen/grant_table.h>
#include <xen/event_channel.h>
#include <xen/io/ring.h>
#include "fsif.h"

#define ROOT_NODE           "backend/vfs"
#define EXPORTS_SUBNODE     "exports"
#define EXPORTS_NODE        ROOT_NODE"/"EXPORTS_SUBNODE
#define WATCH_NODE          EXPORTS_NODE"/requests"

#define TRACE_OPS 1
#define TRACE_OPS_NOISY 2
#define TRACE_RING 3

struct fs_export
{
    int export_id;
    char *export_path;
    char *name;
    struct fs_export *next; 
};

struct fs_request
{
    int active;
    void *page;                         /* Pointer to mapped grant */
    struct fsif_request req_shadow;
    struct aiocb aiocb; 
};


struct mount
{
    struct fs_export *export;
    int dom_id;
    char *frontend;
    int mount_id;                     /* = backend id */
    grant_ref_t gref;
    evtchn_port_t remote_evtchn;
    int evth;                         /* Handle to the event channel */
    evtchn_port_t local_evtchn;
    int gnth;
    struct fsif_back_ring ring;
    int nr_entries;
    struct fs_request *requests;
    unsigned short *freelist;
};


/* Handle to XenStore driver */
extern struct xs_handle *xsh;

bool xenbus_create_request_node(void);
int xenbus_register_export(struct fs_export *export);
int xenbus_get_watch_fd(void);
void xenbus_read_mount_request(struct mount *mount);
void xenbus_write_backend_node(struct mount *mount);
void xenbus_write_backend_ready(struct mount *mount);

/* File operations, implemented in fs-ops.c */
struct fs_op
{
    int type;       /* Type of request (from fsif.h) this handlers 
                       are responsible for */
    void (*dispatch_handler)(struct mount *mount, struct fsif_request *req);
    void (*response_handler)(struct mount *mount, struct fs_request *req);
};

/* This NULL terminated array of all file requests handlers */
extern struct fs_op *fsops[];

static inline void add_id_to_freelist(unsigned int id,unsigned short* freelist)
{
    freelist[id] = freelist[0];
    freelist[0]  = id;
}

static inline unsigned short get_id_from_freelist(unsigned short* freelist)
{
    unsigned int id = freelist[0];
    freelist[0] = freelist[id];
    return id;
}

#endif /* __LIB_FS_BACKEND__ */
