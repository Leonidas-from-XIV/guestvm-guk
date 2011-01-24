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
 * Watch for changes to domain memory target.

 * Author: Mick Jordan
 */

#include <guk/sched.h>
#include <guk/service.h>
#include <guk/init.h>
#include <guk/shutdown.h>
#include <guk/xenbus.h>
#include <guk/xmalloc.h>
#include <guk/trace.h>
#include <guk/mtarget.h>
#include <guk/mm.h>
#include <guk/gnttab.h>

#include <lib.h>

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

