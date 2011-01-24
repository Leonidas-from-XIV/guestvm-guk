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
 * This is some support that is specific to the Maxine Java virtual machine
 * that is difficult to factor out completely. However, it is essentially harmless
 * in a non-Maxine environment.
 *
 * Maxine requires that R14 points to an area called the "local space".
 * Maxine compiled code assumes in particular that it can execute a MOV R14, [R14]
 * instruction at certain points as part of its GC safepoint mechanism.
 * Maxine threads set this up explictly, but in case a microkernel thread
 * execute some Java code, e.g., in response to an interrupt, we make sure
 * that R14 has a fake local space value for all microkernel threads.
 * 
 */

#ifndef _MAXINE_LS_H_
#define _MAXINE_LS_H_

void init_maxine(void);
void *get_local_space(void);

#endif /* _MAXINE_LS_H_ */

