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
 * Watch for changes to domian memory target.

 * Author: Mick Jordan, Sun Microsystems, Inc.
 */

#include <sched.h>
#include <service.h>
#include <init.h>
#include <shutdown.h>
#include <xenbus.h>
#include <lib.h>
#include <xmalloc.h>
#include <trace.h>
#include <mtarget.h>
#include <mm.h>
#include <gnttab.h>

#define CONTROL_DIRECTORY "memory"
#define WATCH_TOKEN        "target"
#define TARGET_PATH     CONTROL_DIRECTORY "/" WATCH_TOKEN
#define MB 1048576

static int initialized = 0;
static long target = 0;

long guk_watch_memory_target(void) {
    long new_target;
    char *path;
    if (initialized == 0) {
      xenbus_watch_path(XBT_NIL,TARGET_PATH, WATCH_TOKEN);
      initialized = 1;
    }
    do {
      path = xenbus_read_watch(WATCH_TOKEN);
      new_target = xenbus_read_integer(path);
    } while (new_target == target);
    target = new_target;
    //xprintk("mtarget: new target %d MB\n", new_target / 1024);
    return new_target;
}

