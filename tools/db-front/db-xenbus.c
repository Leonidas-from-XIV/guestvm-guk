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
