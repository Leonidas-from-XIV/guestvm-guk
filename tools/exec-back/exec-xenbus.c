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
 * xenbus connection handling
 *
 * Author: Mick Jordan
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <xenctrl.h>
#include <xs.h>
//#include "execif.h"
#include "exec-backend.h"


bool xenbus_printf(struct xs_handle *xsh,
                          xs_transaction_t xbt,
                          char* node,
                          char* path,
                          char* fmt,
                          ...)
{
    char fullpath[1024];
    char val[1024];
    va_list args;
    
    va_start(args, fmt);
    sprintf(fullpath,"%s/%s", node, path);
    vsprintf(val, fmt, args);
    va_end(args);
    printf("xenbus_printf (%s) <= %s.\n", fullpath, val);    

    return xs_write(xsh, xbt, fullpath, val, strlen(val));
}

bool xenbus_create_request_node(void)
{
    bool ret;
    struct xs_permissions perms;
    
    assert(xsh != NULL);
    xs_rm(xsh, XBT_NULL, WATCH_NODE);
    ret = xs_mkdir(xsh, XBT_NULL, WATCH_NODE); 

    if (!ret) {
        printf("failed to created %s\n", WATCH_NODE);
        return false;
    }

    perms.id = 0;
    perms.perms = XS_PERM_WRITE;
    ret = xs_set_permissions(xsh, XBT_NULL, WATCH_NODE, &perms, 1);
    if (!ret) {
        printf("failed to set write permission on %s\n", WATCH_NODE);
        return false;
    }

    perms.perms = XS_PERM_READ;
    ret = xs_set_permissions(xsh, XBT_NULL, ROOT_NODE, &perms, 1);
    if (!ret) {
      printf("failed to set read permission on %s\n", ROOT_NODE);
    }
    return ret;
}

int xenbus_get_watch_fd(void)
{
    assert(xsh != NULL);
    assert(xs_watch(xsh, WATCH_NODE, "conn-watch"));
    return xs_fileno(xsh); 
}

/* Small utility function to figure out our domain id */
static int get_self_id(void)
{
    char *dom_id;
    int ret; 
                
    assert(xsh != NULL);
    dom_id = xs_read(xsh, XBT_NULL, "domid", NULL);
    sscanf(dom_id, "%d", &ret); 
                        
    return ret;                                  
} 
