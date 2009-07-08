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
 *     Changes: Grzegorz Milos,Sun Microsystems, Inc., summer intern 2007.
 *            : Mick Jordan, Sun Microsystems, Inc.
 *
 *        Date: Aug 2003, changes Aug 2005, 2008, 2009
 *
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: page allocator
 *              Version for GuestVM that can be fairly simple because most interesting
 *              allocation smarts are at the Java level. We just retain the
 *              page bitmap from the original buddy allocator.
 *              But then we complicated it to support memory ballooning!
 *
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
#include <spinlock.h>
#include <lib.h>
#include <bitmap.h>
//#define MM_DEBUG

/*
 * The functions here should now really be considered as allocating "physical"
 * memory and not combined virtual and physical memory as in the original MiniOS.
 * Most Java allocations, i.e., heap, thread stacks and runtime code regions
 * are allocated in specific areas of virtual memory that are mapped
 * to the physical memory allocated from here. However, since physical memory
 * and virtual memory are still also mapped 1-1, it is possible to
 * view this as also allocating virtual memory. That fact is used by the 
 * microkernel and also by Java when allocating miscellaneous data.
 *
 * There is one downside to this arrangement, which is that physical
 * memory is still allocated contiguously in all cases, even though it
 * does not need to be for the heap, thread stacks and runtime code regions.
 * We would need rather different APIs that could represent non-contiguous
 * results to address that.
 *
 * The code supports "ballooning" through the increase/decrease_memory
 * functions. Holes may be anywhere in the physical address space and
 * are recorded in memory_hole_list. If maxmem is larger than initial domain
 * memory, then memory_hole_list starts off with one entry denoting that.
 * Holes appear allocated in the allocation bitmap.
 *
 */


static DEFINE_SPINLOCK(bitmap_lock);

/*
 * ALLOCATION BITMAP
 * One bit per page of memory. Bit set => page is allocated (or a hole).
 * The bitmap is sized (currently) to be big enough for the max
 * domain memory, not initial, since this simplifies expansion.
 * Each page of the bit map covers 32k 4k pages, i.e. 128MB.
 *
 * The allocation area is split into two regions, small and large.
 * This is since Java heap allocations tend to be large and
 * we would prefer not to pollute this space with small allocations,
 * to avoid unnecessary fragmentation.
 *
 * The small region is sized in proportion to the maximum memory
 * and if we run out we fail over the laarge region. If we fail to allocate 
 * in the large region will try to get more memory from Xen and retry.
 */

static unsigned long *alloc_bitmap;
#define PAGES_PER_MAPWORD ENTRIES_PER_MAPWORD
#define BITS_PER_PAGE (4096 * 8)
#define DEFAULT_SMALL_PERCENTAGE 2 /* percentage reserved for small allocations */
#define SMALL_PERCENTAGE_OPTION "-XX:GUKMS"

/* these values are constant after initialization, unless memory is added or taken away */
static unsigned long first_alloc_page;  /* first allocatable page */
static unsigned long end_alloc_page;    /* last allocatable page+1 */
static unsigned long max_end_alloc_page;/* absolutely last allocatable page+1 */

/* these values fluctuate as memory is allocated */
static unsigned long first_free_page;   /* first free allocatable page */
static unsigned long first_bulk_page;   /* start of bulk allocation region */
static unsigned long first_free_bulk_page;   /* first free bulk allocatable page*/
static unsigned long num_free_pages;
static unsigned long num_free_bulk_pages;

unsigned long *phys_to_machine_mapping;

/* Structure and list to record holes in our physical address space for ballooning */
static LIST_HEAD(memory_hole_list);
typedef struct memory_hole {
  struct list_head memory_hole_next;
  unsigned long start_pfn;
  unsigned long end_pfn;
} memory_hole_t;

#define memory_hole_head (list_entry(memory_hole_list.next, memory_hole_t, memory_hole_next))


static int in_increase_memory = 0;      /* flag to detect recursive entry */

