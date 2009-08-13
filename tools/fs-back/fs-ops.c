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
/******************************************************************************
 *
 * Operation implementations for backend of FS split device driver.
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 * Changes: Mick Jordan, Sun Microsystems, Inc..
 */
#include <stdio.h>
#include <aio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <dirent.h>
#include <xenctrl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include "fs-backend.h"

/* For debugging only */
#include <sys/time.h>
#include <time.h>


#define BUFFER_SIZE 1024

extern int trace_level;

/*
 * Check that path starts with the exported prefix.
 * Generally, permitting equality is bad as it would
 * permit deletion and rename, but stat os ok
 */
static int check_exported_eqok(struct mount *mount, char * path, int eqok)
{
    char *export_path = mount->export->export_path;
	
    while (*export_path != '\0' && *path != '\0')
    {
        if (*export_path++ != *path++) return 0;
    }
    if (*path == '\0') {
      // must be identical
      return *export_path == '\0' && eqok;
    } else {
      return *path == '/';
    }
    return 1;
}

static int check_exported(struct mount *mount, char *path)
{
    return check_exported_eqok(mount, path, 0);
}

unsigned short get_request(struct mount *mount, struct fsif_request *req)
{
    unsigned short id = get_id_from_freelist(mount->freelist); 

    if (trace_level >= TRACE_RING) printf("Private Request id: %d\n", id);
    memcpy(&mount->requests[id].req_shadow, req, sizeof(struct fsif_request));
    mount->requests[id].active = 1;

    return id;
}


void dispatch_file_open(struct mount *mount, struct fsif_request *req)
{
    char *file_name;
    int fd = -ENOENT; // error if not exported
    struct timeval tv1, tv2;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;
    int flags;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching file open operation (gref=%d).\n", req->u.fopen.gref);
    /* Read the request, and open file */
    file_name = xc_gnttab_map_grant_ref(mount->gnth,
                                        mount->dom_id,
                                        req->u.fopen.gref,
                                        PROT_READ);
    flags = req->u.fopen.flags;
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("File open issued for %s, flags %d\n", file_name, flags);
    if (check_exported(mount, file_name))
    {
        fd = open(file_name, flags);
        if (trace_level >= TRACE_OPS) printf("Got FD: %d, errno %d\n", fd, errno);
		if (fd < 0) fd = -errno;
    } else {
		printf("mount missmatch\n");	
	}
    assert(xc_gnttab_munmap(mount->gnth, file_name, 1) == 0);
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;


    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)fd;
}

void dispatch_file_close(struct mount *mount, struct fsif_request *req)
{
    int ret;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching file close operation (fd=%d).\n", req->u.fclose.fd);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("File close issued for %d\n", req->u.fclose.fd); 
    ret = close(req->u.fclose.fd);
    if (trace_level >= TRACE_OPS_NOISY) printf("Got ret: %d\n", ret);
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;


    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}
void dispatch_file_read(struct mount *mount, struct fsif_request *req)
{
    void *buf;
    int fd;
    uint16_t req_id;
    unsigned short priv_id;
    struct fs_request *priv_req;

    /* Read the request */
    buf = xc_gnttab_map_grant_ref(mount->gnth,
                                  mount->dom_id,
                                  req->u.fread.gref,
                                  PROT_WRITE);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("File read issued for FD=%d (len=%ld, offset=%ld)\n", 
            req->u.fread.fd, req->u.fread.len, req->u.fread.offset); 
   
    priv_id = get_request(mount, req);
    if (trace_level >= TRACE_OPS_NOISY) printf("Private id is: %d\n", priv_id);
    priv_req = &mount->requests[priv_id];
    priv_req->page = buf;

    /* Dispatch AIO read request */
    bzero(&priv_req->aiocb, sizeof(struct aiocb));
    priv_req->aiocb.aio_fildes = req->u.fread.fd;
    priv_req->aiocb.aio_nbytes = req->u.fread.len;
    priv_req->aiocb.aio_offset = req->u.fread.offset;
    priv_req->aiocb.aio_buf = buf;
    assert(aio_read(&priv_req->aiocb) >= 0);

     
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;
}

