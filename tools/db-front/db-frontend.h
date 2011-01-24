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
 * xenbus interface for Guest VM microkernel debugging support
 *
 * Author: Grzegorz Milos
 */
#ifndef __LIB_DB_FRONTEND__
#define __LIB_DB_FRONTEND__

#include <xs.h>
#include <xen/xen.h>
#include <xen/io/ring.h>
#include <xen/grant_table.h>
#include <xen/event_channel.h>
#include "dbif.h"

extern struct xs_handle *xsh;


#define ERROR_NO_BACKEND      -1
#define ERROR_MKDIR_FAILED    -2
#define ERROR_IN_USE          -3
int xenbus_request_connection(int dom_id, 
                              grant_ref_t *gref, 
                              evtchn_port_t *evtchn,
			      grant_ref_t *dgref);

#endif /* __LIB_DB_FRONTEND__ */
