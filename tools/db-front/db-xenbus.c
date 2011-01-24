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
 * Author: Grzegorz Milos
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include "db-frontend.h"


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


int xenbus_request_connection(int dom_id, 
                              grant_ref_t *gref, 
                              evtchn_port_t *evtchn,
			      grant_ref_t *dgref)
{
    char request_node[256], *dom_path, **watch_rsp, *db_back_rsp, *str;
    bool ret;
    unsigned int length;
    int i;
    grant_ref_t lgref, ldgref;
    evtchn_port_t levtchn;
    sprintf(request_node,
            "/local/domain/%d/device/db/requests", 
            dom_id);
    dom_path = xs_read(xsh, XBT_NULL, request_node, &length);
    free(dom_path);
    if(length <= 0)
        return -1;
    sprintf(request_node,
            "/local/domain/%d/device/db/requests/%d", 
            dom_id, get_self_id());
    assert(xs_watch(xsh, request_node, "db-token") == true);
    ret = xs_write(xsh, XBT_NULL, request_node, "connection request", 
                                         strlen("connection requset"));
    
    if(!ret)
        return -2;  

watch_again:    
    watch_rsp = xs_read_watch(xsh, &length);
    assert(length == 2);
    assert(strcmp(watch_rsp[XS_WATCH_TOKEN], "db-token") == 0);
    db_back_rsp = watch_rsp[XS_WATCH_PATH] + strlen(request_node);
    if(strcmp(db_back_rsp, "/connected") == 0)
    {
        str = xs_read(xsh, XBT_NULL, watch_rsp[XS_WATCH_PATH], &length);
        assert(length > 0);
        lgref = -1;
        levtchn = -1;
        sscanf(str, "gref=%d evtchn=%d dgref=%d", &lgref, &levtchn, &ldgref);
        //printf("Gref is = %d, evtchn = %d, DGref = %d\n", lgref, levtchn, ldgref);
        *gref = lgref;
        *evtchn = levtchn;
	*dgref = ldgref;
        ret = 0; 
        goto exit;
    }
    if(strcmp(db_back_rsp, "/already-in-use") == 0)
    {                      
        ret = -3; 
        goto exit;
    }

    free(watch_rsp);
    goto watch_again;

exit:
    xs_unwatch(xsh, request_node, "db-token");
    free(watch_rsp);

    return ret;
}