void end_file_read(struct mount *mount, struct fs_request *priv_req)
{
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;
    int ret;

    /* Release the grant */
    assert(xc_gnttab_munmap(mount->gnth, priv_req->page, 1) == 0);

    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    req_id = priv_req->req_shadow.id; 
    if (trace_level >= TRACE_OPS_NOISY) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id;
    ret = aio_return(&priv_req->aiocb);
    if (ret < 0) ret = -errno;
    rsp->ret_val = (uint64_t)ret;
}

void dispatch_file_write(struct mount *mount, struct fsif_request *req)
{
    void *buf;
    int fd;
    uint16_t req_id;
    unsigned short priv_id;
    struct fs_request *priv_req;

    /* Read the request */
    buf = xc_gnttab_map_grant_ref(mount->gnth,
                                  mount->dom_id,
                                  req->u.fwrite.gref,
                                  PROT_READ);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("File write issued for FD=%d (len=%ld, offest=%ld)\n", 
            req->u.fwrite.fd, req->u.fwrite.len, req->u.fwrite.offset); 
   
    priv_id = get_request(mount, req);
    if (trace_level >= TRACE_OPS_NOISY) printf("Private id is: %d\n", priv_id);
    priv_req = &mount->requests[priv_id];
    priv_req->page = buf;

    /* Dispatch AIO write request */
    bzero(&priv_req->aiocb, sizeof(struct aiocb));
    priv_req->aiocb.aio_fildes = req->u.fwrite.fd;
    priv_req->aiocb.aio_nbytes = req->u.fwrite.len;
    priv_req->aiocb.aio_offset = req->u.fwrite.offset;
    priv_req->aiocb.aio_buf = buf;
    assert(aio_write(&priv_req->aiocb) >= 0);

     
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;
}

void end_file_write(struct mount *mount, struct fs_request *priv_req)
{
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;
    int ret;

    /* Release the grant */
    assert(xc_gnttab_munmap(mount->gnth, priv_req->page, 1) == 0);
    
    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    req_id = priv_req->req_shadow.id; 
    if (trace_level >= TRACE_OPS_NOISY) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id;
    ret = aio_return(&priv_req->aiocb);
    if (ret < 0) ret = -errno;
    rsp->ret_val = (uint64_t)ret;
}

void dispatch_stat(struct mount *mount, struct fsif_request *req)
{
    struct fsif_stat *buf;
    struct stat statbuf;
    int fd, type;
    int ret = -1;
    uint16_t req_id;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    char *file_name = NULL;

    /* Read the request */
    buf = xc_gnttab_map_grant_ref(mount->gnth,
                                  mount->dom_id,
                                  req->u.fstat.gref,
                                  PROT_READ | PROT_WRITE);
   
    type = req->type;
    req_id = req->id;
    fd = req->u.fstat.fd;
    if (type == REQ_FSTAT)
    {
        if (trace_level >= TRACE_OPS) printf("File stat issued for FD=%d\n", fd);
    } else {
        file_name = (char *)buf;
        if (trace_level >= TRACE_OPS) printf("File stat issued for %s\n", file_name);
    }
   
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overridden by a response) */
    mount->ring.req_cons++;
   
    if ((type == REQ_FSTAT) || check_exported_eqok(mount, file_name, 1))
    {
        /* Stat, and create the response */ 
        if (type == REQ_FSTAT)
            ret = fstat(fd, &statbuf);
        else
        {
            ret = stat(file_name, &statbuf);
        }
        if (ret >= 0) {
		//	memcpy(buf, &statbuf, sizeof(statbuf));
			buf->st_mode = statbuf.st_mode;
			buf->st_size = statbuf.st_size;
			buf->st_blksize = statbuf.st_blksize;
			buf->st_blocks = statbuf.st_mode;
        }
    }

    /* Release the grant */
    assert(xc_gnttab_munmap(mount->gnth, buf, 1) == 0);
    
    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}


