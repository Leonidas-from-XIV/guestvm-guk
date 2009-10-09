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
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: xmalloc.c
 *      Author: Grzegorz Milos (gm281@cam.ac.uk)
 *     Changes: Mick Jordan, Sun Microsystems Inc.
 *
 *        Date: Aug 2005
 *
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: simple memory allocator
 *
 ****************************************************************************
 * Simple allocator for Mini-os.  If larger than a page, simply use the
 * page-order allocator.
 *
 * Copy of the allocator for Xen by Rusty Russell:
 * Copyright (C) 2005 Rusty Russell IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <guk/os.h>
#include <guk/mm.h>
#include <guk/sched.h>
#include <guk/trace.h>
#include <guk/spinlock.h>

#include <types.h>
#include <lib.h>
#include <list.h>

static LIST_HEAD(freelist);
static spinlock_t freelist_lock = SPIN_LOCK_UNLOCKED;

struct xmalloc_hdr
{
    /* Total including this hdr. */
    size_t size;
    struct list_head freelist;
#if defined(__ia64__)
		// Needed for ia64 as long as the align parameter in _xmalloc()
		// is not supported.
    uint64_t pad;
#endif

} __cacheline_aligned;

static void maybe_split(struct xmalloc_hdr *hdr, size_t size, size_t block)
{
    struct xmalloc_hdr *extra;
    size_t leftover = block - size;

    /* If enough is left to make a block, put it on free list. */
    if ( leftover >= (2 * sizeof(struct xmalloc_hdr)) )
    {
        extra = (struct xmalloc_hdr *)((unsigned long)hdr + size);
        extra->size = leftover;
        list_add(&extra->freelist, &freelist);
    }
    else
    {
        size = block;
    }

    hdr->size = size;
    /* Debugging aid. */
    hdr->freelist.next = hdr->freelist.prev = NULL;
}

static void *xmalloc_new_page(size_t size)
{
    struct xmalloc_hdr *hdr;
    unsigned long flags;

    hdr = (struct xmalloc_hdr *)alloc_page();
    if ( hdr == NULL )
        return NULL;

    spin_lock_irqsave(&freelist_lock, flags);
    maybe_split(hdr, size, PAGE_SIZE);
    spin_unlock_irqrestore(&freelist_lock, flags);
    return hdr+1;
}

/* Return size, increased to alignment with align. */
static inline size_t align_up(size_t size, size_t align)
{
    return (size + align - 1) & ~(align - 1);
}

/* Big object?  Just use the page allocator. */
static void *xmalloc_whole_pages(size_t size)
{
    struct xmalloc_hdr *hdr;
    size_t asize = align_up(size, PAGE_SIZE);

    hdr = (struct xmalloc_hdr *)guk_allocate_pages(asize / PAGE_SIZE, DATA_VM);
        if ( hdr == NULL )
        return NULL;

    hdr->size = asize;
    /* Debugging aid. */
    hdr->freelist.next = hdr->freelist.prev = NULL;
    return hdr+1;
}

void *guk_xmalloc(size_t asize, size_t align)
{
    struct xmalloc_hdr *i;
    unsigned long flags;
    size_t size = asize;
    void *result;

    BUG_ON(in_irq());

    /* Add room for header, pad to align next header. */
    size += sizeof(struct xmalloc_hdr);
    size = align_up(size, __alignof__(struct xmalloc_hdr));

    if (trace_mm()) {
      ttprintk("AME %d %d\n", asize, size);
    }

    /* For big allocs, give them whole pages. */
    if ( size >= PAGE_SIZE ) {
        result = xmalloc_whole_pages(size);
        goto done;
    }

    /* Search free list. */
    spin_lock_irqsave(&freelist_lock, flags);
    list_for_each_entry( i, &freelist, freelist )
    {
        if ( i->size < size )
            continue;
        list_del(&i->freelist);
        maybe_split(i, size, i->size);
        spin_unlock_irqrestore(&freelist_lock, flags);
	//if (i->size > size) printk("xmalloc overalloc %d, %d\n", i->size, size);
	//print_free_list("xmalloc exit 1");
 
        i = i + 1;
        result = (void *) i;
        goto done;
    }
    spin_unlock_irqrestore(&freelist_lock, flags);

    /* Alloc a new page and return from that. */
    result = xmalloc_new_page(size);
done:
    if (trace_mm()) {
        ttprintk("AMX %lx %d %d\n", result, asize, size);
    }
    return result; 
}

void guk_xfree(const void *p)
{
    unsigned long flags;
    struct xmalloc_hdr *i, *tmp, *hdr;

    BUG_ON(in_irq());

    if (trace_mm()) {
      ttprintk("FME %lx\n", p);
    }

    if ( p == NULL )
        return;

    //print_free_list("xfree entry");
    hdr = (struct xmalloc_hdr *)p - 1;
    //printk("xfree %lx\n", p);

    /* We know hdr will be on same page. */
    if(((long)p & PAGE_MASK) != ((long)hdr & PAGE_MASK))
    {
        printk("Header should be on the same page, p=%lx, hdr=%lx\n", p, hdr);
         *(int*)0=0;
    }

    /* Not previously freed. */
    if(hdr->freelist.next || hdr->freelist.prev)
    {
        printk("Should not be previously freed\n");
        *(int*)0=0;
    }

    /* Big allocs free directly. */
    if ( hdr->size >= PAGE_SIZE )
    {
        guk_deallocate_pages(hdr, hdr->size / PAGE_SIZE, DATA_VM);
        //print_free_list("xfree return whole");
        goto done;
    }

    /* Merge with other free block, or put in list. */
    spin_lock_irqsave(&freelist_lock, flags);
    list_for_each_entry_safe( i, tmp, &freelist, freelist )
    {
        unsigned long _i   = (unsigned long)i;
        unsigned long _hdr = (unsigned long)hdr;

        /* Do not merge across page boundaries. */
        if ( ((_i ^ _hdr) & PAGE_MASK) != 0 )
            continue;

        /* We follow this block?  Swallow it. */
        if ( (_i + i->size) == _hdr )
        {
            list_del(&i->freelist);
            i->size += hdr->size;
            hdr = i;
        }

        /* We precede this block? Swallow it. */
        if ( (_hdr + hdr->size) == _i )
        {
            list_del(&i->freelist);
            hdr->size += i->size;
        }
    }

    /* Did we merge an entire page? */
    if ( hdr->size == PAGE_SIZE )
    {
        if((((unsigned long)hdr) & (PAGE_SIZE-1)) != 0)
        {
            printk("Bug\n");
            *(int*)0=0;
        }
        free_pages(hdr, 0);
    }
    else
    {
        list_add(&hdr->freelist, &freelist);
    }

    spin_unlock_irqrestore(&freelist_lock, flags);
done:
    if (trace_mm()) {
      ttprintk("FMX %lx\n", p);
    }
    //print_free_list("xfree return");
}

/*
 * Fairly simplistic implementation that just frees and allocs new size
 * unless existing block can accomodate request
 */
void *guk_xrealloc(const void *p, size_t size, size_t align)
{
    struct xmalloc_hdr *hdr;
    int psize;

    BUG_ON(in_irq());
    if ( p == NULL )
        return guk_xmalloc(size, align);

    hdr = (struct xmalloc_hdr *)p - 1;
    psize = hdr->size - sizeof(struct xmalloc_hdr);
    // check if existing block can accomodate request
    if (psize >= size) return (void *)p;

    void *result = guk_xmalloc(size, align);

    // copy old data
    memcpy(result, p, psize);

    guk_xfree(p);

    return result;

}