#define BULK_ALLOCATION 512             /* 512 pages, 2MB */
#define is_bulk(_n) (_n >= BULK_ALLOCATION)

/* forward refs */
static long increase_reservation(unsigned long n, memory_hole_t *hole);
static long decrease_reservation(memory_hole_t *hole);
static unsigned long can_increase(unsigned long n);

//#define MACHINE_ALLOC

#ifdef MACHINE_ALLOC
/*
 * Experimental support for allocating contiguous
 * physical memory to support 2MB pages, which should be the default for 
 * heap and code. Xen doesn't currently support this however.
 */

/* A bit set in this map means that this domain owns the machine page AND it has
   not been allocated.
*/
static unsigned long *machine_bitmap;
static unsigned long max_machine_page;

static void map_alloc(unsigned long first_page, unsigned long nr_pages);

void *guk_machine_page_pool_bitmap(void) {
  return machine_bitmap;
}

void machine_map_alloc(unsigned long page, int n) {
  unsigned long p;
  for (p = page; p < page + n; p++) {
    clear_map(machine_bitmap, pfn_to_mfn(p));
  }
}

void machine_map_free(unsigned long page, int n) {
  unsigned long p;
  for (p = page; p < page + n; p++) {
    set_map(machine_bitmap, pfn_to_mfn(p));
  }
}

int is_2mb_free(unsigned long index) {
  int i;
  for (i = 0; i < 8; i++) {
    if (machine_bitmap[index + i] != 0xFFFFFFFFFFFFFFFF) {
      return 0;
    }
  }
  return 1;
}

void map_alloc_2mb(unsigned long index) {
  int i;
  for (i = 0; i < 8; i++) {
    machine_bitmap[index + i] = 0;
    int j;
    for (j = 0; j < PAGES_PER_MAPWORD; j++) {
      unsigned long page = (index + i) * PAGES_PER_MAPWORD + j;
      map_alloc(mfn_to_pfn(page), 1);
    }
  }
}

unsigned long guk_allocate_2mb_machine_pages(int n, int type) {
  unsigned long result = 0;
  unsigned long page = 0;
  spin_lock(&bitmap_lock);
  while (page < max_machine_page) {
    unsigned long page_index = page / PAGES_PER_MAPWORD;
    //printk("page %d, b[%d] %lx\n", page, page_index, machine_bitmap[page_index]);
    if (machine_bitmap[page_index] == 0xFFFFFFFFFFFFFFFF) {
      /* need n*8 contiguous words all unallocated in the map */
      int nn = n;
      unsigned long npage = page;
      while (nn > 1 && npage < max_machine_page) {
	if (is_2mb_free(page_index)) {
	  nn--; page_index += 8; npage += 512;
	} else {
	  page = npage;
	  break;
	}
      }
      if (nn == 1) {
	result = page;
	map_alloc_2mb(page / PAGES_PER_MAPWORD);
	num_free_pages -= (n * 512);
	num_free_bulk_pages -= (n * 512);
	break;
      }
    }
    page += 512;
  }
  spin_unlock(&bitmap_lock);
  return result;
}

static void update_machine_bitmap(long pfn_start, long pfn_end) {
  int p;
  for (p = pfn_start; p < pfn_end; p++) {
    long mfn = pfn_to_mfn(p);
    set_map(machine_bitmap, mfn);
  }
}

static void analyze_machine_memory(void) {
  max_machine_page = maximum_ram_page();
  machine_bitmap = (unsigned long *)allocate_pages(max_machine_page / BITS_PER_PAGE, DATA_VM);
  update_machine_bitmap(first_alloc_page, end_alloc_page);
}

#else
unsigned long guk_allocate_2mb_machine_pages(int n, int type) {
  return 0;
}

void *guk_machine_page_pool_bitmap(void) {
  return NULL;
}

#endif /*MACHINE_ALLOC*/


long guk_page_pool_start(void) {
  return first_alloc_page;
}

