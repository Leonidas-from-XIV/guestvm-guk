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
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: mm.c
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: Grzegorz Milos
 *     Changes: Harald Roeck, Sun Microsystems Inv., summer intern 2008
 *     Changes: Mick Jordan, Sun Microsystems Inc.
 *
 *        Date: Aug 2003, changes Aug 2005, 2008, 2009, 2010
 *
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: Arch specific memory management related functions (pagetables)
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
#include <guk/p2m.h>
#include <guk/xmalloc.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <xen/memory.h>

#include <types.h>
#include <lib.h>
#ifdef MM_DEBUG
#define DEBUG(_f, _a...) \
    xprintk("GUK(file=mm.c, line=%d) " _f "\n", __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

/* This function simply returns physical memory in a linear manner starting at env->pfn.
 */
long guk_pfn_linear_alloc(pfn_alloc_env_t *env, unsigned long addr) {
  return pfn_to_mfn(env->pfn++);
}

/* This function allocates an already mapped page as the physical memory to use.
 */
long guk_pfn_alloc_alloc(pfn_alloc_env_t *env, unsigned long addr) {
  unsigned long va = allocate_pages(1, PAGE_FRAME_VM);
  if (va == 0) {
	  return -1;
  }
  return pfn_to_mfn(virt_to_pfn(va));
}


static void new_pt_frame(unsigned long pt_mfn_for_pfn, unsigned long prev_l_mfn,
                                unsigned long offset, unsigned long level)
{
    pgentry_t *tab = (pgentry_t *)start_info.pt_base;
    unsigned long pt_pfn = mfn_to_pfn(pt_mfn_for_pfn);
    unsigned long pt_page = (unsigned long)pfn_to_virt(pt_pfn);
    unsigned long prot_e, prot_t, pincmd;
    mmu_update_t mmu_updates[1];
    struct mmuext_op pin_request;

    prot_e = prot_t = pincmd = 0;
    if (trace_mmpt()) {
        ttprintk("MM: allocating new L%d pt frame, pt_pfn=%lx, offset=%lx\n",
           level, pt_pfn, offset);
    }

    /* We need to clear the page, otherwise we might fail to map it
       as a page table page. Have to do it now before we make it RO. */
    memset((unsigned long*)pfn_to_virt(pt_pfn), 0, PAGE_SIZE);

    switch ( level )
    {
    case L1_FRAME:
         prot_e = L1_PROT;
         prot_t = L2_PROT;
         pincmd = MMUEXT_PIN_L1_TABLE;
         break;
#if defined(__x86_64__) || defined(CONFIG_X86_PAE)
    case L2_FRAME:
         prot_e = L2_PROT;
         prot_t = L3_PROT;
         pincmd = MMUEXT_PIN_L2_TABLE;
         break;
#endif
#if defined(__x86_64__)
    case L3_FRAME:
         prot_e = L3_PROT;
         prot_t = L4_PROT;
         pincmd = MMUEXT_PIN_L3_TABLE;
         break;
#endif
    default:
         xprintk("new_pt_frame() called with invalid level number %d\n", level);
         crash_exit();
         break;
    }

    /* Update the entry */
#if defined(__x86_64__)
    tab = pte_to_virt(tab[l4_table_offset(pt_page)]);
    tab = pte_to_virt(tab[l3_table_offset(pt_page)]);
#endif
#if defined(CONFIG_X86_PAE)
    tab = pte_to_virt(tab[l3_table_offset(pt_page)]);
#endif

    /* pt_pfn is currently mapped RW in the 1-1 virtual address space. To be used
       as a page table page, it must be mapped read-only, which is what we do here */
    mmu_updates[0].ptr = ((pgentry_t)tab[l2_table_offset(pt_page)] & PAGE_MASK) +
                         sizeof(pgentry_t) * l1_table_offset(pt_page);
    mmu_updates[0].val = (pgentry_t)pfn_to_mfn(pt_pfn) << PAGE_SHIFT |
                         (prot_e & ~_PAGE_RW);
    if(HYPERVISOR_mmu_update(mmu_updates, 1, NULL, DOMID_SELF) < 0)
    {
         xprintk("PTE for new page table page could not be updated\n");
         crash_exit();
    }

    /* Pin the page to provide correct protection */
    pin_request.cmd = pincmd;
    pin_request.arg1.mfn = pfn_to_mfn(pt_pfn);
    if(HYPERVISOR_mmuext_op(&pin_request, 1, NULL, DOMID_SELF) < 0)
    {
        xprintk("ERROR: pinning failed\n");
        crash_exit();
    }

    /* Here we actually update the referencing page table entry. */
    mmu_updates[0].ptr = ((pgentry_t)prev_l_mfn << PAGE_SHIFT) + sizeof(pgentry_t) * offset;
    mmu_updates[0].val = (pgentry_t)pfn_to_mfn(pt_pfn) << PAGE_SHIFT | prot_t;
    if(HYPERVISOR_mmu_update(mmu_updates, 1, NULL, DOMID_SELF) < 0)
    {
       xprintk("ERROR: mmu_update failed\n");
       crash_exit();
    }

    /* On return the new page table is zero'ed out, from earlier memset when it was still RW */

}

/* Build the pagetables for the virtual address range start_address .. end_address-1,
 * which is mapped to the physical page range returned by env_pfn->pfn_alloc
 * Free pages for new page frames are available from env_npf->pfn_alloc.
 *
 * Generally, virtual memory is mapped 1-1 to physical memory, i.e., env_pfn->pfn_alloc
 * returns the same value as virt_to_pfn(a) where a is the virtual address being mapped to
 * that page. Java thread stacks, however, are not mapped 1-1.
 *
 * Return 1 if successful, 0 on allocation failure.
 * Page table update failures cause a crash.
 */

static int build_pagetable_vs(unsigned long start_address, unsigned long end_address,
		     int page_size, pfn_alloc_env_t *env_pfn, pfn_alloc_env_t *env_npf)
{
    mmu_update_t mmu_updates[L1_PAGETABLE_ENTRIES + 1];
    pgentry_t *tab = (pgentry_t *)start_info.pt_base, page;
    unsigned long mfn;
    unsigned long offset;
    int count = 0;

    if (trace_mmpt()) ttprintk("MM: mapping memory range 0x%lx - 0x%lx\n", start_address, end_address);

    while(start_address < end_address)
    {
        tab = (pgentry_t *)start_info.pt_base;
        mfn = pfn_to_mfn(virt_to_pfn(start_info.pt_base));

#if defined(__x86_64__)
        offset = l4_table_offset(start_address);
        if(!(tab[offset] & _PAGE_PRESENT)) {
	        /* Need new L3 pt frame */
        	long npf_pfn = env_npf->pfn_alloc(env_npf, start_address);
        	if (npf_pfn < 0) {
        		return 0;
        	} else {
        		new_pt_frame(npf_pfn, mfn, offset, L3_FRAME);
        	}
        }

        page = tab[offset];
        mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(mfn) << PAGE_SHIFT);
#endif
#if defined(__x86_64__) || defined(CONFIG_X86_PAE)
        offset = l3_table_offset(start_address);
        if(!(tab[offset] & _PAGE_PRESENT)) {
            /* Need new L2 pt frame */
        	long npf_pfn = env_npf->pfn_alloc(env_npf, start_address);
        	if (npf_pfn < 0) {
        		return 0;
        	} else {
        		new_pt_frame(npf_pfn, mfn, offset, L2_FRAME);
        	}
        }

        page = tab[offset];
        mfn = pte_to_mfn(page);
        tab = to_virt(mfn_to_pfn(mfn) << PAGE_SHIFT);
#endif
        offset = l2_table_offset(start_address);

	if (page_size == PAGE_SIZE) {
	  if(!(tab[offset] & _PAGE_PRESENT)) {
	      /* Need new L1 pt frame */
      	  long npf_pfn = env_npf->pfn_alloc(env_npf, start_address);
      	  if (npf_pfn < 0) {
      		  return 0;
      	  } else {
      		  new_pt_frame(npf_pfn, mfn, offset, L1_FRAME);
      	  }
	  }

	  page = tab[offset];
	  mfn = pte_to_mfn(page);
	  offset = l1_table_offset(start_address);
	}

	long mfn_for_pfn = env_pfn->pfn_alloc(env_pfn, start_address);
	if (mfn_for_pfn < 0) {
		return 0;
	}

	if (mfn_for_pfn > 0) {
	  mmu_updates[count].ptr = ((pgentry_t)mfn << PAGE_SHIFT) + sizeof(pgentry_t) * offset;
	  if (page_size == PAGE_SIZE) {
	    mmu_updates[count].val = (pgentry_t)mfn_for_pfn << L1_PAGETABLE_SHIFT | L1_PROT;
	  } else {
	    mmu_updates[count].val = (pgentry_t)mfn_for_pfn << L2_PAGETABLE_SHIFT | L1_PROT | _PAGE_PSE;
	  }
	  count++;
	}
        start_address += page_size;
        if (count == L1_PAGETABLE_ENTRIES || start_address == end_address)
        {
            if(HYPERVISOR_mmu_update(mmu_updates, count, NULL, DOMID_SELF) < 0)
            {
                xprintk("PTE could not be updated\n");
                crash_exit();
            }
            count = 0;
        }
    }

    if (trace_mmpt()) ttprintk("MM: page tables setup\n");
    return 1;
}

