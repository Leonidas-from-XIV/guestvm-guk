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
 *
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * Copyright (c) 2005, Keir A Fraser
 *
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

#ifndef _ARCH_MM_H_
#define _ARCH_MM_H_

#include <xen/xen.h>

#if defined(__i386__)
#include <xen/arch-x86_32.h>
#elif defined(__x86_64__)
#include <xen/arch-x86_64.h>
#else
#error "Unsupported architecture"
#endif

#define L1_FRAME                1
#define L2_FRAME                2
#define L3_FRAME                3

#define L1_PAGETABLE_SHIFT      12

#if defined(__i386__)

#if !defined(CONFIG_X86_PAE)

#define L2_PAGETABLE_SHIFT      22

#define L1_PAGETABLE_ENTRIES    1024
#define L2_PAGETABLE_ENTRIES    1024

#define PADDR_BITS              32
#define PADDR_MASK              (~0UL)

#define NOT_L1_FRAMES           1
#define PRIpte "08lx"
typedef unsigned long pgentry_t;

#else /* defined(CONFIG_X86_PAE) */

#define L2_PAGETABLE_SHIFT      21
#define L3_PAGETABLE_SHIFT      30

#define L1_PAGETABLE_ENTRIES    512
#define L2_PAGETABLE_ENTRIES    512
#define L3_PAGETABLE_ENTRIES    4

#define PADDR_BITS              44
#define PADDR_MASK              ((1ULL << PADDR_BITS)-1)

#define L2_MASK  ((1UL << L3_PAGETABLE_SHIFT) - 1)

/*
 * If starting from virtual address greater than 0xc0000000,
 * this value will be 2 to account for final mid-level page
 * directory which is always mapped in at this location.
 */
#define NOT_L1_FRAMES           3
#define PRIpte "016llx"
typedef uint64_t pgentry_t;

#endif /* !defined(CONFIG_X86_PAE) */

#elif defined(__x86_64__)

#define L2_PAGETABLE_SHIFT      21
#define L3_PAGETABLE_SHIFT      30
#define L4_PAGETABLE_SHIFT      39

#define L1_PAGETABLE_ENTRIES    512
#define L2_PAGETABLE_ENTRIES    512
#define L3_PAGETABLE_ENTRIES    512
#define L4_PAGETABLE_ENTRIES    512

/* These are page-table limitations. Current CPUs support only 40-bit phys. */
#define PADDR_BITS              52
#define VADDR_BITS              48
#define PADDR_MASK              ((1UL << PADDR_BITS)-1)
#define VADDR_MASK              ((1UL << VADDR_BITS)-1)

#define L2_MASK  ((1UL << L3_PAGETABLE_SHIFT) - 1)
#define L3_MASK  ((1UL << L4_PAGETABLE_SHIFT) - 1)

#define NOT_L1_FRAMES           3
#define PRIpte "016lx"
typedef unsigned long pgentry_t;

#endif

#define L1_MASK  ((1UL << L2_PAGETABLE_SHIFT) - 1)

/* Given a virtual address, get an entry offset into a page table. */
#define l1_table_offset(_a) \
  (((_a) >> L1_PAGETABLE_SHIFT) & (L1_PAGETABLE_ENTRIES - 1))
#define l2_table_offset(_a) \
  (((_a) >> L2_PAGETABLE_SHIFT) & (L2_PAGETABLE_ENTRIES - 1))
#if defined(__x86_64__) || defined(CONFIG_X86_PAE)
#define l3_table_offset(_a) \
  (((_a) >> L3_PAGETABLE_SHIFT) & (L3_PAGETABLE_ENTRIES - 1))
#endif
#if defined(__x86_64__)
#define l4_table_offset(_a) \
  (((_a) >> L4_PAGETABLE_SHIFT) & (L4_PAGETABLE_ENTRIES - 1))
#endif

#define _PAGE_PRESENT  0x001UL
#define _PAGE_RW       0x002UL
#define _PAGE_USER     0x004UL
#define _PAGE_PWT      0x008UL
#define _PAGE_PCD      0x010UL
#define _PAGE_ACCESSED 0x020UL
#define _PAGE_DIRTY    0x040UL
#define _PAGE_PAT      0x080UL
#define _PAGE_PSE      0x080UL
#define _PAGE_GLOBAL   0x100UL