long guk_page_pool_end(void) {
  return end_alloc_page - 1;
}

void *guk_page_pool_bitmap(void) {
  return alloc_bitmap;
}

/*
 * Hint regarding bitwise arithmetic in map_{alloc,free}:
 *  -(1<<n)  sets all bits >= n.
 *  (1<<n)-1 sets all bits <  n.
 * Variable names in map_{alloc,free}:
 *  *_idx == Index into `alloc_bitmap' array.
 *  *_off == Bit offset within an element of the `alloc_bitmap' array.
 */

/*
 * Mark pages from first_page to first_page + nr_pages, inclusive, as allocated.
 * first_page is a physical page number
 */
static void map_alloc(unsigned long first_page, unsigned long nr_pages)
{
    unsigned long start_off, end_off, curr_idx, end_idx;

#ifdef MM_DEBUG
    unsigned long curr_page;

    for(curr_page=first_page;
        curr_page < first_page + nr_pages;
        curr_page++)
        BUG_ON(allocated_in_map(alloc_bitmap, curr_page));
#endif
    curr_idx  = first_page / PAGES_PER_MAPWORD;
    start_off = first_page & (PAGES_PER_MAPWORD-1);
    end_idx   = (first_page + nr_pages) / PAGES_PER_MAPWORD;
    end_off   = (first_page + nr_pages) & (PAGES_PER_MAPWORD-1);

    if ( curr_idx == end_idx )
    {
        alloc_bitmap[curr_idx] |= ((1UL<<end_off)-1) & -(1UL<<start_off);
    }
    else
    {
        alloc_bitmap[curr_idx] |= -(1UL<<start_off);
        while ( ++curr_idx < end_idx ) alloc_bitmap[curr_idx] = ~0L;
        alloc_bitmap[curr_idx] |= (1UL<<end_off)-1;
    }
#ifdef MM_DEBUG
    for(curr_page=first_page;
        curr_page < first_page + nr_pages;
        curr_page++)
        BUG_ON(!allocated_in_map(alloc_bitmap, curr_page));
#endif
}


/*
 * Mark pages from first_page to first_page + nr_pages, inclusive, as free.
 * first_page is a physical page number
 */
static void map_free(unsigned long first_page, unsigned long nr_pages)
{
    unsigned long start_off, end_off, curr_idx, end_idx;

#ifdef MM_DEBUG
    unsigned long curr_page;

    for(curr_page=first_page;
        curr_page < first_page + nr_pages;
        curr_page++)
        BUG_ON(!allocated_in_map(alloc_bitmap, curr_page));
#endif

    curr_idx = first_page / PAGES_PER_MAPWORD;
    start_off = first_page & (PAGES_PER_MAPWORD-1);
    end_idx   = (first_page + nr_pages) / PAGES_PER_MAPWORD;
    end_off   = (first_page + nr_pages) & (PAGES_PER_MAPWORD-1);

    if ( curr_idx == end_idx )
    {
        alloc_bitmap[curr_idx] &= -(1UL<<end_off) | ((1UL<<start_off)-1);
    }
    else
    {
        alloc_bitmap[curr_idx] &= (1UL<<start_off)-1;
        while ( ++curr_idx != end_idx ) alloc_bitmap[curr_idx] = 0;
        alloc_bitmap[curr_idx] &= -(1UL<<end_off);
    }

#ifdef MM_DEBUG
    for(curr_page=first_page;
        curr_page < first_page + nr_pages;
        curr_page++)
        BUG_ON(allocated_in_map(alloc_bitmap, curr_page));
#endif
}

/* Returns the next free page after "page" */
static int next_free(int page) {
  while (page < end_alloc_page) {
    if (!allocated_in_map(alloc_bitmap, page)) {
      break;
    }
    page++;
  }
  return page;
}