int guk_build_pagetable(unsigned long start_address, unsigned long end_address,
		     pfn_alloc_env_t *env_pfn, pfn_alloc_env_t *env_npf) {
  return build_pagetable_vs(start_address, end_address, PAGE_SIZE, env_pfn, env_npf);
}

int guk_build_pagetable_2mb(unsigned long start_address, unsigned long end_address,
			 pfn_alloc_env_t *env_pfn, pfn_alloc_env_t *env_npf) {
  return build_pagetable_vs(start_address, end_address, PAGE_SIZE * 512, env_pfn, env_npf);
}

/* Clears the page table entries for the given address range and invalidates TLB.
 * We do not reclaim empty page table frames on the grounds that they likely
 * will be used again.
 */
static void demolish_pagetable_vs(unsigned long start_address, unsigned long end_address,
			int page_size) {
    int i = 0;
    multicall_entry_t call[1024];

    if (trace_mmpt()) ttprintk("MM: unmapping memory range 0x%lx - 0x%lx\n", start_address, end_address);

    while (start_address < end_address)
    {
        int arg = 0;
        call[i].op = __HYPERVISOR_update_va_mapping;
	call[i].args[arg++] = start_address;
	call[i].args[arg++] = 0;
	call[i].args[arg++] = UVMF_ALL | UVMF_INVLPG;
        start_address += page_size;

	if (i == 1023 || start_address == end_address) {
	  int ret = HYPERVISOR_multicall(call, i);
	  if (ret) {
	    crash_exit_msg("update_va_mapping hypercall failed");
	  }
	  i = 0;
	}
	i++;
    }

    if (trace_mmpt()) ttprintk("MM: page tables demolished\n");
}

