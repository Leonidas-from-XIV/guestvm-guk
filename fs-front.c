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
/******************************************************************************
 *
 * Frontend driver for FS split device driver.
 *
 * Author: Grzegorz Milos
 * Changes: Mick Jordan
 */

#include <guk/fs.h>
#include <guk/os.h>
#include <guk/init.h>
#include <guk/service.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/xmalloc.h>
#include <guk/xenbus.h>
#include <guk/gnttab.h>
#include <guk/events.h>
#include <guk/trace.h>

#include <fsif.h>
#include <list.h>
#include <stdio.h>

//#define FS_DEBUG 1
#ifdef FS_DEBUG
#define DEBUG(_f, _a...) \
    printk("GUK(file=fs-front.c, line=%d) " _f "\n", __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

struct fs_request;

/******************************************************************************/
/*                      RING REQUEST/RESPONSES HANDLING                       */
/******************************************************************************/

struct fs_request
{
    void *page;
    grant_ref_t gref;
    struct thread *thread;                 /* Thread blocked on this request */
    struct fsif_response shadow_rsp;       /* Response copy writen by the
                                              interrupt handler */
};

/* Ring operations:
 * FSIF ring is used differently to Linux-like split devices. This stems from
 * the fact that no I/O request queue is present. The use of some of the macros
 * defined in ring.h is not allowed, in particular:
 * RING_PUSH_REQUESTS_AND_CHECK_NOTIFY cannot be used.
 *
 * The protocol used for FSIF ring is described below:
 *
 * In order to reserve a request the frontend:
 * a) saves current frontend_ring->req_prod_pvt into a local variable
 * b) checks that there are free request using the local req_prod_pvt
 * c) tries to reserve the request using cmpxchg on frontend_ring->req_prod_pvt
 *    if cmpxchg fails, it means that someone reserved the request, start from
 *    a)
 *
 * In order to commit a request to the shared ring:
 * a) cmpxchg shared_ring->req_prod from local req_prod_pvt to req_prod_pvt+1
 *    Loop if unsuccessful.
 * NOTE: Request should be commited to the shared ring as quickly as possible,
 *       because otherwise other threads might busy loop trying to commit next
 *       requests. It also follows that preemption should be disabled, if
 *       possible, for the duration of the request construction.
 */

/* Number of free requests (for use on front side only). */
#define FS_RING_FREE_REQUESTS(_r, _req_prod_pvt)                         \
    (RING_SIZE(_r) - (_req_prod_pvt - (_r)->rsp_cons))



static RING_IDX reserve_fsif_request(struct fs_import *import)
{
    RING_IDX idx;

    preempt_disable();
again:
    /* We will attempt to reserve slot idx */
    idx = import->ring.req_prod_pvt;
    /* Check for free slots on the ring */
    if(FS_RING_FREE_REQUESTS(&import->ring, idx) == 0)
    {
        preempt_enable();
        schedule();
        preempt_disable();
        goto again;
    }
    /* Attempt to reserve */
    if(cmpxchg(&import->ring.req_prod_pvt, idx, idx+1) != idx)
        goto again;

    return idx;
}

static void commit_fsif_request(struct fs_import *import, RING_IDX idx)
{
    while(cmpxchg(&import->ring.sring->req_prod, idx, idx+1) != idx)
    {
        printk("Failed to commit a request: req_prod=%d, idx=%d\n",
                import->ring.sring->req_prod, idx);
    }
    preempt_enable();

    /* NOTE: we cannot do anything clever about rsp_event, to hold off
     * notifications, because we don't know if we are a single request (in which
     * case we have to notify always), or a part of a larger request group
     * (when, in some cases, notification isn't required) */
    notify_remote_via_evtchn(import->local_port);
}

/* hroeck FIXME: the following suffers form the ABA problem */

static inline void add_id_to_freelist(unsigned int id,unsigned short* freelist)
{
    unsigned int old_id, new_id;

again:
    old_id = freelist[0];
    /* Note: temporal inconsistency, since freelist[0] can be changed by someone
     * else, but we are a sole owner of freelist[id], it's OK. */
    freelist[id] = old_id;
    new_id = id;
    if(cmpxchg(&freelist[0], old_id, new_id) != old_id)
    {
        printk("Cmpxchg on freelist add failed.\n");
        goto again;
    }
}

static inline unsigned short get_id_from_freelist(unsigned short* freelist)
{
    unsigned int old_id, new_id;

again:
    old_id = freelist[0];
    new_id = freelist[old_id];
    if(cmpxchg(&freelist[0], old_id, new_id) != old_id)
    {
        printk("Cmpxchg on freelist remove failed.\n");
        goto again;
    }

    return old_id;
}

/******************************************************************************/
/*                  END OF RING REQUEST/RESPONSES HANDLING                    */
/******************************************************************************/



/******************************************************************************/
/*                         INDIVIDUAL FILE OPERATIONS                         */
/******************************************************************************/
int guk_fs_open(struct fs_import *import, const char *file, int flags)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int fd;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_open call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    sprintf(fsr->page, "%s", file);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_FILE_OPEN;
    req->id = priv_req_id;
    req->u.fopen.gref = fsr->gref;
    req->u.fopen.flags = flags;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    fd = (int)fsr->shadow_rsp.ret_val;
    DEBUG("The following FD returned: %d\n", fd);
    add_id_to_freelist(priv_req_id, import->freelist);

    return fd;
}

