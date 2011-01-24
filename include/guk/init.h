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
 * automatic init features
 * needs a special section (called .GUK_init) in the linker script
 *
 * author: Harald Roeck
 */

#ifndef INIT_H
#define INIT_H

/* an init function is automatically executed during bootup; 
 * init functions should be static to the file they are declared in, take no arguments
 * and return 0 on success and non-zero on failure. init functions may not use any scheduler
 * related mechanisms like sleep or creating threads.
 */

/* type of an init function */
typedef int (*init_function)(void);

/* macro to put a pointer to the init function into a special section of the code */
#define DECLARE_INIT(func) \
    static init_function __init_##func __attribute__((__used__))\
           __attribute__((section (".GUK_init"))) = &func

/* pointers to the init section in the binary */
extern init_function __init_start[], __init_end[];

#endif /* INIT_H */
