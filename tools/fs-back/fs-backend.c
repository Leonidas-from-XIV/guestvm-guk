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
 * Backend for Guest VM microkernel fs support
 *
 * Author: Grzegorz Milos
 *         Mick Jordan
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include <pthread.h>
#include <xenctrl.h>
#include <aio.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <xen/io/ring.h>
#include "fs-backend.h"

struct xs_handle *xsh = NULL;
static struct fs_export *fs_exports = NULL;
static int export_id = 0;
static int mount_id = 0;
int trace_level = 1;

void dispatch_response(struct mount *mount, int priv_req_id)
{
    int i;
    struct fs_op *op;
    struct fs_request *req = &mount->requests[priv_req_id];

    for(i=0;;i++)
    {
        op = fsops[i];
        /* We should dispatch a response before reaching the end of the array */
        assert(op != NULL);
        if(op->type == req->req_shadow.type)
        {
            if (trace_level >= TRACE_RING) printf("Found op for type=%d\n", op->type);
            /* There needs to be a response handler */
            assert(op->response_handler != NULL);
            op->response_handler(mount, req);
            break;
        }
    }

    req->active = 0;
    add_id_to_freelist(priv_req_id, mount->freelist);
}

static void handle_aio_events(struct mount *mount)
{
    int fd, ret, count, i, notify;
    evtchn_port_t port;
    /* AIO control block for the evtchn file destriptor */
    struct aiocb evtchn_cb;
    const struct aiocb * cb_list[mount->nr_entries];
    int request_ids[mount->nr_entries];

    /* Prepare the AIO control block for evtchn */ 
    fd = xc_evtchn_fd(mount->evth); 
    bzero(&evtchn_cb, sizeof(struct aiocb));
    evtchn_cb.aio_fildes = fd;
    evtchn_cb.aio_nbytes = sizeof(port);
    evtchn_cb.aio_buf = &port;
    assert(aio_read(&evtchn_cb) == 0);

wait_again:   
    /* Create list of active AIO requests */
    count = 0;
    for(i=0; i<mount->nr_entries; i++)
        if(mount->requests[i].active)
        {
            cb_list[count] = &mount->requests[i].aiocb;
            request_ids[count] = i;
            count++;
        }
    /* Add the event channel at the end of the list. Event channel needs to be
     * handled last as it exits this function. */
    cb_list[count] = &evtchn_cb;
    request_ids[count] = -1;
    count++;

    /* Block till an AIO requset finishes, or we get an event */ 
    assert(aio_suspend(cb_list, count, NULL) == 0);
    for(i=0; i<count; i++)
        if(aio_error(cb_list[i]) != EINPROGRESS)
        {
            if(request_ids[i] >= 0)
                dispatch_response(mount, request_ids[i]);
            else
                goto read_event_channel;
        }
 
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&mount->ring, notify);
    if (trace_level >= TRACE_RING) printf("Pushed responses and notify=%d\n", notify);
    if(notify)
        xc_evtchn_notify(mount->evth, mount->local_evtchn);
    
    goto wait_again;

read_event_channel:    
    assert(aio_return(&evtchn_cb) == sizeof(evtchn_port_t)); 
    assert(xc_evtchn_unmask(mount->evth, mount->local_evtchn) >= 0);
}


void allocate_request_array(struct mount *mount)
{
    int i, nr_entries = mount->nr_entries;
    struct fs_request *requests;
    unsigned short *freelist;
    
    requests = malloc(sizeof(struct fs_request) *nr_entries);
    freelist = malloc(sizeof(unsigned short) * nr_entries); 
    memset(requests, 0, sizeof(struct fs_request) * nr_entries);
    memset(freelist, 0, sizeof(unsigned short) * nr_entries);
    for(i=0; i< nr_entries; i++)
    {
        requests[i].active = 0; 
        add_id_to_freelist(i, freelist);
    }
    mount->requests = requests;
    mount->freelist = freelist;
}


void* handle_mount(void *data)
{
    int more, notify;
    struct mount *mount = (struct mount *)data;
    
    if (trace_level >= TRACE_OPS) printf("Starting a thread for mount: %d\n", mount->mount_id);
    allocate_request_array(mount);

    for(;;)
    {
        int nr_consumed=0;
        RING_IDX cons, rp;
        struct fsif_request *req;

        handle_aio_events(mount);
moretodo:
        rp = mount->ring.sring->req_prod;
        xen_rmb(); /* Ensure we see queued requests up to 'rp'. */
                
        while ((cons = mount->ring.req_cons) != rp)
        {
            int i;
            struct fs_op *op;

            if (trace_level >= TRACE_RING) printf("Got a request at %d\n", cons);
            req = RING_GET_REQUEST(&mount->ring, cons);
            if (trace_level >= TRACE_RING) printf("Request type=%d\n", req->type); 
            for(i=0;;i++)
            {
                op = fsops[i];
                if(op == NULL)
                {
                    /* We've reached the end of the array, no appropirate
                     * handler found. Warn, ignore and continue. */
                    printf("WARN: Unknown request type: %d\n", req->type);
                    mount->ring.req_cons++; 
                    break;
                }
                if(op->type == req->type)
                {
                    /* There needs to be a dispatch handler */
                    assert(op->dispatch_handler != NULL);
                    op->dispatch_handler(mount, req);
                    break;
                }
             }

            nr_consumed++;
        }
        if (trace_level >= TRACE_RING) printf("Backend consumed: %d requests\n", nr_consumed);
        RING_FINAL_CHECK_FOR_REQUESTS(&mount->ring, more);
        if(more) goto moretodo;

        RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&mount->ring, notify);
        if (trace_level >= TRACE_RING) printf("Pushed responces and notify=%d\n", notify);
        if(notify)
            xc_evtchn_notify(mount->evth, mount->local_evtchn);
    }
 
    if (trace_level >= TRACE_OPS) printf("Destroying thread for mount: %d\n", mount->mount_id);
    pthread_exit(NULL);
}