int guk_fs_close(struct fs_import *import, int fd)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_close call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_FILE_CLOSE;
    req->id = priv_req_id;
    req->u.fclose.fd = fd;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("Close returned: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

ssize_t guk_fs_read(struct fs_import *import, int fd, void *buf,
               ssize_t len, ssize_t offset)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    ssize_t ret = 0;
    ssize_t buf_offset = 0;
    ssize_t chunk_offset = offset;
    ssize_t chunk_ret, chunk;

    while (len > 0)
    {
        if (len > PAGE_SIZE)
        {
	    chunk = PAGE_SIZE;
            len -= PAGE_SIZE;
        } else {
            chunk = len;
	    len = 0;
	}
        /* Prepare our private request structure */
        priv_req_id = get_id_from_freelist(import->freelist);
        DEBUG("Request id for fs_read call is: %d\n", priv_req_id);
        fsr = &import->requests[priv_req_id];
        fsr->thread = current;
        memset(fsr->page, 0, PAGE_SIZE);

        /* Prepare request for the backend */
        back_req_id = reserve_fsif_request(import);
        DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
        req = RING_GET_REQUEST(&import->ring, back_req_id);
        req->type = REQ_FILE_READ;
        req->id = priv_req_id;
        req->u.fread.fd = fd;
        req->u.fread.gref = fsr->gref;
        req->u.fread.len = chunk;
        req->u.fread.offset = chunk_offset;

        /* Set blocked flag before commiting the request, thus avoiding missed
         * response race */
        block(current);
        commit_fsif_request(import, back_req_id);
        schedule();

        /* Read the response */
        chunk_ret = (ssize_t)fsr->shadow_rsp.ret_val;
	ret += chunk_ret;
        DEBUG("The following ret value returned %d\n", ret);
        if (chunk_ret > 0)
            memcpy((char*)buf + buf_offset, fsr->page, chunk_ret);

        add_id_to_freelist(priv_req_id, import->freelist);
        if (chunk_ret <= 0) break;
	chunk_offset += chunk;
	buf_offset += chunk;
    }
    return ret;
}