static void static_dump_page_pool_state(printk_function_ptr printk_function) {
  unsigned long p = first_alloc_page;
  unsigned long free = 0;
  unsigned long unavailable = 0;
  struct list_head *list;
  memory_hole_t *memory_hole;
  (*printk_function)("Page allocation state:\n");
  (*printk_function)("  end_alloc_page %d, max_end_alloc_page %d\n",
		     end_alloc_page, max_end_alloc_page);
  (*printk_function)("  first_free_page %d,  num_free_small_pages %d, \n",
		     first_free_page, num_free_pages - num_free_bulk_pages);
  (*printk_function)("  first_free_bulk_page %d, num_free_bulk_pages %d, first_bulk_page %d\n",
		     first_free_bulk_page, num_free_bulk_pages, first_bulk_page);
  while (p < end_alloc_page) {
    unsigned long q = p;
    if (allocated_in_map(alloc_bitmap, p)) {
      while (p < end_alloc_page && allocated_in_map(alloc_bitmap, p)) { p++; }
      (*printk_function)("%d .. %d allocated\n", q, p - 1);
    } else {
      while (p < end_alloc_page && !allocated_in_map(alloc_bitmap, p)) { p++; }
      (*printk_function)("%d .. %d free\n", q, p - 1);
      free += (p - q);
    }
  }
  list_for_each(list, &memory_hole_list) {
    memory_hole = list_entry(list, memory_hole_t, memory_hole_next);    
    (*printk_function)("%d .. %d unavailable\n", memory_hole->start_pfn, memory_hole->end_pfn - 1);
    unavailable += (memory_hole->end_pfn - memory_hole->start_pfn);
  }
  (*printk_function)("num_free_pages in map %d\n", free);
  (*printk_function)("total unavailable %d\n", unavailable);
}

void guk_dump_page_pool_state(void) {
  static_dump_page_pool_state(printk);
}

/* Try to allocate more memory from Xen
 * n: amount originally requested
 * This is called with the bitmap spinlock held
 */
static long increase_memory_holding_lock(unsigned long n) {
  long result = 0;
  if (in_increase_memory) {
    static_dump_page_pool_state(xprintk);
    crash_exit_msg("recursive entry to increase_memory");
  } else {
    in_increase_memory = 1;
  }
  if (can_increase(n)) {
    while (n > 0) {
      memory_hole_t *memory_hole = memory_hole_head;
      unsigned long hole_size = memory_hole->end_pfn - memory_hole->start_pfn;
      unsigned long nn = hole_size <= n ? hole_size : n;
      spin_unlock(&bitmap_lock); /* there may be recursive entry for page table frames */
      long rc = increase_reservation(nn, memory_hole);
      spin_lock(&bitmap_lock);
      if (rc > 0) {
#ifdef MACHINE_ALLOC
	update_machine_bitmap(memory_hole->start_pfn, memory_hole->start_pfn + rc);
#endif
	map_free(memory_hole->start_pfn, rc);
	if (memory_hole->start_pfn + rc > end_alloc_page) {
	  end_alloc_page = memory_hole->start_pfn + rc;
	}
	num_free_pages += rc;
	num_free_bulk_pages += rc;

	if (hole_size == rc) {
	  list_del(&memory_hole->memory_hole_next);
	} else {
	  memory_hole->start_pfn += rc;
	}
      } else {
	break; /* failed */
      }
      n -= rc;
      result += rc;
    }
  }
  if (trace_mm()) {
    ttprintk("APM %d %d\n", n, result);
  }
  in_increase_memory = 0;
  return result;
}

/* If possible, increase the page pool by extending the memory reservation */
long guk_increase_page_pool(unsigned long n) {
  long rc;
  spin_lock(&bitmap_lock);
  rc = increase_memory_holding_lock(n);
  spin_unlock(&bitmap_lock);
  return rc;
}