void dispatch_truncate(struct mount *mount, struct fsif_request *req)
{
    int fd, ret;
    uint16_t req_id;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    int64_t length;

    req_id = req->id;
    fd = req->u.ftruncate.fd;
    length = req->u.ftruncate.length;
    if (trace_level >= TRACE_OPS) printf("File truncate issued for FD=%d, length=%ld\n", fd, length); 
   
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;
   
    /* Stat, and create the response */ 
    ret = ftruncate(fd, length);

    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}

void dispatch_remove(struct mount *mount, struct fsif_request *req)
{
    char *file_name;
    int ret = -1;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching remove operation (gref=%d).\n", req->u.fremove.gref);
    /* Read the request, and open file */
    file_name = xc_gnttab_map_grant_ref(mount->gnth,
                                        mount->dom_id,
                                        req->u.fremove.gref,
                                        PROT_READ);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("File remove issued for %s\n", file_name); 
    if (check_exported(mount, file_name))
    {
        ret = remove(file_name);
        if (trace_level >= TRACE_OPS_NOISY) printf("Got ret: %d\n", ret);
    }
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    assert(xc_gnttab_munmap(mount->gnth, file_name, 1) == 0);
    mount->ring.req_cons++;


    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}


void dispatch_rename(struct mount *mount, struct fsif_request *req)
{
    char *buf, *old_file_name, *new_file_name;
    int ret = -1;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching rename operation (gref=%d).\n", req->u.fremove.gref);
    /* Read the request, and open file */
    buf = xc_gnttab_map_grant_ref(mount->gnth,
                                  mount->dom_id,
                                  req->u.frename.gref,
                                  PROT_READ);
   
    req_id = req->id;
    old_file_name = buf + req->u.frename.old_name_offset;
    new_file_name = buf + req->u.frename.new_name_offset;
    if (trace_level >= TRACE_OPS) printf("File rename issued for %s -> %s (buf=%s)\n", 
            old_file_name, new_file_name, buf);

    if (check_exported(mount, old_file_name) && check_exported(mount, new_file_name))
    {
        ret = rename(old_file_name, new_file_name);
        if (trace_level >= TRACE_OPS_NOISY) printf("Got ret: %d\n", ret);
    }
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    assert(xc_gnttab_munmap(mount->gnth, buf, 1) == 0);
    mount->ring.req_cons++;


    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}


void dispatch_create(struct mount *mount, struct fsif_request *req)
{
    char *file_name;
    int ret = -1;
    int8_t directory;
    int32_t mode;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching file create operation (gref=%d).\n", req->u.fcreate.gref);
    /* Read the request, and create file/directory */
    mode = req->u.fcreate.mode;
    directory = req->u.fcreate.directory;
    file_name = xc_gnttab_map_grant_ref(mount->gnth,
                                        mount->dom_id,
                                        req->u.fcreate.gref,
                                        PROT_READ);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("File create issued for %s\n", file_name); 
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;

    if (check_exported(mount, file_name))
    {
        if(directory)
        {
            if (trace_level >= TRACE_OPS_NOISY) printf("Issuing create for directory: %s\n", file_name);
            ret = mkdir(file_name, mode);
        }
        else
        {
            if (trace_level >= TRACE_OPS_NOISY) printf("Issuing create for file: %s\n", file_name);
            ret = open(file_name, O_CREAT | O_RDWR | O_EXCL, mode);
            // returns an open file descriptor if successful 
            if (ret >= 0) {
              close(ret);
              ret = 0;
            } else {
	      if (errno == EEXIST) ret = -2;
	    }
        }
        if (trace_level >= TRACE_OPS_NOISY) printf("Got ret %d (errno=%d)\n", ret, errno);
    }
    assert(xc_gnttab_munmap(mount->gnth, file_name, 1) == 0);

    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}

