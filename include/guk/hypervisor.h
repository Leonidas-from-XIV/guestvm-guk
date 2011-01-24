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
 * hypervisor.h
 * 
 * Hypervisor handling.
 * 
 *
 * Copyright (c) 2002, K A Fraser
 * Copyright (c) 2005, Grzegorz Milos
 * Updates: Aravindh Puthiyaparambil <aravindh.puthiyaparambil@unisys.com>
 * Updates: Dietmar Hahn <dietmar.hahn@fujitsu-siemens.com> for ia64
 */

#ifndef _HYPERVISOR_H_
#define _HYPERVISOR_H_

#include <types.h>
#include <xen/xen.h>
#if defined(__x86_64__)
#include <x86/x86_64/hypercall-x86_64.h>
#else
#error "Unsupported architecture"
#endif

#ifndef MAX_VIRT_CPUS
#define MAX_VIRT_CPUS 64
#endif

/*
 * a placeholder for the start of day information passed up from the hypervisor
 */
union start_info_union
{
    start_info_t start_info;
    char padding[512];
};
extern union start_info_union start_info_union;
#define start_info (start_info_union.start_info)

extern start_info_t *xen_info;
extern char shared_info[PAGE_SIZE];

/* hypervisor.c */
void mask_evtchn(u32 port);
void unmask_evtchn(u32 port);
void clear_evtchn(u32 port);

#endif /* __HYPERVISOR_H__ */