/* Returns the total amount of (bulk) memory that could be released from the page pool */
unsigned long guk_decreaseable_page_pool(void) {
    spin_lock(&bitmap_lock);
    int page = end_alloc_page - 1;
    int n = 0;
    while (page >= first_bulk_page) {
      if (!allocated_in_map(alloc_bitmap, page)) {
	while (!allocated_in_map(alloc_bitmap, page) && (page >= first_bulk_page)) {
	  page--; n++;
	}
      }
      page--;
    }
    spin_unlock(&bitmap_lock);
    //xprintk("decreaseable_memory %d\n", n);
    return n;
}

/* A free(p) may callback to deallocate_pages so must release the lock */
void unlocked_free(void *p) {
    spin_unlock(&bitmap_lock); 
    free(p);
    spin_lock(&bitmap_lock);    
}

/* add to memory_hole_list keeping it ordered by start_pfn */
void memory_hole_add(memory_hole_t *new_memory_hole) {
    struct list_head *list;
    memory_hole_t *memory_hole;
    list_for_each(list, &memory_hole_list) {
      memory_hole = list_entry(list, memory_hole_t, memory_hole_next);
      if (memory_hole->start_pfn > new_memory_hole->start_pfn) break;
    }
    list_add_tail(&new_memory_hole->memory_hole_next, list);
}
  
/* If there are n pages free somewhere in the bulk section of the page pool, decrease our
   reservation by that amount. */
long guk_decrease_page_pool(unsigned long n) {
    long rc = 0;
    //xprintk("decrease_memory %d\n", n);
    unsigned long page = end_alloc_page - 1;

    spin_lock(&bitmap_lock);
    while (page >= first_bulk_page && n > 0) {
      if (!allocated_in_map(alloc_bitmap, page)) {
	unsigned long npage = page;
	int nn = 0;
	npage = page + 1;
	while (!allocated_in_map(alloc_bitmap, page) && (page >= first_bulk_page) && (nn < n)) {
	  page--; nn++;
	}
	/* May get called back for small pages so unlock */
	spin_unlock(&bitmap_lock); 
	memory_hole_t *memory_hole = xmalloc(memory_hole_t);
	memory_hole->start_pfn = page + 1;
	memory_hole->end_pfn = npage;
	rc = decrease_reservation(memory_hole);
	spin_lock(&bitmap_lock);
	if (rc > 0) {
	  num_free_pages -= rc;
	  num_free_bulk_pages -= rc;
	  if (end_alloc_page == memory_hole->end_pfn) {
	    end_alloc_page -= rc;
	  }
	  map_alloc(memory_hole->start_pfn, rc);
	  /*xprintk("list empty %d, end %d, start %d\n",
		  list_empty(&memory_hole_list),
		  memory_hole->end_pfn,
		  memory_hole_head->start_pfn);*/
	  if (list_empty(&memory_hole_list) ||
	          memory_hole->end_pfn != memory_hole_head->start_pfn) {
	    memory_hole_add(memory_hole);
	  } else {
	    /* coalesce */
	    memory_hole_head->start_pfn = memory_hole->start_pfn;
	    unlocked_free(memory_hole);
	  }
	} else {
	  unlocked_free(memory_hole);
	}
	n -= nn;
      }
      page--;
    }
    spin_unlock(&bitmap_lock); 
    return rc;
}

/*
 * Allocate n contiguous pages. Returns a VIRTUAL/PHYSICAL address.
 */
