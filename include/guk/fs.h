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
 * Sibling guest file system support.

 * Author: Grzegorz Milos
 * Changes: Mick Jordan
 */
#ifndef __FS_H__
#define __FS_H__

#include <guk/os.h>
#include <types.h>
#include <list.h>
#include <fsif.h>

#define FSTEST_CMDLINE   "fstest"

struct fs_import {
    domid_t dom_id;                 /* dom id of the exporting domain       */ 
    uint16_t export_id;                  /* export id (exporting dom specific)   */
    char *path;                     /* path of exported fs */             
    uint16_t import_id;                  /* import id (specific to this domain)  */ 
    struct list_head list;          /* list of all imports                  */
    unsigned int nr_entries;        /* Number of entries in rings & request
                                       array                                */
    struct fsif_front_ring ring;    /* frontend ring (contains shared ring) */
    int gnt_ref;                    /* grant reference to the shared ring   */
    unsigned int local_port;        /* local event channel port             */
    char *backend;                  /* XenBus location of the backend       */
    struct fs_request *requests;    /* Table of requests                    */
    unsigned short *freelist;       /* List of free request ids             */
};


int     guk_fs_open(struct fs_import *, const char *file, int flags);
int     guk_fs_close(struct fs_import *, int fd);
ssize_t guk_fs_read(struct fs_import *, int fd, void *buf, ssize_t len, ssize_t offset);
ssize_t guk_fs_write(struct fs_import *, int fd, const void *buf, ssize_t len, ssize_t offset);
int     guk_fs_fstat(struct fs_import *, int fd, struct fsif_stat *buf);
int     guk_fs_stat(struct fs_import *, const char *file, struct fsif_stat *buf);
int     guk_fs_truncate(struct fs_import *, int fd, int64_t length);
int     guk_fs_remove(struct fs_import *, char *file);
int     guk_fs_rename(struct fs_import *, char *old_file_name, char *new_file_name);
int     guk_fs_create(struct fs_import *, char *name, int8_t directory, int32_t mode);
int     guk_fs_fchmod(struct fs_import *, int fd, int32_t mode);
int64_t guk_fs_space(struct fs_import *, char *location);
int     guk_fs_sync(struct fs_import *, int fd);
char**  guk_fs_list(struct fs_import *, char *name, int32_t offset, int32_t *nr_files, int *has_more);
char*   guk_fs_import_path(struct fs_import *);

struct list_head *guk_fs_get_imports(void);
struct fs_import *guk_fs_get_next(struct fs_import *);

#endif
