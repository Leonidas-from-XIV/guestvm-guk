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
 * (C) 2006 - Cambridge University
 ****************************************************************************
 *
 *        File: gnttab.c
 *      Author: Steven Smith (sos22@cam.ac.uk)
 *     Changes: Grzegorz Milos (gm281@cam.ac.uk)
 *     Changes: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 *     Changes: Mick Jordan , Sun Microsystems, Inc.
 *
 *        Date: July 2006
 *
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: Simple grant tables implementation. About as stupid as it's
 *  possible to be and still work.
 *
 ****************************************************************************
 */
#include <guk/os.h>
#include <guk/mm.h>
#include <guk/gnttab.h>
#include <guk/trace.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>

#include <spinlock.h>

#define NR_RESERVED_ENTRIES 8

/* NR_GRANT_FRAMES must be less than or equal to that configured in Xen */
#define NR_GRANT_FRAMES 4
#define NR_GRANT_ENTRIES (NR_GRANT_FRAMES * PAGE_SIZE / sizeof(grant_entry_t))

/* synchronize access to the free list to prevent races between
 * get_free_entry and put_free_entry 
 */
static DEFINE_SPINLOCK(gnttab_lock);
static grant_entry_t *gnttab_table;
static grant_ref_t gnttab_list[NR_GRANT_ENTRIES];

/* count the number of free grant refs */
static int num_free = 0;
/* keep track of suspend/resume */
static int gnttab_suspended = 0;

static void
put_free_entry(grant_ref_t ref)
{
    int flags;
    spin_lock_irqsave(&gnttab_lock, flags);
    gnttab_list[ref] = gnttab_list[0];
    gnttab_list[0]  = ref;
    num_free++;
    spin_unlock_irqrestore(&gnttab_lock, flags);

}

static grant_ref_t
get_free_entry(void)
{
    unsigned int ref; 
    int flags;
    spin_lock_irqsave(&gnttab_lock, flags);
    ref = gnttab_list[0];
    gnttab_list[0] = gnttab_list[ref];
    num_free--;
    spin_unlock_irqrestore(&gnttab_lock, flags);

    BUG_ON(num_free < 0); /* FIXME: we should be able to recover from this */
    return ref;
}

grant_ref_t
gnttab_grant_access(domid_t domid, unsigned long frame, int readonly)
{
    grant_ref_t ref;

    ref = get_free_entry();
    gnttab_table[ref].frame = frame;
    gnttab_table[ref].domid = domid;
    wmb();
    readonly *= GTF_readonly;
    gnttab_table[ref].flags = GTF_permit_access | readonly;

    return ref;
}

grant_ref_t
gnttab_grant_transfer(domid_t domid, unsigned long pfn)
{
    grant_ref_t ref;

    ref = get_free_entry();
    gnttab_table[ref].frame = pfn;
    gnttab_table[ref].domid = domid;
    wmb();
    gnttab_table[ref].flags = GTF_accept_transfer;

    return ref;
}

int
gnttab_end_access(grant_ref_t ref)
{
    u16 flags, nflags;

    nflags = gnttab_table[ref].flags;
    do {
        if ((flags = nflags) & (GTF_reading|GTF_writing)) {
            printk("WARNING: g.e. still in use!\n");
            return 0;
        }
    } while ((nflags = cmpxchg(&gnttab_table[ref].flags, flags, 0)) !=
            flags);

    put_free_entry(ref);
    return 1;
}

unsigned long
gnttab_end_transfer(grant_ref_t ref)
{
    unsigned long frame;
    u16 flags;

    while (!((flags = gnttab_table[ref].flags) & GTF_transfer_committed)) {
        if (cmpxchg(&gnttab_table[ref].flags, flags, 0) == flags) {
            put_free_entry(ref);
            return 0;
        }
    }

    /* If a transfer is in progress then wait until it is completed. */
    while (!(flags & GTF_transfer_completed)) {
        flags = gnttab_table[ref].flags;
    }

    /* Read the frame number /after/ reading completion status. */
    rmb();
    frame = gnttab_table[ref].frame;

    put_free_entry(ref);

    return frame;
}