static unsigned long _allocate_pages(int n, int type) 
{
    int page;
    unsigned long result = 0;
    int is_bulk_alloc = is_bulk(n);

    BUG_ON(in_irq());
    if (trace_mm()) {
      ttprintk("APE %d %d\n", n, type);
    }
    spin_lock(&bitmap_lock);

    while (result == 0) {
      unsigned long end_page = is_bulk_alloc ? end_alloc_page : first_bulk_page;
      page = is_bulk_alloc ? first_free_bulk_page : first_free_page;
      while (page < end_page) {
        if (!allocated_in_map(alloc_bitmap, page)) {
	  int nn = n;
	  int npage = page + 1;
	  while (nn > 1 && (npage < end_page)) {
	    if (!allocated_in_map(alloc_bitmap, npage)) {
	      nn--; npage++;
	    } else {
	      page = npage;
	      break;
	    }
	  }
	  /* if nn==1, found n consecutive */
	  if (nn == 1) {
	    result = (unsigned long) to_virt(PFN_PHYS(page));
	    if (is_bulk_alloc) {
	      num_free_bulk_pages -= n;
	      if (page == first_free_bulk_page) {
		first_free_bulk_page = next_free(page + n);
	      }
	    } else {
	      if (page == first_free_page) {
		first_free_page = next_free(page + n);
	      }
	    }
	    num_free_pages -= n;
	    map_alloc(page, n);
#ifdef MACHINE_ALLOC
	    machine_map_alloc(page, n);
#endif
	    break;
	  }
        }
        page++;
      }
  
      if (result > 0) break;
      else {
	if (!is_bulk_alloc) {
	  /* Out of small pages, try the bulk area */
	  is_bulk_alloc = 1;
	} else {
	  /* Out of bulk pages, try to increase */
	  if (!increase_memory_holding_lock(n)) break;
	}
      }
    }

    spin_unlock(&bitmap_lock);
    if (trace_mm()) {
      ttprintk("APX %lx %d %d %d\n", result, n, first_free_page, first_free_bulk_page);
    }
    if (result == 0 && !is_bulk_alloc) {
      crash_exit_msg("failed to allocate small pages");
    }
    return result;
}

/* Alloc 2^order pages, first fit. Returns a VIRTUAL address. */
unsigned long alloc_pages(int order) {
    return _allocate_pages(1 << order, DATA_VM);
}

/* Allocate n contiguous pages. Returns a VIRTUAL address.
*/
unsigned long guk_allocate_pages(int n, int type) {
  return _allocate_pages(n, type);
}

unsigned long guk_extend_allocate_pages(void *pointer, int n, int type) {
  return 0;
}

void guk_deallocate_pages(void *pointer, int n, int type) {
    BUG_ON(in_irq());
    if (trace_mm()) {
      ttprintk("FPE %lx %d\n", pointer, n);
    }
    spin_lock(&bitmap_lock);
    unsigned long page = virt_to_pfn(pointer);
    if (is_bulk(n)) {
      if (page < first_free_bulk_page) first_free_bulk_page = page;
      num_free_bulk_pages += n;
    } else {
      if (page < first_free_page) first_free_page = page;
    }
    map_free(page, n);
#ifdef MACHINE_ALLOC
    machine_map_free(page, n);
#endif
    spin_unlock(&bitmap_lock);
    if (trace_mm()) {
      ttprintk("FPX %lx %d %d %d\n", pointer, n, first_free_page, first_free_bulk_page);
    }
    num_free_pages += n;
}

void guk_free_pages(void *pointer, int order)
{
    deallocate_pages(pointer, 1 << order, DATA_VM);
}

unsigned long guk_total_free_pages(void) {
  return num_free_pages;
}

unsigned long guk_bulk_free_pages(void) {
  return num_free_bulk_pages;
}

unsigned long xen_maximum_reservation(void) {
    domid_t domid = DOMID_SELF;
    return HYPERVISOR_memory_op(XENMEM_maximum_reservation, &domid);
}

unsigned long guk_maximum_reservation(void) {
  return max_end_alloc_page;
}

unsigned long guk_maximum_ram_page(void) {
  unsigned long result;
  return HYPERVISOR_memory_op(XENMEM_maximum_ram_page, &result);
  return result;
}

unsigned long guk_pagetable_base(void) {
  return start_info.pt_base;
}


/* If maximum_reservation is greater than current reservation, then resize the
 * the phys_to_machine_mapping_table now to avoid problems trying to find the space
 * later when we call increase_reservation.
 */
