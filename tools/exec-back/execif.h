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
 * 
 * This would define the packets that traverse the ring between the exec frontend and backend.
 * Currently, everything is done by xenstore, but bulk/binary I/O certainly would benefit from
 * ring-based communication.
 * 
 * Author:  Mick Jordan
 * 
 */

#ifndef __EXECIF_H__
#define __EXECIF_H__

#include <xen/io/ring.h>
#include <xen/grant_table.h>

#define REQ_EXEC             1

struct execif_exec_request {
    grant_ref_t gref;
    int flags;
};

/* exec operation request */
struct execif_request {
    uint8_t type;                 /* Type of the request                  */
    uint16_t id;                  /* Request ID, copied to the response   */
    union {
        struct execif_exec_request     exec;
    } u;
};
typedef struct execif_request execif_request_t;

/* exec operation response */
struct execif_response {
    uint16_t id;
    uint64_t ret_val;
};

typedef struct execif_response execif_response_t;


DEFINE_RING_TYPES(execif, struct execif_request, struct execif_response);

#define STATE_INITIALISED     "init"
#define STATE_READY           "ready"



#endif