grant_ref_t
gnttab_alloc_and_grant(void **map)
{
    unsigned long mfn;
    grant_ref_t gref;

    *map = (void *)alloc_page();
    mfn = virt_to_mfn(*map);
    gref = gnttab_grant_access(0, mfn, 0);
    return gref;
}

static const char *gnttabop_error_msgs[] = GNTTABOP_error_msgs;

const char *
gnttabop_error(int16_t status)
{
    status = -status;
    if (status < 0 || status >= ARRAY_SIZE(gnttabop_error_msgs))
	return "bad status";
    else
        return gnttabop_error_msgs[status];
}

void gnttab_suspend(void)
{
    int i;

    for (i=0; i < NR_GRANT_FRAMES; i++)
	HYPERVISOR_update_va_mapping((unsigned long)(((char *)gnttab_table) + PAGE_SIZE*i), 
		(pte_t){0x0<<PAGE_SHIFT}, UVMF_INVLPG);

    ++gnttab_suspended;
}

static int init_frames(unsigned long *frames, int num_frames)
{
    struct gnttab_setup_table setup;
    int i;
    int r;
    
    for (i = NR_RESERVED_ENTRIES; i < NR_GRANT_ENTRIES; i++)
        put_free_entry(i);

    setup.dom = DOMID_SELF;
    setup.nr_frames = num_frames;
    set_xen_guest_handle(setup.frame_list, frames);

    r = HYPERVISOR_grant_table_op(GNTTABOP_setup_table, &setup, 1);
    if (trace_gnttab()) {
      for (i = 0; i < num_frames; i++) {
	tprintk("GT: gnttab_frame[%d] = %x\n", i, frames[i]);
      }
    }
    return r;
}

void gnttab_resume(void)
{
    unsigned long frames[NR_GRANT_FRAMES];
    int i;

    if (trace_gnttab()) 
	tprintk("GT: initialising gnttab on resume\n");

    BUG_ON(gnttab_suspended != 1);


    init_frames(frames, NR_GRANT_FRAMES);

    for(i=0; i < NR_GRANT_FRAMES; ++i) {
	HYPERVISOR_update_va_mapping((unsigned long)(((char *)gnttab_table) + PAGE_SIZE*i), 
		(pte_t){(frames[i] << PAGE_SHIFT) | L1_PROT}, UVMF_INVLPG);

    }
    --gnttab_suspended;
}

unsigned long pfn_gntframe_alloc(pfn_alloc_env_t *env, unsigned long addr) {
  unsigned long *frames = (unsigned long *)env->data;
  return frames[env->pfn++];;
}

void init_gnttab(void)
{
    unsigned long frames[NR_GRANT_FRAMES];

    if (trace_gnttab()) tprintk("GT: initialising gnttab on startup\n");

    /* The following call populates frames with mfns (from Xen) for the shared grant table. */
    init_frames(frames, NR_GRANT_FRAMES);
    /* We map the grant table at the first virtual address after the maximum machine ram  */    
    gnttab_table = pfn_to_virt(maximum_ram_page());

    struct pfn_alloc_env pfn_pageframe_env = {
      .pfn_alloc = pfn_alloc_alloc
    };
    struct pfn_alloc_env pfn_gntframe_env = {
      .pfn_alloc = pfn_gntframe_alloc
    };
    pfn_gntframe_env.pfn = 0; /* index into frames */
    pfn_gntframe_env.data = (void*)frames;
    build_pagetable((unsigned long) gnttab_table,
		    (unsigned long) (gnttab_table) + NR_GRANT_FRAMES * PAGE_SIZE,
		    &pfn_gntframe_env, &pfn_pageframe_env);

    if (trace_gnttab())
	tprintk("GT: gnttab_table mapped at %p\n", gnttab_table);
}