void resize_phys_to_machine_mapping_table(void) {
  if (max_end_alloc_page > end_alloc_page) {
    int new_phys_to_machine_mapping_size = round_pgup(max_end_alloc_page * sizeof(long)) / PAGE_SIZE;
    unsigned long *new_phys_to_machine_mapping =
      (unsigned long *) allocate_pages(new_phys_to_machine_mapping_size, DATA_VM);
    if (new_phys_to_machine_mapping == 0) {
      crash_exit_msg("can't resize phys_to_machine_mapping\n");
    }
    memcpy(new_phys_to_machine_mapping, phys_to_machine_mapping, end_alloc_page * sizeof(long));
    phys_to_machine_mapping = new_phys_to_machine_mapping;
  }

  memory_hole_t *memory_hole = xmalloc(memory_hole_t);
  memory_hole->start_pfn = end_alloc_page;
  memory_hole->end_pfn = max_end_alloc_page;
  list_add(&memory_hole->memory_hole_next, &memory_hole_list);
}

/* Increase our physical memory allocation by nr_pages using
   the given hole in our current physical address space.
 */
static long increase_reservation(unsigned long nr_pages, memory_hole_t *memory_hole) {
    struct xen_memory_reservation reservation = {
          .address_bits = 0,
	  .extent_order = 0,
	  .domid        = DOMID_SELF
    };
    int pfn;

    unsigned long start_pfn = memory_hole->start_pfn;
    //xprintk("increase_reservation %d using %d ", nr_pages, start_pfn);
    BUG_ON(memory_hole->end_pfn - memory_hole->start_pfn < nr_pages);
    /* fill in pfns so Xen can update machine_tophys for us */
    for (pfn = 0; pfn < nr_pages; pfn++) {
        phys_to_machine_mapping[start_pfn + pfn] = start_pfn + pfn;
    }
    set_xen_guest_handle(reservation.extent_start, &phys_to_machine_mapping[start_pfn]);
    reservation.nr_extents = nr_pages;
    long rc = HYPERVISOR_memory_op(XENMEM_populate_physmap, &reservation);
    /* rc is the number actually allocated, if we get less return whatever we got */
    if (rc > 0) {
	struct pfn_alloc_env pfn_frame_alloc_env = {
	  .pfn_alloc = pfn_alloc_alloc
	};

	struct pfn_alloc_env pfn_linear_tomap_env = {
	  .pfn_alloc = pfn_linear_alloc
	};

	pfn_linear_tomap_env.pfn = start_pfn;
	build_pagetable(pfn_to_virtu(start_pfn), pfn_to_virtu(start_pfn + rc),
			&pfn_linear_tomap_env, &pfn_frame_alloc_env);
	arch_update_p2m(start_pfn, start_pfn + rc, 1);
    }
    //xprintk(".. returned %d\n", rc);
    return rc;
}

/* Decrease our reservation using given memory_hole
 */
static long decrease_reservation(memory_hole_t *memory_hole) {
    struct xen_memory_reservation reservation = {
          .address_bits = 0,
	  .extent_order = 0,
	  .domid        = DOMID_SELF
    };
    unsigned long nr_pages = memory_hole->end_pfn - memory_hole->start_pfn;
    //xprintk("decrease_reservation %d..%d (%d) ", memory_hole->start_pfn, memory_hole->end_pfn, nr_pages);
    demolish_pagetable(pfn_to_virtu(memory_hole->start_pfn), pfn_to_virtu(memory_hole->end_pfn));
    set_xen_guest_handle(reservation.extent_start, &phys_to_machine_mapping[memory_hole->start_pfn]);
    reservation.nr_extents = nr_pages;
    long rc = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
    /* rc is the number actually freed */
    if (rc != nr_pages) {
      xprintk("decrease_reservation: failed to release, rc = %ld\n", rc);
      crash_exit();
    } else {
      /* null out phys_to_machine_mapping table entries */
      int pfn;
      for (pfn = 0; pfn < nr_pages; pfn++) {
        phys_to_machine_mapping[memory_hole->start_pfn + pfn] = -1;
      }
      arch_update_p2m(memory_hole->start_pfn, memory_hole->end_pfn, 0);
    }
    //xprintk(".. returned %d\n", rc);
    return rc;
}

