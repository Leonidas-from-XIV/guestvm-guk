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
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: mm.c
 *      Author: Rolf Neugebauer
 *     Changes: Grzegorz Milos
 *     Changes: Harald Roeck
 *     Changes: Mick Jordan
 *
 *        Date: Aug 2003
 *
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: p2m
 *
 ****************************************************************************
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <guk/os.h>
#include <guk/hypervisor.h>
#include <guk/mm.h>
#include <guk/xmalloc.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <x86/arch_sched.h>
#include <xen/memory.h>

#include <types.h>
#include <lib.h>
/* This code maintains the pfn_to_mfn_frame_list in arch_shared_info
 * that is used by save/restore.
 *
 * This structure is two-level, not unlike a pagetable structure. 
 * The root of the table is a (l3) page of data containing up to 512 "pointers"
 * to (l2) pages of data that contain "pointers" into the phys_to_machine_mapping table.
 * Each entry in an l2 page is a "pointer" to one page of the phys_to_machine_mapping
 * table and successive l2 entries reference successive pages in the table.
 * A "pointer" is in fact a machine frame number and so cannot be dereferenced
 * directly. 
 *
 * E.g. If the domain has 64k pages (256MB), then there is one l3 entry and
 * 128 l2 entries (64k/512 = 128). So a full l2 page can manage 1GB.
 *
 * Since the phys_to_machine_mapping is now sized for the maximum memory on startup
 * we build the entire structure on startup, even though the max pfn may be less
 * than maxmem. 
 * 
 * On a restore we have to rebuild the table. The variables below
 * record enough state to make all this possible.
 *
 */

#define MAX_L2_PAGES 32 /* 32GB max domain memory */
#define ENTRIES_PER_L2_PAGE (PAGE_SIZE / sizeof(unsigned long))
#define PFNS_PER_L3_PAGE (256 * 1024)
/* Virtual address of the root of the table */
static unsigned long *pfn_to_mfn_table;
/* Array of virtual addresses of the l2 pages */
static unsigned long *l2_list_pages[MAX_L2_PAGES];
/* Current number of l2 pages */
static int num_l2_pages;
/* Current max pfn */
static unsigned long max_pfn_table;
static unsigned long maxmem_pfn_table;

/* This is called on resume */
void arch_rebuild_p2m(void)
{
    unsigned long *l2_list;
    unsigned long l1_offset, l2_offset, l3_offset;
    int i;
    
    l2_list = 0;
    i=0;
    for(l1_offset=0, l2_offset=0, l3_offset=0;
	    l1_offset < maxmem_pfn_table;
	    l1_offset += ENTRIES_PER_L2_PAGE, l2_offset++)
    {
	if(l2_offset % ENTRIES_PER_L2_PAGE == 0) {
	    l2_list = l2_list_pages[i++];
	    BUG_ON(i > num_l2_pages);
	    pfn_to_mfn_table[l3_offset] = virt_to_mfn(l2_list);
	    l3_offset++;
	}
	l2_list[l2_offset % ENTRIES_PER_L2_PAGE] =
	    virt_to_mfn(&phys_to_machine_mapping[l1_offset]);
    }

    HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
        virt_to_mfn(pfn_to_mfn_table);
    HYPERVISOR_shared_info->arch.max_pfn = max_pfn_table;
}

void dump_p2m(void) {
  unsigned long l1_offset, l2_offset, l3_offset;
  xprintk("num_l2_pages %d, max_pfn_table %d\n", num_l2_pages, max_pfn_table);
  for (l1_offset = 0, l3_offset=0; l3_offset < num_l2_pages; l3_offset++) {
    xprintk("l3[%d]: mfn %lx, pfn %lx l2t %lx\n", l3_offset,
	   pfn_to_mfn_table[l3_offset],
	   mfn_to_pfn(pfn_to_mfn_table[l3_offset]),
	   l2_list_pages[l3_offset]);
    unsigned long *l2_table = l2_list_pages[l3_offset];
    
    for (l2_offset = 0; l1_offset < max_pfn_table && l2_offset < ENTRIES_PER_L2_PAGE; l2_offset++) {
      xprintk("  l2[%d]: mfn %lx, pfn %lx", l2_offset, l2_table[l2_offset], mfn_to_pfn(l2_table[l2_offset]));
      if (l2_offset & 1) xprintk("\n"); else xprintk("  ");
      l1_offset += ENTRIES_PER_L2_PAGE;
    }
  }
  xprintk("\n");
}

void arch_update_p2m(unsigned long start_pfn, unsigned long end_pfn, int adding)
{
    unsigned long *l2_list;
    unsigned long l1_offset, l2_offset, l3_offset;
    int i = 0;

    if (adding) {
      /* adding entries */
      if (start_pfn == 0) {
	for(l1_offset=start_pfn, l2_offset=start_pfn / ENTRIES_PER_L2_PAGE, l3_offset=0;
	    l1_offset < end_pfn;
	    l1_offset += ENTRIES_PER_L2_PAGE, l2_offset++) {
	  /* Allocate new L2 page if needed */
	  if(l2_offset % ENTRIES_PER_L2_PAGE == 0) {
	    BUG_ON(num_l2_pages + i >= MAX_L2_PAGES);
	    l2_list = (unsigned long *)alloc_page();
	    l2_list_pages[num_l2_pages + i] = l2_list;
	    pfn_to_mfn_table[num_l2_pages + l3_offset] = virt_to_mfn(l2_list);
	    l3_offset++;
	    i++;
	  }
	  l2_list[l2_offset % ENTRIES_PER_L2_PAGE] =
	    virt_to_mfn(&phys_to_machine_mapping[l1_offset]);
	}
	num_l2_pages += i;
      }
      if (end_pfn > max_pfn_table) {
	max_pfn_table = end_pfn;
      }
    } else {
      /* removing entries */
      if (end_pfn == max_pfn_table) {
	max_pfn_table = start_pfn;
      }
    }
    HYPERVISOR_shared_info->arch.max_pfn = max_pfn_table;

    //if (start_pfn > 0) dump_p2m();
}

void arch_init_p2m(unsigned long max_pfn, unsigned long maxmem_pfn) {
  num_l2_pages = 0;
  pfn_to_mfn_table = (unsigned long *)alloc_page();
  HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list = virt_to_mfn(pfn_to_mfn_table);
  max_pfn_table = 0;
  /* Build table for maxmem */
  maxmem_pfn_table = maxmem_pfn;
  arch_update_p2m(0, maxmem_pfn, 1);
  /* Now set it's current limit */
  max_pfn_table = max_pfn;  
  HYPERVISOR_shared_info->arch.max_pfn = max_pfn_table;
  //dump_p2m();
}

