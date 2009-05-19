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
#include "fsif.h"
#include "fs-backend.h"


static bool xenbus_printf(struct xs_handle *xsh,
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
    
    assert(xsh != NULL);
    xs_rm(xsh, XBT_NULL, WATCH_NODE);
    ret = xs_mkdir(xsh, XBT_NULL, WATCH_NODE); 

    return ret;
}

int xenbus_register_export(struct fs_export *export)
{
    xs_transaction_t xst = 0;
    char node[1024];

    assert(xsh != NULL);
    if(xsh == NULL)
    {
        printf("Could not open connection to xenbus deamon.\n");
        goto error_exit;
    }
    printf("Connection to the xenbus deamon opened successfully.\n");

    /* Start transaction */
    xst = xs_transaction_start(xsh);
    if(xst == 0)
    {
        printf("Could not start a transaction.\n");
        goto error_exit;
    }
    printf("XS transaction is %d\n", xst); 
 
    /* Create node string */
    sprintf(node, "%s/%d", EXPORTS_NODE, export->export_id); 
    /* Remove old export (if exists) */ 
    xs_rm(xsh, xst, node);

    if(!(xenbus_printf(xsh, xst, node, "name", "%s", export->name) &&
         xenbus_printf(xsh, xst, node, "path", "%s", export->export_path)))
    {
        printf("Could not write the export node.\n");
        goto error_exit;
    }

    xs_transaction_end(xsh, xst, 0);
    return 0; 

error_exit:    
    if(xst != 0)
        xs_transaction_end(xsh, xst, 1);
    return -1;
}

int xenbus_get_watch_fd(void)
{
    assert(xsh != NULL);
    assert(xs_watch(xsh, WATCH_NODE, "conn-watch"));
    return xs_fileno(xsh); 
}

void xenbus_read_mount_request(struct mount *mount)
{
    char *frontend, node[1024];

    sprintf(node, WATCH_NODE"/%d/%d/frontend", 
                           mount->dom_id, mount->export->export_id);
    assert(xsh != NULL);
    frontend = xs_read(xsh, XBT_NULL, node, NULL);
    mount->frontend = frontend;
    sprintf(node, "%s/state", frontend);
    assert(strcmp(xs_read(xsh, XBT_NULL, node, NULL), STATE_READY) == 0);
    sprintf(node, "%s/ring-ref", frontend);
    mount->gref = atoi(xs_read(xsh, XBT_NULL, node, NULL));
    sprintf(node, "%s/event-channel", frontend);
    mount->remote_evtchn = atoi(xs_read(xsh, XBT_NULL, node, NULL));
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


void xenbus_write_backend_node(struct mount *mount)
{
    char node[1024], backend_node[1024];
    int self_id;

    assert(xsh != NULL);
    self_id = get_self_id();
    printf("Our own dom_id=%d\n", self_id);
    sprintf(node, "%s/backend", mount->frontend);
    sprintf(backend_node, "/local/domain/%d/"ROOT_NODE"/%d",
                                self_id, mount->mount_id);
    xs_write(xsh, XBT_NULL, node, backend_node, strlen(backend_node));

    sprintf(node, ROOT_NODE"/%d/state", mount->mount_id);
    xs_write(xsh, XBT_NULL, node, STATE_INITIALISED, strlen(STATE_INITIALISED));
}

void xenbus_write_backend_ready(struct mount *mount)
{
    char node[1024];
    int self_id;

    assert(xsh != NULL);
    self_id = get_self_id();
    sprintf(node, ROOT_NODE"/%d/state", mount->mount_id);
    xs_write(xsh, XBT_NULL, node, STATE_READY, strlen(STATE_READY));
}