void dispatch_list(struct mount *mount, struct fsif_request *req)
{
    char *file_name, *buf;
    uint32_t offset, nr_files, error_code; 
    uint64_t ret_val;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;
    DIR *dir;
    struct dirent *dirent = NULL;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching list operation (gref=%d).\n", req->u.flist.gref);
    /* Read the request, and list directory */
    offset = req->u.flist.offset;
    buf = file_name = xc_gnttab_map_grant_ref(mount->gnth,
                                        mount->dom_id,
                                        req->u.flist.gref,
                                        PROT_READ | PROT_WRITE);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("Dir list issued for %s, offset %d\n", file_name, offset); 
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overridden by a response) */
    mount->ring.req_cons++;

    ret_val = 0;
    nr_files = 0;
    if (check_exported_eqok(mount, file_name, 1))
    {
        dir = opendir(file_name);
        if(dir == NULL)
        {
            error_code = errno;
            goto error_out;
        }
        /* Skip offset dirs */
        dirent = readdir(dir);
        while(offset-- > 0 && dirent != NULL)
            dirent = readdir(dir);
        /* If there was any error with reading the directory, errno will be set */
        error_code = errno;
        /* Copy file names of the remaining non-NULL dirents into buf */
        assert(NAME_MAX < PAGE_SIZE >> 1);
        while(dirent != NULL && 
                (PAGE_SIZE - ((unsigned long)buf & (PAGE_SIZE - 1))) > NAME_MAX)
        {
            int curr_length = strlen(dirent->d_name) + 1;        
            memcpy(buf, dirent->d_name, curr_length);
            buf += curr_length;
            dirent = readdir(dir);
            error_code = errno;
            nr_files++;
        }
error_out:    
        ret_val = ((nr_files << NR_FILES_SHIFT) & NR_FILES_MASK) | 
                  ((error_code << ERROR_SHIFT) & ERROR_MASK) | 
                  (dirent != NULL ? HAS_MORE_FLAG : 0);
    }
    assert(xc_gnttab_munmap(mount->gnth, file_name, 1) == 0);
    
    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = ret_val;
}

void dispatch_chmod(struct mount *mount, struct fsif_request *req)
{
    int fd, ret;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;
    int32_t mode;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching file chmod operation (fd=%d, mode=%o).\n", 
            req->u.fchmod.fd, req->u.fchmod.mode);
    req_id = req->id;
    fd = req->u.fchmod.fd;
    mode = req->u.fchmod.mode;
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;

    if (trace_level >= TRACE_OPS) printf("chmod issued for fd=%d, mode=%o\n", fd, mode);
    ret = fchmod(fd, mode); 

    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}

void dispatch_fs_space(struct mount *mount, struct fsif_request *req)
{
    char *file_name;
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;
    struct statfs stat;
    int64_t ret = -1;

    if (trace_level >= TRACE_OPS_NOISY) printf("Dispatching fs space operation (gref=%d).\n", req->u.fspace.gref);
    /* Read the request, and open file */
    file_name = xc_gnttab_map_grant_ref(mount->gnth,
                                        mount->dom_id,
                                        req->u.fspace.gref,
                                        PROT_READ);
   
    req_id = req->id;
    if (trace_level >= TRACE_OPS) printf("Fs space issued for %s\n", file_name); 
    if (check_exported(mount, file_name))
    {
        ret = statfs(file_name, &stat);
        if(ret >= 0)
            ret = stat.f_bsize * stat.f_bfree;
    }

    assert(xc_gnttab_munmap(mount->gnth, file_name, 1) == 0);
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;

    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)ret;
}