/* Can the current physical memory be increased by n pages?
 * Returns n if so, 0 if not
 */
static unsigned long can_increase(unsigned long n) {
    struct list_head *list;
    memory_hole_t *memory_hole;
    list_for_each(list, &memory_hole_list) {
      memory_hole = list_entry(list, memory_hole_t, memory_hole_next);
      unsigned long hole = memory_hole->end_pfn - memory_hole->start_pfn;
      if (hole >= n) return n;
      n -= hole;
    }
    return 0;
}

/* Returns the size in pages of the current memory reservation.
 * I.e., maxmem less the holes. Therefore, initially it is
 * the bootstrap domain memory allocation.
 */
unsigned long guk_current_reservation(void) {
  /* sum the holes */
    unsigned long result = 0;
    struct list_head *list;
    memory_hole_t *memory_hole;
    list_for_each(list, &memory_hole_list) {
      memory_hole = list_entry(list, memory_hole_t, memory_hole_next);
      result += memory_hole->end_pfn - memory_hole->start_pfn;
    }
    return max_end_alloc_page - result;
}

extern int num_option(char *cmd_line, char *option);

/*
 * Initialise allocator, placing pfns [min,max] in free pool.
 */
static void init_page_allocator(char *cmd_line, unsigned long min, unsigned long max)
{
    unsigned long bitmap_pages;
    int small_pct;

    /* Allocate space for the allocation bitmap.
     * Since a domain's maximum reservation can change
     * we just allocate enough for all machine memory.
     */
    max_end_alloc_page = xen_maximum_reservation();
    bitmap_pages = max_end_alloc_page / BITS_PER_PAGE;
    alloc_bitmap = (unsigned long *)pfn_to_virt(min);
    min += bitmap_pages;
    first_alloc_page = min;
    end_alloc_page = max;
    num_free_pages = max - min;
    first_free_page = first_alloc_page;
    /* reserve SMALL_PERCENTAGE of maximum allocation for small pages */
    small_pct = num_option(cmd_line, SMALL_PERCENTAGE_OPTION);
    if (small_pct < 0) small_pct = DEFAULT_SMALL_PERCENTAGE;
    first_bulk_page = first_free_page + (max_end_alloc_page * small_pct) / 100;
    if (first_bulk_page > end_alloc_page) first_bulk_page = end_alloc_page;
    first_free_bulk_page = first_bulk_page;
    num_free_bulk_pages = max - first_bulk_page;
    if (trace_mm()) {
      tprintk("MM: allocation bitmap pages %d\n", bitmap_pages);
      ttprintk("API %d %d %d %d\n", first_alloc_page, first_bulk_page, end_alloc_page, max_end_alloc_page);
    }

    /* All allocated by default. */
    memset(alloc_bitmap, ~0, bitmap_pages * 4096);
    /* Now free up the memory we've been given to play with.
       N.B. The pages beyond the initial domain reservation up to maxmem
       will therefore appear allocated.
     */
    map_free(first_alloc_page, num_free_pages);
#ifdef MACHINE_ALLOC
    analyze_machine_memory();
#endif
    // static_dump_page_pool_state(xprintk);
}

void init_mm(char *cmd_line)
{

    unsigned long pt_pfn, max_pfn;

    if (trace_mm()) tprintk("MM: Init\n");

    arch_init_mm(&pt_pfn, &max_pfn);
    /*
     * now we can initialise the page allocator
     */
    if (trace_mm()) 
	tprintk("MM: initialise page allocator for %lx-%lx\n",
           pt_pfn, max_pfn);
    
    barrier();

    init_page_allocator(cmd_line, pt_pfn, max_pfn);
    
    resize_phys_to_machine_mapping_table();

    arch_init_p2m(max_pfn, max_end_alloc_page);

    if (trace_mm()) {
	tprintk("MM: done\n");
    }

}