ssize_t guk_fs_write(struct fs_import *import, int fd, const void *buf,
                 ssize_t len, ssize_t offset)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    ssize_t ret;

    BUG_ON(len > PAGE_SIZE);
    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_read call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    memcpy(fsr->page, buf, len);
    BUG_ON(len > PAGE_SIZE);
    memset((char *)fsr->page + len, 0, PAGE_SIZE - len);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_FILE_WRITE;
    req->id = priv_req_id;
    req->u.fwrite.fd = fd;
    req->u.fwrite.gref = fsr->gref;
    req->u.fwrite.len = len;
    req->u.fwrite.offset = offset;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (ssize_t)fsr->shadow_rsp.ret_val;
    DEBUG("The following ret value returned %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

static int fs_xstat(struct fs_import *import,
            int fd,
            const char *file,
            struct fsif_stat *stat_buf,
			int req_type)
{
    struct fs_request *fsr;
	struct fsif_stat *buf;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_stat call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    if (req_type == REQ_FSTAT)
    {
        memset(fsr->page, 0, PAGE_SIZE);
    } else {
        sprintf(fsr->page, "%s", file);
    }

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = req_type;
    req->id = priv_req_id;
    req->u.fstat.fd   = fd;
    req->u.fstat.gref = fsr->gref;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
	preempt_disable();
    block(current);
    commit_fsif_request(import, back_req_id);
	preempt_enable();
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("Following ret from fstat: %d\n", ret);

    //memcpy(stat_buf, fsr->page, sizeof(struct stat));
	buf = (struct fsif_stat *)fsr->page;
	stat_buf->st_mode = buf->st_mode;
	stat_buf->st_size = buf->st_size;
	stat_buf->st_blksize = buf->st_blksize;
	stat_buf->st_blocks = buf->st_mode;
	stat_buf->st_atim = buf->st_atim;
	stat_buf->st_mtim = buf->st_mtim;
	stat_buf->st_ctim = buf->st_ctim;

    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

int guk_fs_fstat(struct fs_import *import,
            int fd,
            struct fsif_stat *stat_buf)
{
    return fs_xstat(import, fd, NULL, stat_buf, REQ_FSTAT);
}
int guk_fs_stat(struct fs_import *import,
            const char *file,
            struct fsif_stat *stat_buf)
{
    return fs_xstat(import, -1, file, stat_buf, REQ_STAT);
}

int guk_fs_truncate(struct fs_import *import,
                int fd,
                int64_t length)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_truncate call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_FILE_TRUNCATE;
    req->id = priv_req_id;
    req->u.ftruncate.fd = fd;
    req->u.ftruncate.length = length;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
	preempt_disable();
    block(current);
    commit_fsif_request(import, back_req_id);
	preempt_enable();
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("Following ret from ftruncate: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

int guk_fs_remove(struct fs_import *import, char *file)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_open call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    sprintf(fsr->page, "%s", file);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_REMOVE;
    req->id = priv_req_id;
    req->u.fremove.gref = fsr->gref;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("The following ret: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}


int guk_fs_rename(struct fs_import *import,
              char *old_file_name,
              char *new_file_name)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;
    char old_header[] = "old: ";
    char new_header[] = "new: ";

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_open call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    sprintf(fsr->page, "%s%s%c%s%s",
            old_header, old_file_name, '\0', new_header, new_file_name);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_RENAME;
    req->id = priv_req_id;
    req->u.frename.gref = fsr->gref;
    req->u.frename.old_name_offset = strlen(old_header);
    req->u.frename.new_name_offset = strlen(old_header) +
                                     strlen(old_file_name) +
                                     strlen(new_header) +
                                     1 /* Accouning for the additional
                                          end of string character */;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("The following ret: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

int guk_fs_create(struct fs_import *import, char *name,
              int8_t directory, int32_t mode)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_create call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    sprintf(fsr->page, "%s", name);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_CREATE;
    req->id = priv_req_id;
    req->u.fcreate.gref = fsr->gref;
    req->u.fcreate.directory = directory;
    req->u.fcreate.mode = mode;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("The following ret: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

char** guk_fs_list(struct fs_import *import, char *name,
               int32_t offset, int32_t *nr_files, int *has_more)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    char **files, *current_file;
    int i;

    DEBUG("Different masks: NR_FILES=(%llx, %d), ERROR=(%llx, %d), HAS_MORE(%llx, %d)\n",
            NR_FILES_MASK, NR_FILES_SHIFT, ERROR_MASK, ERROR_SHIFT, HAS_MORE_FLAG, HAS_MORE_SHIFT);
    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_list call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    sprintf(fsr->page, "%s", name);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_DIR_LIST;
    req->id = priv_req_id;
    req->u.flist.gref = fsr->gref;
    req->u.flist.offset = offset;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    *nr_files = (fsr->shadow_rsp.ret_val & NR_FILES_MASK) >> NR_FILES_SHIFT;
    files = NULL;
    if(*nr_files <= 0) goto exit;
    files = malloc(sizeof(char*) * (*nr_files));
    current_file = fsr->page;
    for(i=0; i<*nr_files; i++)
    {
        files[i] = strdup(current_file);
        current_file += strlen(current_file) + 1;
    }
    if(has_more != NULL) {
        *has_more = ((fsr->shadow_rsp.ret_val & HAS_MORE_FLAG) == HAS_MORE_FLAG) ? 1 : 0;
    }
    add_id_to_freelist(priv_req_id, import->freelist);
exit:
    return files;
}

int guk_fs_fchmod(struct fs_import *import, int fd, int32_t mode)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_chmod call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_CHMOD;
    req->id = priv_req_id;
    req->u.fchmod.fd = fd;
    req->u.fchmod.mode = mode;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("The following returned: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

int64_t guk_fs_space(struct fs_import *import, char *location)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int64_t ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_space is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;
    sprintf(fsr->page, "%s", location);

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_FS_SPACE;
    req->id = priv_req_id;
    req->u.fspace.gref = fsr->gref;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int64_t)fsr->shadow_rsp.ret_val;
    DEBUG("The following returned: %lld\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}

int guk_fs_sync(struct fs_import *import, int fd)
{
    struct fs_request *fsr;
    unsigned short priv_req_id;
    RING_IDX back_req_id;
    struct fsif_request *req;
    int ret;

    /* Prepare our private request structure */
    priv_req_id = get_id_from_freelist(import->freelist);
    DEBUG("Request id for fs_sync call is: %d\n", priv_req_id);
    fsr = &import->requests[priv_req_id];
    fsr->thread = current;

    /* Prepare request for the backend */
    back_req_id = reserve_fsif_request(import);
    DEBUG("Backend request id=%d, gref=%d\n", back_req_id, fsr->gref);
    req = RING_GET_REQUEST(&import->ring, back_req_id);
    req->type = REQ_FILE_SYNC;
    req->id = priv_req_id;
    req->u.fsync.fd = fd;

    /* Set blocked flag before commiting the request, thus avoiding missed
     * response race */
    block(current);
    commit_fsif_request(import, back_req_id);
    schedule();

    /* Read the response */
    ret = (int)fsr->shadow_rsp.ret_val;
    DEBUG("Close returned: %d\n", ret);
    add_id_to_freelist(priv_req_id, import->freelist);

    return ret;
}


/******************************************************************************/
/*                       END OF INDIVIDUAL FILE OPERATIONS                    */
/******************************************************************************/


static void fsfront_handler(evtchn_port_t port, void *data)
{
    struct fs_import *import = (struct fs_import*)data;
    static int in_irq = 0;
    RING_IDX cons, rp;
    int more;

    /* Check for non-reentrance */
    BUG_ON(in_irq);
    in_irq = 1;

    DEBUG("Event from import [%d:%d].\n", import->dom_id, import->export_id);
moretodo:
    rp = import->ring.sring->req_prod;
    rmb(); /* Ensure we see queued responses up to 'rp'. */
    cons = import->ring.rsp_cons;
    while (cons != rp)
    {
        struct fsif_response *rsp;
        struct fs_request *req;

        rsp = RING_GET_RESPONSE(&import->ring, cons);
        DEBUG("Response at idx=%d to request id=%d, ret_val=%lx\n",
            import->ring.rsp_cons, rsp->id, rsp->ret_val);
        req = &import->requests[rsp->id];
        memcpy(&req->shadow_rsp, rsp, sizeof(struct fsif_response));
        DEBUG("Waking up: %s\n", req->thread->name);
        wake(req->thread);

        cons++;
    }

    import->ring.rsp_cons = rp;
    RING_FINAL_CHECK_FOR_RESPONSES(&import->ring, more);
    if(more) goto moretodo;

    in_irq = 0;
}

static void alloc_request_table(struct fs_import *import)
{
    struct fs_request *requests;
    int i;

    BUG_ON(import->nr_entries <= 0);
    if (trace_fs_front()) tprintk("Allocating request array for import %d, nr_entries = %d.\n",
            import->import_id, import->nr_entries);
    requests = xmalloc_array(struct fs_request, import->nr_entries);
    import->freelist = xmalloc_array(unsigned short, import->nr_entries);
    memset(import->freelist, 0, sizeof(unsigned short) * import->nr_entries);
    for(i=0; i<import->nr_entries; i++)
    {
        requests[i].page = (void *)alloc_page();
        requests[i].gref = gnttab_grant_access(import->dom_id,
                                               virt_to_mfn(requests[i].page),
                                               0);
        //printk("   ===>> Page=%lx, gref=%d, mfn=%lx\n", requests[i].page, requests[i].gref, virt_to_mfn(requests[i].page));
        add_id_to_freelist(i, import->freelist);
    }
    import->requests = requests;
}


#if 0
/******************************************************************************/
/*                                FS TESTS                                    */
/******************************************************************************/


void test_fs_import(void *data)
{
    struct fs_import *import = (struct fs_import *)data;
    int ret, fd, i, nr_files;
    char buffer[1024];
    ssize_t offset;
    char **files;
    long ret64;

    printk("fs test entered\n");
    /* Sleep for 1s and then try to open a file */
    sleep(1000);
    printk("fs test woke from sleep\n");
    ret = fs_create(import, "fs-created-directory", 1, 0777);
    printk("Directory create: %d\n", ret);

    ret = fs_create(import, "fs-created-directory/fs-created-file", 0, 0666);
    printk("File create: %d\n", ret);

    fd = fs_open(import, "fs-created-directory/fs-created-file", O_RDRW);
    printk("File descriptor: %d\n", fd);
    if(fd < 0) return;

    offset = 0;
    for(i=0; i<10; i++)
    {
        sprintf(buffer, "Current time is: %lld\n", NOW());
        ret = fs_write(import, fd, buffer, strlen(buffer), offset);
        printk("Written current time (%d)\n", ret);
        if(ret < 0)
            return;
        offset += ret;
    }

    ret = fs_close(import, fd);
    printk("Closed fd: %d, ret=%d\n", fd, ret);

    printk("Listing files in /\n");
    files = fs_list(import, "/", 0, &nr_files, NULL);
    for(i=0; i<nr_files; i++)
        printk(" files[%d] = %s\n", i, files[i]);

    ret64 = fs_space(import, "/");
    printk("Free space: %lld (=%lld Mb)\n", ret64, (ret64 >> 20));

}

//    char *content = (char *)alloc_page();
    int fd, ret;
//    int read;
    char write_string[] = "\"test data written from minios\"";
    struct fsif_stat_response stat;
    char **files;
    int32_t nr_files, i;
    int64_t ret64;


    fd = fs_open(import, "test-export-file");
//    read = fs_read(import, fd, content, PAGE_SIZE, 0);
//    printk("Read: %d bytes\n", read);
//    content[read] = '\0';
//    printk("Value: %s\n", content);
    ret = fs_write(import, fd, write_string, strlen(write_string), 0);
    printk("Ret after write: %d\n", ret);
    ret = fs_stat(import, fd, &stat);
    printk("Ret after stat: %d\n", ret);
    printk(" st_mode=%o\n", stat.stat_mode);
    printk(" st_uid =%d\n", stat.stat_uid);
    printk(" st_gid =%d\n", stat.stat_gid);
    printk(" st_size=%ld\n", stat.stat_size);
    printk(" st_atime=%ld\n", stat.stat_atime);
    printk(" st_mtime=%ld\n", stat.stat_mtime);
    printk(" st_ctime=%ld\n", stat.stat_ctime);
    ret = fs_truncate(import, fd, 30);
    printk("Ret after truncate: %d\n", ret);
    ret = fs_remove(import, "test-to-remove/test-file");
    printk("Ret after remove: %d\n", ret);
    ret = fs_remove(import, "test-to-remove");
    printk("Ret after remove: %d\n", ret);
    ret = fs_chmod(import, fd, 0700);
    printk("Ret after chmod: %d\n", ret);
    ret = fs_sync(import, fd);
    printk("Ret after sync: %d\n", ret);
    ret = fs_close(import, fd);
    //ret = fs_rename(import, "test-export-file", "renamed-test-export-file");
    //printk("Ret after rename: %d\n", ret);
    ret = fs_create(import, "created-dir", 1, 0777);
    printk("Ret after dir create: %d\n", ret);
    ret = fs_create(import, "created-dir/created-file", 0, 0777);
    printk("Ret after file create: %d\n", ret);
    files = fs_list(import, "/", 15, &nr_files, NULL);
    for(i=0; i<nr_files; i++)
        printk(" files[%d] = %s\n", i, files[i]);
    ret64 = fs_space(import, "created-dir");
    printk("Ret after space: %lld\n", ret64);


/******************************************************************************/
/*                            END OF FS TESTS                                 */
/******************************************************************************/
#endif


static int init_fs_import(struct fs_import *import, int test)
{
    char *err;
    xenbus_transaction_t xbt;
    char nodename[1024], r_nodename[1024], token[128], *message = NULL;
    struct fsif_sring *sring;
    int retry = 0;
    domid_t self_id;

    if (trace_fs_front()) tprintk("Initialising FS frontend to backend dom %d\n", import->dom_id);
    /* Allocate page for the shared ring */
    sring = (struct fsif_sring*) alloc_page();
    memset(sring, 0, PAGE_SIZE);

    /* Init the shared ring */
    SHARED_RING_INIT(sring);

    /* Init private frontend ring */
    FRONT_RING_INIT(&import->ring, sring, PAGE_SIZE);
    import->nr_entries = import->ring.nr_ents;

    /* Allocate table of requests */
    alloc_request_table(import);

    /* Grant access to the shared ring */
    import->gnt_ref = gnttab_grant_access(import->dom_id, virt_to_mfn(sring), 0);

    /* Allocate event channel */
    BUG_ON(evtchn_alloc_unbound(import->dom_id,
                                fsfront_handler,
                                ANY_CPU,
                                import,
                                &import->local_port));
    unmask_evtchn(import->local_port);


    self_id = xenbus_get_self_id();
    /* Write the frontend info to a node in our Xenbus */
    sprintf(nodename, "/local/domain/%d/device/vfs/%d",
                        self_id, import->import_id);

again:
    err = xenbus_transaction_start(&xbt);
    if (err) {
        printk("starting transaction\n");
    }

    err = xenbus_printf(xbt,
                        nodename,
                        "ring-ref",
                        "%u",
                        import->gnt_ref);
    if (err) {
        message = "writing ring-ref";
        goto abort_transaction;
    }

    err = xenbus_printf(xbt,
                        nodename,
                        "event-channel",
                        "%u",
                        import->local_port);
    if (err) {
        message = "writing event-channel";
        goto abort_transaction;
    }

    err = xenbus_printf(xbt, nodename, "state", STATE_READY, 0xdeadbeef);


    err = xenbus_transaction_end(xbt, 0, &retry);
    if (retry) {
            goto again;
        if (trace_fs_front()) tprintk("completing transaction\n");
    } else if (err)
	free(err);

    /* Now, when our node is prepared we write request in the exporting domain
     * */
    if (trace_fs_front()) tprintk("Our own id is %d\n", self_id);
    sprintf(r_nodename,
            "/local/domain/%d/backend/vfs/exports/requests/%d/%d/frontend",
            import->dom_id, self_id, import->export_id);
    BUG_ON(xenbus_write(XBT_NIL, r_nodename, nodename));

    goto done;

abort_transaction:
    err = xenbus_transaction_end(xbt, 1, &retry);
    if (err)
	free(err);

done:

#define WAIT_PERIOD 10   /* Wait period in ms */
#define MAX_WAIT    50   /* Max number of WAIT_PERIODs */
    import->backend = NULL;
    sprintf(r_nodename, "%s/backend", nodename);

    for(retry = MAX_WAIT; retry > 0; retry--)
    {
        err = xenbus_read(XBT_NIL, r_nodename, &import->backend);
		if (err) {
			if (trace_fs_front()) 
				tprintk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, err);
			free(err);
			sleep(WAIT_PERIOD);
			continue;
		}
        if(import->backend) {
            if (trace_fs_front()) 
		printk("Backend found at %s\n", import->backend);
            break;
        }
    }

    if(!import->backend)
    {
		if (trace_fs_front()) 
			tprintk("No backend available.\n");
        /* TODO - cleanup datastructures/xenbus */
        return 1;
    }
    sprintf(r_nodename, "%s/state", import->backend);
    sprintf(token, "fs-front-%d", import->import_id);
    /* The token will not be unique if multiple imports are inited */
    xenbus_watch_path(XBT_NIL, r_nodename, token);
    xenbus_wait_for_value(token, r_nodename, STATE_READY);
    if (trace_fs_front()) tprintk("fs-backend ready.\n");

    // if (test) create_thread("fs-tester", test_fs_import, 0, import);

    return 0;
}


static void add_export(struct list_head *exports, unsigned int domid)
{
    char node[1024], path[1024], **exports_list = NULL, *ret_msg;
    char *msg = NULL;
    int j = 0;
    static int import_id = 0;

    sprintf(node, "/local/domain/%d/backend/vfs/exports", domid);
    ret_msg = xenbus_ls(XBT_NIL, node, &exports_list);
    if (ret_msg && strcmp(ret_msg, "ENOENT"))
        printk("couldn't read %s: %s\n", node, ret_msg);
    while(exports_list && exports_list[j])
    {
        struct fs_import *import; 
        int export_id = -1;

        sscanf(exports_list[j], "%d", &export_id);
        if(export_id >= 0)
        {
            import = xmalloc(struct fs_import);
            import->dom_id = domid;

		sprintf(path, "%s/%d/path", node, export_id);
		msg = xenbus_read(XBT_NIL, path, &import->path);
		if (msg) {
		    printk("%s %d ERROR reading xenbus : %s\n", __FILE__, __LINE__, msg);
		    goto exit;
		}


            import->export_id = export_id;
            import->import_id = import_id++;
            INIT_LIST_HEAD(&import->list);
            list_add(&import->list, exports);

        }
        free(exports_list[j]);
        j++;
    }
exit:
    if(exports_list)
        free(exports_list);
    if(ret_msg)
        free(ret_msg);
}

#if 0
static struct list_head* probe_exports(void)
{
    struct list_head *exports;
    char **node_list = NULL, *msg = NULL;
    int i = 0;
    static int import_id = 0;

    exports = xmalloc(struct list_head);
    INIT_LIST_HEAD(exports);

    msg = xenbus_ls(XBT_NIL, "/local/domain", &node_list);
    if(msg)
    {
        if (trace_fs_front())
			tprintk("Could not list VFS exports (%s).\n", msg);
        goto exit;
    }
    	
    while(node_list[i])
    {
        char node[1024], path[1024], **exports_list = NULL, *ret_msg;
        int j = 0;

        sprintf(node, "/local/domain/%s/backend/vfs/exports", node_list[i]);
        ret_msg = xenbus_ls(XBT_NIL, node, &exports_list);
        while(exports_list && exports_list[j])
        {
            struct fs_import *import;
            int export_id = -1;
            sscanf(exports_list[j], "%d", &export_id);
            if(export_id >= 0)
            {
                import = xmalloc(struct fs_import);
                sscanf(node_list[i], "%d", &import->dom_id);
		sprintf(path, "%s/%d/path", node, export_id);
		msg = xenbus_read(XBT_NIL, path, &import->path);
		if (msg) {
		    printk("%s %d ERROR reading xenbus: %s\n", __FILE__, __LINE__, msg);
		    goto exit;
		}
                import->export_id = export_id;
                import->import_id = import_id++;
                INIT_LIST_HEAD(&import->list);
                list_add(&import->list, exports);
            }
            free(exports_list[j]);
            j++;
        }
        if(exports_list)
            free(exports_list);
        if(ret_msg)
            free(ret_msg);
        free(node_list[i]);
        i++;
    }

exit:
    if(msg)
        free(msg);
    if(node_list)
        free(node_list);
    return exports;
}
#endif


static LIST_HEAD(exports);

static int fs_init = 0;
static void init_fs_frontend(int test)
{
    struct list_head *entry;
    struct fs_import *import = NULL;
    if (trace_fs_front()) tprintk("Initing FS frontend(s), test %d.\n", test);

//    fs_imports = probe_exports();
    add_export(&exports, 0);
    list_for_each(entry, &exports)
    {
        import = list_entry(entry, struct fs_import, list);

        if (trace_fs_front())
	    tprintk("FS export [dom=%d, id=%d, path=%s] found\n",
                import->dom_id, import->export_id, import->path);

        if(init_fs_import(import, test)) {
	    fs_init = 2;
	    return;
	}
    }
    fs_init = 1;
}

struct list_head *guk_fs_get_imports(void) {
  while (!fs_init) {
    sleep(1000);
  }

  if (fs_init == 1) {
      return &exports;
  } else
      return NULL;
}

struct fs_import *guk_fs_get_next(struct fs_import *import)
{
	struct fs_import *next = NULL;

	if (import->list.next != &exports)
		next = list_entry(import->list.next, struct fs_import, list);

	return next;
}

char *guk_fs_import_path(struct fs_import *import)
{
	return import->path;
}

static int fs_suspend(void)
{
    if(fs_init != 1)
	return 0;

    xprintk("WARNING: fs suspend not supported\n");
    return 1;
}

static int fs_resume(void)
{
    if(fs_init != 1)
	return 0;

    xprintk("WARNING: fs resume not supported\n");
    return 1;
}

static int fs_shutdown(void)
{
    if(fs_init != 1)
	return 0;

    return 1;
}

static void fs_thread(void *cmd_line)
{
    int fstest = strstr((char*)cmd_line, FSTEST_CMDLINE) == NULL ? 0 : 1;
    init_fs_frontend(fstest);
}

static int start_fs_thread(void *args)
{
    create_thread("fs-frontend", fs_thread, UKERNEL_FLAG, args);
    return 0;
}

static struct service fs_service = {
    .name = "fs service",

    .init = start_fs_thread,
    .suspend = fs_suspend,
    .resume = fs_resume,
    .shutdown = fs_shutdown,
    .arg = "",
};

USED static int init_func(void)
{
	register_service(&fs_service);
	return 0;
}
DECLARE_INIT(init_func);