static void handle_connection(int frontend_dom_id, int export_id)
{
    struct mount *mount;
    struct fs_export *export;
    int evt_port;
    pthread_t handling_thread;
    struct fsif_sring *sring;

    if (trace_level >= TRACE_OPS) printf("Handling connection from dom=%d, for export=%d\n", 
            frontend_dom_id, export_id);
    /* Try to find the export on the list */
    export = fs_exports;
    while(export)
    {
        if(export->export_id == export_id)
            break;
        export = export->next;
    }
    if(!export)
    {
        printf("Could not find the export (the id is unknown).\n");
        return;
    }

    mount = (struct mount*)malloc(sizeof(struct mount));
    mount->dom_id = frontend_dom_id;
    mount->export = export;
    mount->mount_id = mount_id++;
    xenbus_read_mount_request(mount);
    if (trace_level >= TRACE_OPS) printf("Frontend found at: %s (gref=%d, evtchn=%d)\n", 
            mount->frontend, mount->gref, mount->remote_evtchn);
    xenbus_write_backend_node(mount);
    mount->evth = -1;
    mount->evth = xc_evtchn_open(); 
    assert(mount->evth != -1);
    mount->local_evtchn = -1;
    mount->local_evtchn = xc_evtchn_bind_interdomain(mount->evth, 
                                                     mount->dom_id, 
                                                     mount->remote_evtchn);
    assert(mount->local_evtchn != -1);
    mount->gnth = -1;
    mount->gnth = xc_gnttab_open(); 
    assert(mount->gnth != -1);
    sring = xc_gnttab_map_grant_ref(mount->gnth,
                                    mount->dom_id,
                                    mount->gref,
                                    PROT_READ | PROT_WRITE);
    BACK_RING_INIT(&mount->ring, sring, PAGE_SIZE);
    mount->nr_entries = mount->ring.nr_ents; 
    xenbus_write_backend_ready(mount);

    pthread_create(&handling_thread, NULL, &handle_mount, mount);
}

static void await_connections(void)
{
    int fd, ret, dom_id, export_id; 
    fd_set fds;
    char **watch_paths;
    unsigned int len;

    assert(xsh != NULL);
    fd = xenbus_get_watch_fd(); 
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    /* Infinite watch loop */
    do {
        ret = select(fd+1, &fds, NULL, NULL, NULL);
        assert(ret == 1);
        watch_paths = xs_read_watch(xsh, &len);
        assert(len == 2);
        assert(strcmp(watch_paths[1], "conn-watch") == 0);
        if(strcmp(watch_paths[0], WATCH_NODE) == 0)
            goto next_select;
        dom_id = -1;
        export_id = -1;
        if (trace_level >= TRACE_RING) printf("Path changed %s\n", watch_paths[0]);
        sscanf(watch_paths[0], WATCH_NODE"/%d/%d/frontend", 
                &dom_id, &export_id);
        if((dom_id >= 0) && (export_id >= 0))
            handle_connection(dom_id, export_id);
next_select:        
        printf("Awaiting next connection.\n");
        /* TODO - we need to figure out what to free */
        //free(watch_paths[0]);
        //free(watch_paths[1]);
    } while (1);
}

struct fs_export* create_export(char *name, char *export_path)
{
    struct fs_export *curr_export, **last_export;

    /* Create export structure */
    curr_export = (struct fs_export *)malloc(sizeof(struct fs_export));
    curr_export->name = name;
    curr_export->export_path = export_path;
    curr_export->export_id = export_id++;
    /* Thread it onto the list */
    curr_export->next = NULL;
    last_export = &fs_exports;
    while(*last_export)
        last_export = &((*last_export)->next);
    *last_export = curr_export;

    return curr_export;
}

int main(int argc, char*argv[])
{
    struct fs_export *export;
    char *export_name = "/exports";
    //printf("fsif_request size %d, fsif_response size %d\n", sizeof(struct fsif_request), sizeof(struct fsif_response));

    if (argc > 1) sscanf(argv[1], "%d", &trace_level);
    if (argc > 2) export_name = argv[2];

    /* Open the connection to XenStore first */
    xsh = xs_domain_open();
    assert(xsh != NULL);
    xs_rm(xsh, XBT_NULL, ROOT_NODE);
    /* Create watch node */
    xenbus_create_request_node();
    
    /* Create & register the default export */
    export = create_export("default", export_name);
    xenbus_register_export(export);

    await_connections();
    /* Close the connection to XenStore when we are finished with everything */
    xs_daemon_close(xsh);
#if 0
    int xc_handle;
    char *shared_page;
    int prot = PROT_READ | PROT_WRITE;
  
    xc_handle = xc_gnttab_open();
    printf("Main fn.\n");

    shared_page = xc_gnttab_map_grant_ref(xc_handle,
                                           7,
                                           2047,
                                           prot);
    
    shared_page[20] = '\0';
    printf("Current content of the page = %s\n", shared_page);
    sprintf(shared_page, "%s", "Haha dirty page now! Very bad page.");
    xc_gnttab_munmap(xc_handle, shared_page, 1);
    xc_gnttab_close(xc_handle);
    unrelated next line, saved for later convinience    
    xc_evtchn_notify(mount->evth, mount->local_evtchn);
#endif
}