#if defined(__i386__)
#define L1_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED)
#define L2_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_DIRTY |_PAGE_USER)
#if defined(CONFIG_X86_PAE)
#define L3_PROT (_PAGE_PRESENT)
#endif /* CONFIG_X86_PAE */
#elif defined(__x86_64__)
#define L1_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_USER)
#define L2_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_DIRTY|_PAGE_USER)
#define L3_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_DIRTY|_PAGE_USER)
#define L4_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_DIRTY|_PAGE_USER)
#endif /* __i386__ || __x86_64__ */

#ifndef CONFIG_X86_PAE
#define PAGE_SIZE       (1UL << L1_PAGETABLE_SHIFT)
#else
#define PAGE_SIZE       (1ULL << L1_PAGETABLE_SHIFT)
#endif
#define PAGE_SHIFT      L1_PAGETABLE_SHIFT
#define PAGE_MASK       (~(PAGE_SIZE-1))

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> L1_PAGETABLE_SHIFT)
#define PFN_DOWN(x)	((x) >> L1_PAGETABLE_SHIFT)
#define PFN_PHYS(x)	((x) << L1_PAGETABLE_SHIFT)
#define PHYS_PFN(x)	((x) >> L1_PAGETABLE_SHIFT)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)        (((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* Definitions for machine and pseudophysical addresses. */
#ifdef CONFIG_X86_PAE
typedef unsigned long long paddr_t;
typedef unsigned long long maddr_t;
#else
typedef unsigned long paddr_t;
typedef unsigned long maddr_t;
#endif

extern unsigned long *phys_to_machine_mapping;
extern char _text, _etext, _edata, _end;
#define pfn_to_mfn(_pfn) (phys_to_machine_mapping[(_pfn)])
static __inline__ maddr_t phys_to_machine(paddr_t phys)
{
	maddr_t machine = pfn_to_mfn(phys >> PAGE_SHIFT);
	machine = (machine << PAGE_SHIFT) | (phys & ~PAGE_MASK);
	return machine;
}

#define mfn_to_pfn(_mfn) (machine_to_phys_mapping[(_mfn)])
static __inline__ paddr_t machine_to_phys(maddr_t machine)
{
	paddr_t phys = mfn_to_pfn(machine >> PAGE_SHIFT);
	phys = (phys << PAGE_SHIFT) | (machine & ~PAGE_MASK);
	return phys;
}

/* These macros all assume (modulo the VIRT_START offset) that virtual and physical
 * memory are mapped 1-1.
 */

#define VIRT_START                 ((unsigned long)&_text)

#define to_phys(x)                 ((unsigned long)(x)-VIRT_START)
#define to_virt(x)                 ((void *)((unsigned long)(x)+VIRT_START))
#define to_virtu(x)                ((unsigned long)(x)+VIRT_START)

#define virt_to_pfn(_virt)         (PFN_DOWN(to_phys(_virt)))
#define virt_to_mfn(_virt)         (pfn_to_mfn(virt_to_pfn(_virt)))
#define mach_to_virt(_mach)        (to_virt(machine_to_phys(_mach)))
#define virt_to_mach(_virt)        (phys_to_machine(to_phys(_virt)))
#define mfn_to_virt(_mfn)          (to_virt(mfn_to_pfn(_mfn) << PAGE_SHIFT))
#define pfn_to_virt(_pfn)          (to_virt((_pfn) << PAGE_SHIFT))
#define pfn_to_virtu(_pfn)         (to_virtu((_pfn) << PAGE_SHIFT))

/* Pagetable walking. */
#define pte_to_mfn(_pte)           (((_pte) & (PADDR_MASK&PAGE_MASK)) >> L1_PAGETABLE_SHIFT)
#define pte_to_virt(_pte)          to_virt(mfn_to_pfn(pte_to_mfn(_pte)) << PAGE_SHIFT)

#if !defined(CONFIG_X86_PAE)
#define __pte(x) ((pte_t) { (x) } )
#else
#define __pte(x) ({ unsigned long long _x = (x);        \
    ((pte_t) {(unsigned long)(_x), (unsigned long)(_x>>32)}); })
#endif

#define round_pgdown(_p)  ((_p)&PAGE_MASK)
#define round_pgup(_p)    (((_p)+(PAGE_SIZE-1))&PAGE_MASK)


#endif /* _ARCH_MM_H_ */
