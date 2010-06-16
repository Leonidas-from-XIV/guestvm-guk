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

/* Changes: Mick Jordan, Sun Microsystems, Inc.
 */

#ifndef _MM_H_
#define _MM_H_

#if defined(__i386__)
#include <xen/arch-x86_32.h>
#elif defined(__x86_64__)
#include <xen/arch-x86_64.h>
#else
#error "Unsupported architecture"
#endif

#include <guk/arch_mm.h>

#include <lib.h>

/* allocation type values that match Maxine VirtualMemory.Type */
#define HEAP_VM 0
#define STACK_VM 1
#define CODE_VM 2
#define DATA_VM 3
#define PAGE_FRAME_VM 4

void init_mm(char *cmd_line);
void arch_init_mm(unsigned long* start_pfn_p, unsigned long* max_pfn_p);

extern unsigned long guk_pagetable_base(void);

/* allocate n contiguous pages of given type */
unsigned long guk_allocate_pages(int n, int type);
/* deallocate n contiguous pages at pointer of given type */
void guk_deallocate_pages(void *pointer, int n, int type);
/* attempt to extend an existing allocation */
unsigned long guk_extend_allocate_pages(void *pointer, int n, int type);

/* These two variants are only called within the microkernel and not from Java */
/* allocate 2^order contiguous pages */
unsigned long alloc_pages(int order);
#define alloc_page()    alloc_pages(0)

/* free 2^order  contiguous pages at pointer */
void guk_free_pages(void *pointer, int order);
#define free_page(_pointer)    guk_free_pages(_pointer, 0)
#define free_pages guk_free_pages

static __inline__ int get_order(unsigned long size)
{
    int order;
    size = (size-1) >> PAGE_SHIFT;
    for ( order = 0; size; order++ )
        size >>= 1;
    return order;
}

/* allocate n contiguous 2MB machine pages, aligned on a 2MB boundary */
unsigned long guk_allocate_2mb_machine_pages(int n, int type);

struct pfn_alloc_env;
typedef struct pfn_alloc_env pfn_alloc_env_t;

/* Instances of this object are passed to build_pagetable and allow
 * choice in how physical memory is used both to map virtual memory
 * and also to allocate new page table frames as necessary.
 * pfn is available for use to record position, e.g., in a linear allocation
 * and pfn_alloc is the function that actually returns the physical
 * page to be used for the given virtual address addr.
 * A return value of zero is interpreted as a request to not map that page.
 * A return value of < 0 implies an allocation error.
 * The data field can be used for arbitrary custom state as needed.
 * N.B. pfn_alloc actually returns the machine page, i.e., typically the
 * result of calling pfn_to_mfn, since this is what build_pagetable
 * actually wants. It also allows foreign machine frames, e.g., grant table
 * frames, to be mapped.
 */
struct pfn_alloc_env {
    void *data;
    long pfn;
    long (*pfn_alloc)(pfn_alloc_env_t *env, unsigned long addr);
};
/* This function simply returns physical memory in a linear manner starting at env->pfn.
 * This does not allocate so cannot fail.
 */
long guk_pfn_linear_alloc(pfn_alloc_env_t *env, unsigned long addr);
/* This function allocates an already mapped page as the physical memory to use.
 * Returns -1 if the allocation fails.
 */
long guk_pfn_alloc_alloc(pfn_alloc_env_t *env, unsigned long addr);

/* Build 4K page tables for given address range.
 * Returns 1 if successful, 0 on allocation failure.
 */
int guk_build_pagetable(unsigned long start_address, unsigned long end_address,
		     pfn_alloc_env_t *env_pfn, pfn_alloc_env_t *env_npf);
/* Build 2MB page tables for given address range.
 * Returns 1 if successful, 0 on allocation failure.
  */
int guk_build_pagetable_2mb(unsigned long start_address, unsigned long end_address,
		     pfn_alloc_env_t *env_pfn, pfn_alloc_env_t *env_npf);
/* Reverse operations, tear down existing tables for given address range */
void guk_demolish_pagetable(unsigned long start_address, unsigned long end_address);
void guk_demolish_pagetable_2mb(unsigned long start_address, unsigned long end_address);

/* write protect pages in given address range */
void guk_write_protect(unsigned long start_address, unsigned long end_address);
void guk_write_protect_2mb(unsigned long start_address, unsigned long end_address);

/* If possible, increase the page pool by extending the memory reservation */
long guk_increase_page_pool(unsigned long pages);
/* If possible, decrease the page pool by reducing the memory reservation */
long guk_decrease_page_pool(unsigned long pages);

/* Returns the total amount of (bulk) memory that could be released from the page pool */
unsigned long guk_decreaseable_page_pool(void);
unsigned long guk_current_reservation(void);
unsigned long guk_maximum_reservation(void);
unsigned long guk_maximum_ram_page(void);
unsigned long guk_total_free_pages(void);
unsigned long guk_bulk_free_pages(void);
long guk_page_pool_start(void);
long guk_page_pool_end(void);
void *guk_page_pool_bitmap(void);
void guk_dump_page_pool_state(void);
void *guk_machine_page_pool_bitmap(void);

/* The unmap functions unset the PAGE_PRESENT bit in the page table entry.
 * The machine frame part is not cleared. To do that use clear_pte.
 */

/* unmap page at virtual address addr, assumed mapped 1-1 with physical */
extern int guk_unmap_page(unsigned long addr);
/* remap page at virtual address addr, assumed mapped 1-1 with physical */
extern int guk_remap_page(unsigned long addr);
/* unmap page at virtual address addr, currently mapped to physical pfn */
extern int guk_unmap_page_pfn(unsigned long addr, unsigned long pfn);
/* remap page at virtual address addr to physical pfn */
extern int guk_remap_page_pfn(unsigned long addr, unsigned long pfn);
/* return pfn used to map virtual address addr, assuming not mapped 1-1,
 * i.e., virt_to_pfn cannot be used, but pagetables must be traversed.
 * Returns -1 if the address cannot be resolved.
 * Sets *pte to the actual page table entry.
 */
extern long guk_not11_virt_to_pfn(unsigned long addr, unsigned long *pte);

extern int guk_clear_pte(unsigned long addr);

/* return non-zero if addr is mapped, zero otherwise. */
extern int guk_validate(unsigned long addr);

extern unsigned long guk_pfn_to_mfn(unsigned long pfn);
extern unsigned long guk_mfn_to_pfn(unsigned long mfn);

#define allocate_pages guk_allocate_pages
#define deallocate_pages guk_deallocate_pages
#define extend_allocate_pages guk_extend_allocate_pages
#define free_pages guk_free_pages
#define pfn_linear_alloc guk_pfn_linear_alloc
#define pfn_alloc_alloc guk_pfn_alloc_alloc
#define build_pagetable guk_build_pagetable
#define demolish_pagetable guk_demolish_pagetable
#define maximum_ram_page guk_maximum_ram_page
#define validate guk_validate


#endif /* _MM_H_ */
