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
