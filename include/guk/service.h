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
 * service lists
 *
 * Author: Harald Roeck
 */

#ifndef _SERVICE_H_
#define _SERVICE_H_

#include <list.h>

struct service {
    char *name;
    int (*init)(void *arg);
    int (*shutdown)(void);
    int (*suspend)(void);
    int (*resume)(void);
    void *arg;
    
    struct list_head list;
};

extern void register_service(struct service *);
extern void start_services(void);
extern int suspend_services(void);
extern int resume_services(void);
extern int shutdown_services(void);

#endif /* _SERVICE_H_ */
