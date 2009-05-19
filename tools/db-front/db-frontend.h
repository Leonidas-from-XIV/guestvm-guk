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
 * xenbus interface for Guest VM microkernel debugging support
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
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
