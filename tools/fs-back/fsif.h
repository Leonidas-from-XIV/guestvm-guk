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
 * This defines the packets that traverse the ring between the fs frontend and backend.
 * 
 * Author: Grzegorz Milos
 *         Mick Jordan
 * 
 */

#ifndef __FSIF_H__
#define __FSIF_H__

#include <xen/io/ring.h>
#include <xen/grant_table.h>

#define REQ_FILE_OPEN        1
#define REQ_FILE_CLOSE       2
#define REQ_FILE_READ        3
#define REQ_FILE_WRITE       4
#define REQ_FSTAT            5
#define REQ_FILE_TRUNCATE    6
#define REQ_REMOVE           7
#define REQ_RENAME           8
#define REQ_CREATE           9
#define REQ_DIR_LIST        10
#define REQ_CHMOD           11
#define REQ_FS_SPACE        12
#define REQ_FILE_SYNC       13
#define REQ_STAT            14

struct fsif_open_request {
    grant_ref_t gref;
    int32_t flags;
};

struct fsif_close_request {
    int32_t fd;
};

struct fsif_read_request {
    int32_t fd;
    grant_ref_t gref;
    uint64_t len;
    uint64_t offset;
};

struct fsif_write_request {
    int32_t fd;
    grant_ref_t gref;
    uint64_t len;
    uint64_t offset;
};

struct fsif_stat_request {
    int32_t fd;
    grant_ref_t gref;
};

struct fsif_truncate_request {
    int32_t fd;
    uint32_t pad;
    int64_t length;
};

struct fsif_remove_request {
    grant_ref_t gref;
};

struct fsif_rename_request {
    uint16_t old_name_offset;
    uint16_t new_name_offset;
    grant_ref_t gref;
};

struct fsif_create_request {
    int32_t directory;
    int32_t mode;
    grant_ref_t gref;
};

struct fsif_list_request {
    uint32_t offset;
    grant_ref_t gref;
};

#define NR_FILES_SHIFT  0
#define NR_FILES_SIZE   16   /* 16 bits for the number of files mask */
#define NR_FILES_MASK   (((1ULL << NR_FILES_SIZE) - 1) << NR_FILES_SHIFT)
#define ERROR_SIZE      32   /* 32 bits for the error mask */
#define ERROR_SHIFT     (NR_FILES_SIZE + NR_FILES_SHIFT)
#define ERROR_MASK      (((1ULL << ERROR_SIZE) - 1) << ERROR_SHIFT)
#define HAS_MORE_SHIFT  (ERROR_SHIFT + ERROR_SIZE)    
#define HAS_MORE_FLAG   (1ULL << HAS_MORE_SHIFT)

struct fsif_chmod_request {
    int32_t fd;
    int32_t mode;
};

struct fsif_space_request {
    grant_ref_t gref;
};

struct fsif_sync_request {
    int32_t fd;
};

struct fsif_stat {
	int32_t    st_mode;    /* protection */
	int32_t    st_size;    /* total size, in bytes */
	int32_t    st_blksize; /* blocksize for filesystem I/O */
	int32_t   st_blocks;  /* number of blocks allocated */
        int64_t  st_atim;
        int64_t  st_mtim;
        int64_t  st_ctim;
};

/* FS operation request */
struct fsif_request {
    uint8_t type;                 /* Type of the request                  */
    uint8_t pad1;
    uint16_t id;                  /* Request ID, copied to the response   */
    uint32_t pad2;
    union {
        struct fsif_open_request     fopen;
        struct fsif_close_request    fclose;
        struct fsif_read_request     fread;
        struct fsif_write_request    fwrite;
        struct fsif_stat_request     fstat;
        struct fsif_truncate_request ftruncate;
        struct fsif_remove_request   fremove;
        struct fsif_rename_request   frename;
        struct fsif_create_request   fcreate;
        struct fsif_list_request     flist;
        struct fsif_chmod_request    fchmod;
        struct fsif_space_request    fspace;
        struct fsif_sync_request     fsync;
    } u;
};
typedef struct fsif_request fsif_request_t;

/* FS operation response */
struct fsif_response {
    uint16_t id;
    uint16_t pad1;
    uint32_t pad2;
    uint64_t ret_val;
};

typedef struct fsif_response fsif_response_t;


DEFINE_RING_TYPES(fsif, struct fsif_request, struct fsif_response);

#define STATE_INITIALISED     "init"
#define STATE_READY           "ready"



#endif