void guk_demolish_pagetable(unsigned long start_address, unsigned long end_address) {
  demolish_pagetable_vs(start_address, end_address, PAGE_SIZE);
}

void guk_demolish_pagetable_2mb(unsigned long start_address, unsigned long end_address) {
  demolish_pagetable_vs(start_address, end_address, PAGE_SIZE * 512);
}

static void write_protect_vs(unsigned long start_address, unsigned long end_address, int page_size) {
    int i = 0;
    multicall_entry_t call[1024];

    if (trace_mmpt()) ttprintk("MM: write protecting memory range 0x%lx - 0x%lx\n", start_address, end_address);

    while (start_address < end_address)
    {
        int arg = 0;
	unsigned long pte;
	guk_not11_virt_to_pfn(start_address, &pte);
        call[i].op = __HYPERVISOR_update_va_mapping;
	call[i].args[arg++] = start_address;
	call[i].args[arg++] = pte & ~_PAGE_RW;
	call[i].args[arg++] = 0;
        start_address += page_size;

	if (i == 1023 || start_address == end_address) {
	  int ret = HYPERVISOR_multicall(call, i);
	  if (ret) {
	    crash_exit_msg("update_va_mapping hypercall failed");
	  }
	  i = 0;
	}
	i++;
    }

    if (trace_mmpt()) ttprintk("MM: page tables demolished\n");

}

void guk_write_protect(unsigned long start_address, unsigned long end_address) {
  write_protect_vs(start_address, end_address, PAGE_SIZE);
}

void guk_write_protect_2mb(unsigned long start_address, unsigned long end_address) {
  write_protect_vs(start_address, end_address, PAGE_SIZE * 512);
}

