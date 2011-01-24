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
#ifndef _P2M_H_
#define _P2M_H_

/* Initialize the p2m mapping table on domain startup.
 * max_pfn initial domain memory max
 * memmax_pfn maxmem domain memory
 */
void arch_init_p2m(unsigned long max_pfn, unsigned long maxmem_pfn);
/* Rebuild the p2m mapping table on domain restore */
void arch_rebuild_p2m(void);
/* Modify the p2m mapping table on memory decrease/increase. 
 */
void arch_update_p2m(unsigned long start_pfn, unsigned long end_pfn, int adding);

#endif //_P2M_H_