void dispatch_file_sync(struct mount *mount, struct fsif_request *req)
{
    int fd;
    uint16_t req_id;
    unsigned short priv_id;
    struct fs_request *priv_req;

    req_id = req->id;
    fd = req->u.fsync.fd;
    if (trace_level >= TRACE_OPS) printf("File sync issued for FD=%d\n", fd); 
   
    priv_id = get_request(mount, req);
    if (trace_level >= TRACE_OPS_NOISY) printf("Private id is: %d\n", priv_id);
    priv_req = &mount->requests[priv_id];

    /* Dispatch AIO read request */
    bzero(&priv_req->aiocb, sizeof(struct aiocb));
    priv_req->aiocb.aio_fildes = fd;
    assert(aio_fsync(O_SYNC, &priv_req->aiocb) >= 0);

     
    /* We can advance the request consumer index, from here on, the request
     * should not be used (it may be overrinden by a response) */
    mount->ring.req_cons++;
}

void end_file_sync(struct mount *mount, struct fs_request *priv_req)
{
    RING_IDX rsp_idx;
    fsif_response_t *rsp;
    uint16_t req_id;

    /* Get a response from the ring */
    rsp_idx = mount->ring.rsp_prod_pvt++;
    req_id = priv_req->req_shadow.id; 
    if (trace_level >= TRACE_RING) printf("Writing response at: idx=%d, id=%d\n", rsp_idx, req_id);
    rsp = RING_GET_RESPONSE(&mount->ring, rsp_idx);
    rsp->id = req_id; 
    rsp->ret_val = (uint64_t)aio_return(&priv_req->aiocb);
}

struct fs_op fopen_op     = {.type             = REQ_FILE_OPEN,
                             .dispatch_handler = dispatch_file_open,
                             .response_handler = NULL};
struct fs_op fclose_op    = {.type             = REQ_FILE_CLOSE,
                             .dispatch_handler = dispatch_file_close,
                             .response_handler = NULL};
struct fs_op fread_op     = {.type             = REQ_FILE_READ,
                             .dispatch_handler = dispatch_file_read,
                             .response_handler = end_file_read};
struct fs_op fwrite_op    = {.type             = REQ_FILE_WRITE,
                             .dispatch_handler = dispatch_file_write,
                             .response_handler = end_file_write};
struct fs_op fstat_op     = {.type             = REQ_STAT,
                             .dispatch_handler = dispatch_stat,
                             .response_handler = NULL};
struct fs_op ftruncate_op = {.type             = REQ_FILE_TRUNCATE,
                             .dispatch_handler = dispatch_truncate,
                             .response_handler = NULL};
struct fs_op fremove_op   = {.type             = REQ_REMOVE,
                             .dispatch_handler = dispatch_remove,
                             .response_handler = NULL};
struct fs_op frename_op   = {.type             = REQ_RENAME,
                             .dispatch_handler = dispatch_rename,
                             .response_handler = NULL};
struct fs_op fcreate_op   = {.type             = REQ_CREATE,
                             .dispatch_handler = dispatch_create,
                             .response_handler = NULL};
struct fs_op flist_op     = {.type             = REQ_DIR_LIST,
                             .dispatch_handler = dispatch_list,
                             .response_handler = NULL};
struct fs_op fchmod_op    = {.type             = REQ_CHMOD,
                             .dispatch_handler = dispatch_chmod,
                             .response_handler = NULL};
struct fs_op fspace_op    = {.type             = REQ_FS_SPACE,
                             .dispatch_handler = dispatch_fs_space,
                             .response_handler = NULL};
struct fs_op fsync_op     = {.type             = REQ_FILE_SYNC,
                             .dispatch_handler = dispatch_file_sync,
                             .response_handler = end_file_sync};
struct fs_op ffstat_op     = {.type             = REQ_FSTAT,
                             .dispatch_handler = dispatch_stat,
                             .response_handler = NULL};


struct fs_op *fsops[] = {&fopen_op, 
                         &fclose_op, 
                         &fread_op, 
                         &fwrite_op, 
                         &fstat_op, 
                         &ftruncate_op, 
                         &fremove_op, 
                         &frename_op, 
                         &fcreate_op, 
                         &flist_op, 
                         &fchmod_op, 
                         &fspace_op, 
                         &fsync_op,
			 &ffstat_op,
                         NULL};
