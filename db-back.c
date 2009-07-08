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
 * Support for domain control by remote debugger using front/back device driver.
 * See tools/db-front for front-end.
 *
 * Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 * Changes: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 *          Mick Jordan, Sun Microsystems, Inc.
 *
 */


#include <guk/os.h>
#include <guk/xenbus.h>
#include <guk/xmalloc.h>
#include <guk/gnttab.h>
#include <guk/events.h>
#include <guk/db.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/spinlock.h>
#include <guk/trace.h>

#include <x86/traps.h>

#include <dbif.h>

#define DB_DEBUG
#ifdef DB_DEBUG
#define DEBUG(_l, _f, _a...) \
    if (db_debug_level >= _l) printk("GUK(file=db-back.c, line=%d) " _f "\n", __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

#define RELAX_NS 500000

static int db_debug_level = 0;
static int db_in_use = 0;
static struct dbif_back_ring ring;
static grant_ref_t ring_gref;
static evtchn_port_t port;
static char *cmd_line;
/* page for communicating block data */
static grant_ref_t data_grant_ref;
static char *data_page;
/* list for watchpoints */
static LIST_HEAD(watchpoints_list);
static DEFINE_SPINLOCK(watchpoints_list_lock);
struct db_watchpoint {
  struct list_head list;
  unsigned long address;
  unsigned long page_address;
  unsigned long size;
  int kind;
  unsigned long pfn;
};

#define DB_EXIT_UNSET 0
#define DB_EXIT_SET 1
#define DB_EXIT_DONE 2
#define DB_EXIT_FIN 3
static int db_exit = DB_EXIT_UNSET;
static int debug_state = 0;

int guk_debugging(void) {
  return debug_state;
}

void guk_set_debugging(int state) {
  debug_state = state;
}

/*
 * The one ukernel thread the debugger needs to gather is the primordial maxine thread.
 */
static int is_app_thread(struct thread *thread) {
    return !is_ukernel(thread) || strcmp(thread->name, "maxine") == 0;
}

static struct dbif_response *get_response(void)
{
    RING_IDX rsp_idx;

    rsp_idx = ring.rsp_prod_pvt++;

    return RING_GET_RESPONSE(&ring, rsp_idx);
}

/* Following two methods are no longer used */
static void dispatch_read_u64(struct dbif_request *req)
{
    struct dbif_response *rsp;
    u64 *pointer;

    pointer = (u64 *)req->u.read_u64.address;
    DEBUG(2, "Read request for %p recieved.", pointer);
    rsp = get_response();
    rsp->id = req->id;
    rsp->ret_val = *pointer;
}

static void dispatch_write_u64(struct dbif_request *req)
{
    struct dbif_response *rsp;
    u64 *pointer, value;

    pointer = (u64 *)req->u.writebytes.address;
    value = req->u.write_u64.value;
    DEBUG(2, "Write request for %p recieved, value=%lx, current_value=%lx.",
            pointer, value, *pointer);
    *pointer = value;
    rsp = get_response();
    rsp->id = req->id;
    rsp->ret_val = 0;
}

/* Support for handling bad requests gracefully.
 * Before issuing a read/write, db_back_access is set to 1 and
 * db_back_adr is set to the address we are trying to access.
 * if a fault occurs (see traps.c) the handler will check these values
 * and if they match, will cause a return from set_db_back_handler
 * with a non-zero result. (cf setjmp/longjmp)
 */
int db_back_access;
unsigned long db_back_addr;
unsigned long db_back_handler[3];
extern int set_db_back_handler(void * handler_addr);

int db_is_dbaccess_addr(unsigned long addr) {
  return db_back_access && db_back_addr == addr;
}

int db_is_dbaccess(void) {
  return db_back_access;
}

static void dispatch_readbytes(struct dbif_request *req)
{
    volatile struct dbif_response *rsp;
    char *pointer;
    uint16_t n;
    int i;

    pointer = (char *)req->u.readbytes.address;
    n = req->u.readbytes.n;
    DEBUG(2, "Readbytes request for %p, %d received.", pointer, n);
    rsp = get_response();
    rsp->id = req->id;
    rsp->ret_val = n;
    
    for (i = 0; i < n; i++) {
      db_back_access = 1;
      db_back_addr = (unsigned long)pointer;
      if (set_db_back_handler(db_back_handler) == 0) {
	data_page[i] = *pointer++;
      } else {
	printk("memory read %lx failed\n", pointer);
	rsp->ret_val = 0;
	break;
      }
    }
    db_back_access = 0;
}

static void dispatch_writebytes(struct dbif_request *req)
{
    struct dbif_response *rsp;
    char *pointer;
    uint16_t n;
    int i;

    pointer = (char *)req->u.writebytes.address;
    n = req->u.writebytes.n;
    DEBUG(2, "Writebytes request for %p, %d received",
            pointer, n);
    rsp = get_response();
    rsp->id = req->id;
    rsp->ret_val = n;
    for (i = 0; i < n; i++) {
      db_back_access = 1;
      db_back_addr = (unsigned long)pointer;
      if (set_db_back_handler(&db_back_handler) == 0) {
	*pointer++ = data_page[i];
      } else {
	printk("memory write %lx failed\n", pointer);
	rsp->ret_val = 0;
	break;
      }
    }
}

static void dispatch_gather_threads(struct dbif_request *req)
{
    /* FIXME: use scheduler interface to do this */
    struct dbif_response *rsp;
    struct list_head *list_head;
    struct thread *thread;
    int numThreads = 0;
    struct db_thread *db_thread = (struct db_thread *)data_page;

    DEBUG(1, "Gather threads request.");
    /* -1 == max value for uint16_t */
    spin_lock(&thread_list_lock);
    list_for_each(list_head, &thread_list) {
        thread = list_entry(list_head, struct thread, thread_list);
        if (is_app_thread(thread)) {
	    db_thread->id = thread->id;
	    db_thread->flags = thread->flags;
	    db_thread->stack = (unsigned long)thread->stack;
	    db_thread->stack_size = thread->stack_size;
	    db_thread++;
	    numThreads++; /* TODO: handle overflow */
        }
    }
    spin_unlock(&thread_list_lock);
    rsp = get_response();
    rsp->id = req->id;
    if (db_exit == DB_EXIT_SET) {
        rsp->ret_val = -1;
	db_exit = DB_EXIT_DONE;
    } else {
        rsp->ret_val = numThreads;
    }
}

static void clear_all_req_debug_suspend(void) {
    /* FIXME: use scheduler interface to do this */
    struct thread *thread;
    struct list_head *list_head;

    spin_lock(&thread_list_lock);
    list_for_each(list_head, &thread_list)
    {
        thread = list_entry(list_head, struct thread, thread_list);
        if(is_req_debug_suspend(thread))
        {
	  clear_req_debug_suspend(thread);
        }
    }
    spin_unlock(&thread_list_lock);
}

static void activate_watchpoints(void);
static void single_step_thread(struct thread *thread);

static void dispatch_resume_all(struct dbif_request *req) {
    struct dbif_response *rsp;
    struct thread *thread, *sthread;
    struct list_head *list_head;

    DEBUG(1, "Resume_all request.");
    rsp = get_response();
    rsp->id = req->id;
    rsp->ret_val = 0;

    clear_all_req_debug_suspend();
    /* Deal with any watchpointed thread by single stepping it before
     * reactivating watchpoints. */
    /* Can't hold the lock while calling suspend_thread as it may have to sleep */
    while (1) {
      sthread = NULL;
      spin_lock(&thread_list_lock);
      list_for_each(list_head, &thread_list) {
	  thread = list_entry(list_head, struct thread, thread_list);
	  if (is_app_thread(thread) && is_watchpoint(thread)) {
	    sthread = thread;
	    break;
	  }
      }
      spin_unlock(&thread_list_lock);
      if (sthread != NULL) {
	DEBUG(1, "Stepping watchpoint thread %d.", sthread->id);
	single_step_thread(sthread);
	clear_watchpoint(sthread);
	clear_req_debug_suspend(sthread);
      } else {
	break;
      }
    }
    
    activate_watchpoints();
    spin_lock(&thread_list_lock);
    list_for_each(list_head, &thread_list) {
        thread = list_entry(list_head, struct thread, thread_list);
	if (is_app_thread(thread)) {
	  if (is_debug_suspend(thread)) {
	    DEBUG(1, "Resuming thread %d.", thread->id);
	    clear_debug_suspend(thread);
	    db_wake(thread);
	  }
	}
    }
    spin_unlock(&thread_list_lock);
}

static void suspend_thread(struct thread *thread) {
  if (!is_runnable(thread)) {
    /* Thread is blocked but may become runnable again while the
       debugger is in control, e.g. due to a sleep expiring.
       So we make sure that if this happens it will arrange to
       debug_suspend itself. */
    if (!is_debug_suspend(thread)) {
      set_req_debug_suspend(thread);
      set_need_resched(thread);
    }
  } else {
    set_req_debug_suspend(thread);
    set_need_resched(thread);
    /* Busy wait till the thread stops running */
    while(is_runnable(thread) || is_running(thread)){
      nanosleep(RELAX_NS);
    }
    /* debug_suspend indicates whether we stopped voluntarily */

    if(thread->preempt_count != 1) {
      printk("Thread's id=%d preempt_count is 0x%lx\n",
	     thread->id, thread->preempt_count);
      sleep(100);
      printk("Thread's id=%d preempt_count is 0x%lx\n",
	     thread->id, thread->preempt_count);
      BUG_ON(is_runnable(thread));
      BUG();
    }
  }
}

static void deactivate_watchpoints(void);
static void dispatch_suspend_all(struct dbif_request *req) {
    struct dbif_response *rsp;
    struct thread *thread, *sthread;
    struct list_head *list_head;

    DEBUG(1, "Suspend_all request.");
    rsp = get_response();
    rsp->id = req->id;
    rsp->ret_val = 0;
    /* Can't hold the lock while calling suspend_thread as it may have to sleep */
    while (1) {
      sthread = NULL;
      spin_lock(&thread_list_lock);
      list_for_each(list_head, &thread_list) {
	thread = list_entry(list_head, struct thread, thread_list);
	if (is_app_thread(thread) && !(is_debug_suspend(thread) || is_req_debug_suspend(thread))) {
	  sthread = thread;
	  break;
	}
      }
      spin_unlock(&thread_list_lock);
      if (sthread != NULL) {
	DEBUG(1, "Suspending thread %d.", sthread->id);
	suspend_thread(sthread);
      } else {
	break;
      }
    }
    deactivate_watchpoints();
}

static void dispatch_suspend_thread(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.suspend.thread_id;
    DEBUG(1, "Suspend request for thread_id=%d.",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL) {
        DEBUG(1, "Thread of id=%d not found.", thread_id);
        rsp->ret_val = (uint64_t)-1;
    } else {
	DEBUG(1, "Thread id=%d found.", thread_id);
	suspend_thread(thread);
	rsp->ret_val = 0;
    }
    DEBUG(1, "Returning from suspend.");
}

static void single_step_thread(struct thread *thread) {
  struct pt_regs *regs;

  set_stepped(thread);
  if(thread->preempt_count != 1)
    printk("Preempt count = %lx", thread->preempt_count);
  BUG_ON(thread->preempt_count != 1);
  regs = (struct pt_regs*)thread->regs;
  if(regs != NULL) {
    DEBUG(1, " >> Thread %d found (regs=%lx, regs=%lx).",
	  thread->id, thread->regs, thread->regs->rip);
    //BUG_ON(thread->regs->eflags > 0xFFF);
    thread->regs->eflags |= 0x00000100; /* Trap Flag */
  }

  /* do_debug trap which happens soon after waking up, will set tmp to 1
   * */
  db_wake(thread);
  while(is_runnable(thread) || is_running(thread)){
    nanosleep(RELAX_NS);
  }
  clear_stepped(thread);
  /* Here, the thread is not runnable any more, and therefore there will
   * be no exceptions happening on it */
}

static void dispatch_single_step_thread(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.step.thread_id;
    DEBUG(1, "Step request for thread_id=%d.",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL) {
        DEBUG(1, "Thread of id=%d not found.", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }
    else if(!is_debug_suspend(thread) || is_runnable(thread) || is_running(thread)) {
	DEBUG(1, "Thread of id=%d not suspended.", thread_id);
	rsp->ret_val = (uint64_t)-2;
    } else {
        DEBUG(1, "Thread %d found.", thread->id);
	single_step_thread(thread);
        rsp->ret_val = 0;
    }
}

static void dispatch_resume_thread(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.resume.thread_id;
    DEBUG(1, "Resume request for thread_id=%d.",
            thread_id);
    clear_all_req_debug_suspend();
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL) {
        DEBUG(1, "Thread of id=%d not found.", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }

    if(!is_debug_suspend(thread) || is_runnable(thread) || is_running(thread)) {
        DEBUG(1, "Thread of id=%d not suspended (is_debug_suspended=%lx,"
			                        "is_runnable=%lx,"
                                                "is_running=%lx).",
			thread_id,
			is_debug_suspend(thread),
			is_runnable(thread),
			is_running(thread));
        rsp->ret_val = (uint64_t)-2;
    } else {
        DEBUG(1, "Thread %s found (runnable=%d, stepped=%d).",
                thread->name, is_runnable(thread), is_stepped(thread));
        clear_debug_suspend(thread);
	
        db_wake(thread);
        rsp->ret_val = 0;
    }
}

static void dispatch_get_regs(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;
    struct db_regs *regs;

    thread_id = req->u.get_regs.thread_id;
    DEBUG(1, "Get regs request for thread_id=%d.",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL)
    {
        DEBUG(1, "Thread of id=%d not found.", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }
    else
    if(is_runnable(thread) || is_running(thread))
    {
        DEBUG(1, "Thread of id=%d is not blocked.", thread_id);
        rsp->ret_val = (uint64_t)-2;
    }
    else
    {
        DEBUG(1, "Thread id=%d found, regs=%lx.", thread_id, thread->regs);
        rsp->ret_val = 0;
        regs = &rsp->u.regs;
        if(thread->regs == NULL)
        {
            memset(regs, 0, sizeof(struct db_regs));
            regs->rip = thread->ip;
            regs->rsp = thread->sp;
        }
        else
        {
	    regs->xmm0 = thread->fpregs->xmm0;
	    regs->xmm1 = thread->fpregs->xmm1;
	    regs->xmm2 = thread->fpregs->xmm2;
  	    regs->xmm3 = thread->fpregs->xmm3;
            regs->xmm4 = thread->fpregs->xmm4;
	    regs->xmm5 = thread->fpregs->xmm5;
	    regs->xmm6 = thread->fpregs->xmm6;
	    regs->xmm7 = thread->fpregs->xmm7;
	    regs->xmm8 = thread->fpregs->xmm8;
	    regs->xmm9 = thread->fpregs->xmm9;
	    regs->xmm10 = thread->fpregs->xmm10;
	    regs->xmm11 = thread->fpregs->xmm11;
	    regs->xmm12 = thread->fpregs->xmm12;
	    regs->xmm13 = thread->fpregs->xmm13;
	    regs->xmm14 = thread->fpregs->xmm14;
	    regs->xmm15 = thread->fpregs->xmm15;

            regs->r15 = thread->regs->r15;
            regs->r14 = thread->regs->r14;
            regs->r13 = thread->regs->r13;
            regs->r12 = thread->regs->r12;
            regs->rbp = thread->regs->rbp;
            regs->rbx = thread->regs->rbx;
            regs->r11 = thread->regs->r11;
            regs->r10 = thread->regs->r10;
            regs->r9 = thread->regs->r9;
            regs->r8 = thread->regs->r8;
            regs->rax = thread->regs->rax;
            regs->rcx = thread->regs->rcx;
            regs->rdx = thread->regs->rdx;
            regs->rsi = thread->regs->rsi;
            regs->rdi = thread->regs->rdi;
            regs->rip = thread->regs->rip;
            regs->flags = thread->regs->eflags;
            regs->rsp = thread->regs->rsp;
        }
    }
}

static void dispatch_set_ip(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.set_ip.thread_id;
    DEBUG(1, "Set ip request for thread_id=%d.",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL)
    {
        DEBUG(1, "Thread of id=%d not found.", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }
    else
    if(is_runnable(thread) || is_running(thread))
    {
        DEBUG(1, "Thread of id=%d is not blocked.", thread_id);
        rsp->ret_val = (uint64_t)-2;
    }
    else
    if(thread->regs == NULL)
    {
        DEBUG(1, "Regs not available for thread of id=%d .", thread_id);
        rsp->ret_val = (uint64_t)-3;
    }
    else
    {
        DEBUG(1, "Thread %s found.", thread->name);
        rsp->ret_val = 0;
        //printk("Updated RIP from %lx, to %lx\n", thread->regs->rip, req->u.set_ip.ip);
        thread->regs->rip = req->u.set_ip.ip;
    }
}

static void dispatch_get_thread_stack(struct dbif_request *req)
{
    struct dbif_response *rsp;
    struct thread *thread;

    thread = get_thread_by_id(req->u.get_stack.thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread != NULL)
    {
	DEBUG(1, "Get stack request, thread %s.", thread->name);
        rsp->ret_val = (uint64_t)thread->stack;
        rsp->ret_val2 = thread->stack_size;
    } else
    {
        rsp->ret_val = (uint64_t)-1;
        rsp->ret_val = 0;
    }
}

__attribute__((weak)) void guk_dispatch_app_specific1_request(
        struct dbif_request *req, struct dbif_response *rsp)
{
    printk("App specific debug request, but no implementation!.\n");
    rsp->ret_val = (uint64_t)-1;
}

static void dispatch_app_specific1(struct dbif_request *req)
{
    struct dbif_response *rsp;

    DEBUG(1, "App specific1 request received.");
    rsp = get_response();
    guk_dispatch_app_specific1_request(req, rsp);
    rsp->id = req->id;
}

static void dispatch_db_debug(struct dbif_request *req)
{
    struct dbif_response *rsp;
    int new_level = req->u.db_debug.level;
    rsp = get_response();
    rsp->id = req->id;
    printk("Setting debug level to %d\n", new_level);
    rsp->ret_val = db_debug_level;
    db_debug_level = new_level;
}

static void dispatch_db_signoff(struct dbif_request *req) {
  db_exit = DB_EXIT_FIN;
}

/* Returns a db_watchpoint if address is a watchpoint address OR
 * is on a watchpointed page.
 */
static struct db_watchpoint *get_watchpoint(unsigned long address) {
    struct list_head *list_head;
    struct db_watchpoint *wp;
    struct db_watchpoint *result = NULL;
    unsigned long page_address = round_pgdown(address);
    spin_lock(&watchpoints_list_lock);
    list_for_each(list_head, &watchpoints_list) {
        wp = list_entry(list_head, struct db_watchpoint, list);
        if(wp->address == address) {
	  result = wp;
	  break;
	} else if (wp->page_address == page_address) {
	  result = wp;
	  /* but keep looking for an exact match */
        }
    }
    spin_unlock(&watchpoints_list_lock);
    return result;
}


static void activate_watchpoint(struct db_watchpoint *wp) {
    guk_unmap_page_pfn(wp->page_address, wp->pfn);
    DEBUG(2, "unmapped page at %lx\n", wp->page_address);
}

static void deactivate_watchpoint(struct db_watchpoint *wp) {
    guk_remap_page_pfn(wp->page_address, wp->pfn);
    DEBUG(2, "remapped page at %lx\n", wp->page_address);
}

int db_is_watchpoint(unsigned long address) {
  struct thread *thread = current;
  struct db_watchpoint *wp = get_watchpoint(address);
  if (wp == NULL) return 0;
  /* So either the current thread hit a watchpoint or it hit a watchpointed page. */
  if (wp->address == address) {
    /* match, suspend thread */
    struct fp_regs *fpregs = thread->fpregs;
    asm (save_fp_regs_asm : : [fpr] "r" (fpregs));
    DEBUG(2, "watch point: %lx\n", address);
    BUG_ON(!is_preemptible(thread));
    thread->flags |= WATCH_FLAG;
    thread->db_data = wp;
    set_req_debug_suspend(thread);
    set_need_resched(thread);
  } else {
    /* Some other access to watchpointed page.
       In an SMP context we really should suspend all threads before
       unprotecting the page as otherwise there is a tiny window where
       a watchpoint on this page in a thread on another CPU could be missed.
       But we can't do that from here and it's a lot of work.
     */
    deactivate_watchpoint(wp);
    per_cpu(smp_processor_id(), db_support) = wp;
    thread->regs->eflags |= 0x00000100; /* Trap Flag */
  }
  return 1;
}

/* This is called from the trap[ handler after db_is_watchpoint returns from an access
   to a watchpointed page. We need to protect the page again and continue.
 */
int db_watchpoint_step(void) {
   struct thread *thread = current;
   struct db_watchpoint *wp =  (struct db_watchpoint *) per_cpu(smp_processor_id(), db_support);
   if (wp == NULL) return 0;
   activate_watchpoint(wp);
   thread->regs->eflags &= ~0x00000100;
   per_cpu(smp_processor_id(), db_support) = NULL;
   return 1;
}


static void validate_watchpoint(unsigned long address) {
  unsigned long data;
  DEBUG(2, "validate_watchpoint %lx\n", address);
  db_back_access = 1;
  db_back_addr = address;
  if (set_db_back_handler(db_back_handler) == 0) {
    data = *(unsigned long *)address;
    DEBUG(2, "validation failed - read watchpoint word\n");
  } else {
    DEBUG(2, "validation ok\n");
  }
  db_back_access = 0;
}

USED static void activate_watchpoints(void) {
    struct list_head *list_head;
    struct db_watchpoint *wp;
    spin_lock(&watchpoints_list_lock);
    list_for_each(list_head, &watchpoints_list) {
      wp = list_entry(list_head, struct db_watchpoint, list);
      activate_watchpoint(wp);
    }
    spin_unlock(&watchpoints_list_lock);
}

USED static void deactivate_watchpoints(void) {
    struct list_head *list_head;
    struct db_watchpoint *wp;
    spin_lock(&watchpoints_list_lock);
    list_for_each(list_head, &watchpoints_list) {
      wp = list_entry(list_head, struct db_watchpoint, list);
      deactivate_watchpoint(wp);
    }
    spin_unlock(&watchpoints_list_lock);
}

static void dispatch_db_activate_watchpoint(struct dbif_request *req) {
    struct db_watchpoint *wp;
    struct dbif_response *rsp;
    unsigned long pte;
    rsp = get_response();
    rsp->id = req->id;

    wp = xmalloc(struct db_watchpoint);
    wp->address = req->u.watchpoint_request.address;
    wp->size = req->u.watchpoint_request.size;
    wp->kind = req->u.watchpoint_request.kind;
    spin_lock(&watchpoints_list_lock);
    list_add_tail(&wp->list, &watchpoints_list);
    spin_unlock(&watchpoints_list_lock);
    wp->pfn = guk_not11_virt_to_pfn(wp->address, &pte);
    wp->page_address = round_pgdown(wp->address);
    activate_watchpoint(wp);
    if (db_debug_level >= 2) {
      validate_watchpoint(wp->address);
    }
    rsp->ret_val = 1;
}

static void dispatch_db_deactivate_watchpoint(struct dbif_request *req) {
    struct db_watchpoint *wp, *wpp;
    struct dbif_response *rsp;
    rsp = get_response();
    rsp->id = req->id;

    wp = get_watchpoint(req->u.watchpoint_request.address);
    if (wp != NULL && wp->address == req->u.watchpoint_request.address) {
      /* remove */
      spin_lock(&watchpoints_list_lock);
      list_del(&wp->list);
      spin_unlock(&watchpoints_list_lock);
      /* any other watchpoints on this page? */
      wpp = get_watchpoint(req->u.watchpoint_request.address);
      if (wpp == NULL) {
	deactivate_watchpoint(wp);
      }
      rsp->ret_val = 1;
    } else {
      rsp->ret_val = 0;
    }
}

static void dispatch_db_watchpoint_info(struct dbif_request *req) {
    struct db_watchpoint *wp;
    struct dbif_response *rsp;
    struct thread *thread;
    rsp = get_response();
    rsp->id = req->id;
    thread = get_thread_by_id(req->u.get_stack.thread_id);
    if (thread == NULL || !is_watchpoint(thread)) {
      rsp->ret_val = -1;
    } else {
      wp = (struct db_watchpoint*) thread->db_data;
      rsp->ret_val = wp->address;
      rsp->ret_val2 = wp->kind;
    }
}

static void ring_thread(void *unused)
{
    int more, notify;
    struct thread *this_thread;

    DEBUG(3, "Ring thread.");
    this_thread = current;
    db_in_use = 1;

    for(;;)
    {
        int nr_consumed=0;
        RING_IDX cons, rp;
        struct dbif_request *req;

moretodo:
        rp = ring.sring->req_prod;
        rmb(); /* Ensure we see queued requests up to 'rp'. */

        while ((cons = ring.req_cons) != rp)
        {
            DEBUG(3, "Got a request at %d", cons);
            req = RING_GET_REQUEST(&ring, cons);
            DEBUG(3, "Request type=%d", req->type);
            switch(req->type)
            {
                case REQ_READ_U64:
                    dispatch_read_u64(req);
                    break;
                case REQ_WRITE_U64:
                    dispatch_write_u64(req);
                    break;
                case REQ_READBYTES:
                    dispatch_readbytes(req);
                    break;
                case REQ_WRITEBYTES:
                    dispatch_writebytes(req);
                    break;
                case REQ_GATHER_THREAD:
                    dispatch_gather_threads(req);
                    break;
                case REQ_SUSPEND_THREAD:
                    dispatch_suspend_thread(req);
                    break;
                case REQ_RESUME_THREAD:
                    dispatch_resume_thread(req);
                    break;
                case REQ_SUSPEND_ALL:
                    dispatch_suspend_all(req);
                    break;
                case REQ_RESUME_ALL:
                    dispatch_resume_all(req);
                    break;
                case REQ_SINGLE_STEP_THREAD:
                    dispatch_single_step_thread(req);
                    break;
                case REQ_GET_REGS:
                    dispatch_get_regs(req);
                    break;
                case REQ_GET_THREAD_STACK:
                    dispatch_get_thread_stack(req);
                    break;
                case REQ_SET_IP:
                    dispatch_set_ip(req);
                    break;
                case REQ_APP_SPECIFIC1:
                    dispatch_app_specific1(req);
                    break;
	        case REQ_DB_DEBUG:
		    dispatch_db_debug(req);
		    break;
	        case REQ_SIGNOFF:
	   	    dispatch_db_signoff(req);
		    break;
	        case REQ_ACTIVATE_WP:
		    dispatch_db_activate_watchpoint(req);
		    break;
	        case REQ_DEACTIVATE_WP:
	   	    dispatch_db_deactivate_watchpoint(req);
		    break;
	        case REQ_WP_INFO:
	   	    dispatch_db_watchpoint_info(req);
		    break;
                default:
                    BUG();
            }
            ring.req_cons++;
            nr_consumed++;
        }

        DEBUG(3, "Backend consumed: %d requests", nr_consumed);
        RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&ring, notify);
        DEBUG(3, "Rsp producer=%d", ring.sring->rsp_prod);
        DEBUG(3, "Pushed responces and notify=%d", notify);
        if(notify)
            notify_remote_via_evtchn(port);
        DEBUG(3, "Done notifying.");

        preempt_disable();
        block(this_thread);
        RING_FINAL_CHECK_FOR_REQUESTS(&ring, more);
        if(more)
        {
            db_wake(this_thread);
            preempt_enable();
            goto moretodo;
        }

        preempt_enable();
        schedule();
    }
}

static void db_evt_handler(evtchn_port_t port, void *data)
{
    struct thread *thread = (struct thread *)data;

    DEBUG(3, "DB evtchn handler.");
    db_wake(thread);
    /* TODO - it would be good to resched here */
}

extern int guk_app_main(struct app_main_args *args);
static void handle_connection(int dom_id)
{
    char *err, node[256];
    struct dbif_sring *sring;
    struct thread *thread;
    struct app_main_args aargs;

    /* Temporary: for debugging we accept all the connections */
    db_in_use = 0;
    db_exit = 0;
    sprintf(node, "device/db/requests/%d", dom_id);
    if(db_in_use)
    {
        err = xenbus_printf(XBT_NIL,
                            node,
                            "already-in-use",
                            "");
        return;
    }

    /* Allocate shared buffer for the ring */
    sring = (void *)alloc_page();
    sprintf((char *)sring, "To test ringu");
    /*printk(" ===> spage=%lx, pfn=%lx, mfn=%lx\n", sring,
                                                  virt_to_pfn(sring),
                                                  virt_to_mfn(sring));
    */
    SHARED_RING_INIT(sring);
    ring_gref = gnttab_grant_access(dom_id,
                                    virt_to_mfn(sring),
                                    0);
    BACK_RING_INIT(&ring, sring, PAGE_SIZE);
    thread = create_debug_thread("db-ring-handler", ring_thread, NULL);
    //printk("Thread pointer %p, name=%s\n", thread, thread->name);
    /* Allocate event channel */
    BUG_ON(evtchn_alloc_unbound(dom_id,
                                db_evt_handler,
                                ANY_CPU,
                                thread,
                                &port));


    data_page = (char*)alloc_page();
    data_grant_ref = gnttab_grant_access(dom_id, virt_to_mfn(data_page), 0);
    err = xenbus_printf(XBT_NIL,
                        node,
                        "connected",
                        "gref=%d evtchn=%d dgref=%d", ring_gref, port, data_grant_ref);

    aargs.cmd_line = cmd_line;
    guk_app_main(&aargs);
}

/* Small utility function to figure out our domain id */
static domid_t get_self_id(void)
{
    char *dom_id;
    domid_t ret;

    BUG_ON(xenbus_read(XBT_NIL, "domid", &dom_id));
    sscanf(dom_id, "%d", &ret);

    return ret;
}

void init_db_backend(char *cmdl)
{
    xenbus_transaction_t xbt;
    char *err, *message, *watch_rsp;
    int retry;

    printk("Initialising debugging backend\n");
    cmd_line = cmdl;
    db_back_access = 0;
again:
    retry = 0;
    message = NULL;
    err = xenbus_transaction_start(&xbt);
    if (err) {
        printk("Error starting transaction\n");
    }

    err = xenbus_printf(xbt,
                        "device/db",
                        "requests",
                        "%s",
                        "directory watched for attach request");
    if (err) {
        message = "writing requset node";
        goto abort_transaction;
    }


    err = xenbus_transaction_end(xbt, 0, &retry);
    if (retry) {
            goto again;
        printk("completing transaction\n");
    } else if (err)
	free(err);

    goto done;

abort_transaction:
    err = xenbus_transaction_end(xbt, 1, &retry);
    if(err)
	free(err);

done:

    printk("Initialising debugging backend complete (This domain id=%d)\n",
            get_self_id());

    watch_rsp = xenbus_watch_path(XBT_NIL, "device/db/requests", "db-back");
    if(watch_rsp)
	free(watch_rsp);
    for(;;)
    {
        char *changed_path;
        int dom_id;

        changed_path = xenbus_read_watch("db-back");
        if (trace_db_back()) tprintk("Path changed: %s\n", changed_path);
        dom_id = -1;
        sscanf(changed_path, "device/db/requests/%d", &dom_id);
        if (trace_db_back()) tprintk("Extracted id: %d\n", dom_id);
        free(changed_path);
        /* Ignore the response we are writing in the same directory */
        if(dom_id >= 0 &&
            (strlen(changed_path) < strlen("device/db/requests/XXXXXX")))
            handle_connection(dom_id);
    }
}

void guk_db_exit_notify_and_wait(void) {
  if (db_in_use) {
    // Report a termination and wait for signoff
    // unless frontend has already signed off.
    if (db_exit == DB_EXIT_UNSET) db_exit = DB_EXIT_SET;
    while (db_exit != DB_EXIT_FIN) {
      schedule();
    }
  }
}
