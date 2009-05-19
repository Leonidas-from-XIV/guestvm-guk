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
 * Sibling guest file system support.

 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 * Changes: Mick Jordan, Sun Microsystems, Inc..
 */
#ifndef __FS_H__
#define __FS_H__

#include <os.h>
#include <fsif.h>
#include <list.h>

#define FSTEST_CMDLINE   "fstest"

struct fs_import 
{
    domid_t dom_id;                 /* dom id of the exporting domain       */ 
    u16 export_id;                  /* export id (exporting dom specific)   */
    char *path;                     /* path of exported fs */             
    u16 import_id;                  /* import id (specific to this domain)  */ 
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

void init_fs_frontend(int fstest);

int guk_fs_open(struct fs_import *import, char *file, int flags);
int guk_fs_close(struct fs_import *import, int fd);
size_t guk_fs_read(struct fs_import *import, int fd, void *buf, 
               ssize_t len, ssize_t offset);
ssize_t guk_fs_write(struct fs_import *import, int fd, void *buf, 
                 ssize_t len, ssize_t offset);
int guk_fs_fstat(struct fs_import *import, 
            int fd, 
            struct fsif_stat_response *stat);
int guk_fs_stat(struct fs_import *import, 
            char *file, 
            struct fsif_stat_response *stat);
int guk_fs_truncate(struct fs_import *import, 
                int fd, 
                int64_t length);
int guk_fs_remove(struct fs_import *import, char *file);
int guk_fs_rename(struct fs_import *import, 
              char *old_file_name, 
              char *new_file_name);
int guk_fs_create(struct fs_import *import, char *name, 
              int8_t directory, int32_t mode);
char** guk_fs_list(struct fs_import *import, char *name, 
               int32_t offset, int32_t *nr_files, int *has_more);
int guk_fs_chmod(struct fs_import *import, int fd, int32_t mode);
int64_t guk_fs_space(struct fs_import *import, char *location);
int guk_fs_sync(struct fs_import *import, int fd);

struct list_head *guk_fs_get_imports(void);

#endif
