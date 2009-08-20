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
 * xenbus connection handling
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
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
