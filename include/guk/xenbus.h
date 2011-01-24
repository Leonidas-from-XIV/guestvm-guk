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
#ifndef XENBUS_H__
#define XENBUS_H__

#include <xen/xen.h>

typedef unsigned long xenbus_transaction_t;
#define XBT_NIL ((xenbus_transaction_t)0)

/* Initialize the XenBus system. */
void init_xenbus(void);

void xenbus_suspend(void);
void xenbus_resume(void);

/* Read the value associated with a path.  Returns a malloc'd error
   string on failure and sets *value to NULL.  On success, *value is
   set to a malloc'd copy of the value. */
char *guk_xenbus_read(xenbus_transaction_t xbt, const char *path, char **value);
#define xenbus_read guk_xenbus_read

char *xenbus_watch_path(xenbus_transaction_t xbt, char *path, char *token);
char* xenbus_wait_for_value(char* token, char *path, char* value);
char * xenbus_read_watch(char *token);

int xenbus_rm_watch(char *token);

/* Associates a value with a path.  Returns a malloc'd error string on
   failure. */
char *xenbus_write(xenbus_transaction_t xbt, const char *path, const char *value);

/* Removes the value associated with a path.  Returns a malloc'd error
   string on failure. */
char *xenbus_rm(xenbus_transaction_t xbt, const char *path);

/* List the contents of a directory.  Returns a malloc'd error string
   on failure and sets *contents to NULL.  On success, *contents is
   set to a malloc'd array of pointers to malloc'd strings.  The array
   is NULL terminated.  May block. */
char *xenbus_ls(xenbus_transaction_t xbt, const char *prefix, char ***contents);

/* Reads permissions associated with a path.  Returns a malloc'd error
   string on failure and sets *value to NULL.  On success, *value is
   set to a malloc'd copy of the value. */
char *xenbus_get_perms(xenbus_transaction_t xbt, const char *path, char **value);

/* Sets the permissions associated with a path.  Returns a malloc'd
   error string on failure. */
char *xenbus_set_perms(xenbus_transaction_t xbt, const char *path, domid_t dom, char perm);

/* Start a xenbus transaction.  Returns the transaction in xbt on
   success or a malloc'd error string otherwise. */
char *xenbus_transaction_start(xenbus_transaction_t *xbt);

/* End a xenbus transaction.  Returns a malloc'd error string if it
   fails.  abort says whether the transaction should be aborted.
   Returns 1 in *retry iff the transaction should be retried. */
char *xenbus_transaction_end(xenbus_transaction_t, int abort,
			     int *retry);

/* Read path and parse it as an integer.  Returns -1 on error. */
int xenbus_read_integer(char *path);

char* xenbus_printf(xenbus_transaction_t xbt,
                                  char* node, char* path,
                                  char* fmt, ...);
/* get this domain's id */
domid_t xenbus_get_self_id(void);

#endif /* XENBUS_H__ */