void arch_init_mm(unsigned long* free_pfn_ptr, unsigned long* max_pfn_ptr)
{
    unsigned long start_pfn;
    if (trace_mmpt()) {
        tprintk("  _text:        %p\n", &_text);
        tprintk("  _etext:       %p\n", &_etext);
        tprintk("  _edata:       %p\n", &_edata);
        tprintk("  stack start:  %p %lx (%ld)\n", &stack, virt_to_pfn(&stack),
		                                  virt_to_pfn(&stack));
        tprintk("  _end:         %p\n", &_end);
	tprintk("  pt_base:      %p %lx (%ld)\n", start_info.pt_base,
		                                  virt_to_pfn(start_info.pt_base),
		                                  virt_to_pfn(start_info.pt_base));
	tprintk("  nr_pt_frames: %ld\n", start_info.nr_pt_frames);
	tprintk("  nr_pages    : %ld\n", start_info.nr_pages);
	tprintk("  mfn_list    : %p\n", start_info.mfn_list);
	tprintk("  store_addr  : %p %lx (%ld)\n", mfn_to_virt(start_info.store_mfn),
		                                  mfn_to_pfn(start_info.store_mfn),
		                                  mfn_to_pfn(start_info.store_mfn));
	tprintk("  pfn_to_mfn  : %p %lx (%ld)\n", phys_to_machine_mapping,
		                                  virt_to_pfn(phys_to_machine_mapping),
		                                  virt_to_pfn(phys_to_machine_mapping));
	tprintk("  mfn_to_pfn  : %p\n", machine_to_phys_mapping);
	tprintk("  hyp_start   : %p\n", HYPERVISOR_VIRT_START);
	tprintk("  hyp_end     : %p\n", HYPERVISOR_VIRT_END);
    }

    /* First page follows page table pages and 3 more pages (store page etc) */
    *free_pfn_ptr = PFN_UP(to_phys(start_info.pt_base)) +
                start_info.nr_pt_frames + 3; /* are the 3 pages necessary? why? */
    *max_pfn_ptr = start_info.nr_pages;

    if (trace_mmpt()) {
        tprintk("  free_pfn:     %lx (%ld)\n", *free_pfn_ptr, *free_pfn_ptr);
        tprintk("  max_pfn:      %lx (%ld)\n", *max_pfn_ptr, *max_pfn_ptr);
    }

    /* start_address is the virtual address of the page after the page frames
     * allocated by the domain builder. We assume that addresses from 0 to start_address-1
     * have been mapped by the domain builder. Note that, although the initial page frames
     * themselves are all mapped, the last frame is likely not full. The pfns between
     * *free_pfn_ptr and start_pfn are available, and used here for additional page frames
     * as needed.
     */
    start_pfn = (start_info.nr_pt_frames - NOT_L1_FRAMES) * L1_PAGETABLE_ENTRIES;

    struct pfn_alloc_env pfn_frame_alloc_env = {
        .pfn_alloc = pfn_linear_alloc
    };

    struct pfn_alloc_env pfn_linear_tomap_env = {
        .pfn_alloc = pfn_linear_alloc
    };

    pfn_frame_alloc_env.pfn = *free_pfn_ptr;
    pfn_linear_tomap_env.pfn = start_pfn;

    build_pagetable(pfn_to_virtu(start_pfn), pfn_to_virtu(*max_pfn_ptr),
		    &pfn_linear_tomap_env, &pfn_frame_alloc_env);
    *free_pfn_ptr = pfn_frame_alloc_env.pfn;
    if (trace_mmpt())
        tprintk("MM: next_pfn %x (%d)\n", *free_pfn_ptr, *free_pfn_ptr);
}

int guk_unmap_page_pfn(unsigned long addr, unsigned long pfn) {
    pte_t val = __pte(pfn_to_mfn(pfn) << PAGE_SHIFT);
    return HYPERVISOR_update_va_mapping(PAGE_ALIGN(addr),
	    val,
	    (unsigned long)UVMF_ALL | UVMF_INVLPG);
}

int guk_unmap_page(unsigned long addr)
{
    return guk_unmap_page_pfn(addr, virt_to_pfn(addr));
}

int guk_clear_pte(unsigned long addr) {
    return HYPERVISOR_update_va_mapping(PAGE_ALIGN(addr),
	    __pte(0),
	    (unsigned long)UVMF_ALL | UVMF_INVLPG);

}

int guk_remap_page_pfn(unsigned long addr, unsigned long pfn) {
    pte_t val = __pte( (pfn_to_mfn(pfn) << PAGE_SHIFT) | L1_PROT );
    return HYPERVISOR_update_va_mapping(PAGE_ALIGN(addr),
	    val,
	    (unsigned long)UVMF_ALL | UVMF_INVLPG);
}

int guk_remap_page(unsigned long addr)
{
  return guk_remap_page_pfn(addr, virt_to_pfn(addr));
}

long guk_not11_virt_to_pfn(unsigned long addr, unsigned long *pte_ptr) {
  pgentry_t *tab = (pgentry_t *)start_info.pt_base;
  long pfn;
  int level = 4;
  int offset;
  unsigned long pte;
  while (level > 0) {
    switch (level) {
    case 4: offset = l4_table_offset(addr); break;
    case 3: offset = l3_table_offset(addr); break;
    case 2: offset = l2_table_offset(addr); break;
    case 1: offset = l1_table_offset(addr); break;
    }
    pte = tab[offset];
    if (level > 1 && !(pte & _PAGE_PRESENT)) {
    	xprintk("guk_not11_virt_to_pfn NOT PRESENT addr %lx, level %d, offset %d, pte %lx\n", addr, level, offset, pte);
    	return -1;
    }
    pfn = mfn_to_pfn(pte_to_mfn(pte));
    tab = to_virt(pfn << PAGE_SHIFT);
    level--;
  }
  *pte_ptr = pte;
  return pfn;
}

int guk_validate(unsigned long addr) {
  unsigned long pte;
  long r = guk_not11_virt_to_pfn(addr, &pte);
  return r > 0 && (pte & _PAGE_PRESENT) ? 1 : 0;
}

unsigned long guk_pfn_to_mfn(unsigned long pfn) {
  return pfn_to_mfn(pfn);
}

unsigned long guk_mfn_to_pfn(unsigned long mfn) {
  return mfn_to_pfn(mfn);
}


