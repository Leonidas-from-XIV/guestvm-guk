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
#include <guk/trace.h>

#include <x86/traps.h>

#include <dbif.h>
#include <spinlock.h>

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
static int is_maxine_thread(struct thread *thread) {
    return strcmp(thread->name, "maxine") == 0;
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
    DEBUG(2, "Read request for %p recieved.\n", pointer);
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
    DEBUG(2, "Write request for %p recieved, value=%lx, current_value=%lx.\n",
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

static void dispatch_readbytes(struct dbif_request *req)
{
    volatile struct dbif_response *rsp;
    char *pointer;
    uint16_t n;
    int i;

    pointer = (char *)req->u.readbytes.address;
    n = req->u.readbytes.n;
    set_db_back_handler(db_back_handler);
    DEBUG(2, "Readbytes request for %p, %d received.\n", pointer, n);
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
    DEBUG(2, "Writebytes request for %p, %d received\n",
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

    DEBUG(1, "Gather threads request.\n");
    /* -1 == max value for uint16_t */
    spin_lock(&thread_list_lock);
    list_for_each(list_head, &thread_list) {
        thread = list_entry(list_head, struct thread, thread_list);
        if (!is_ukernel(thread) || is_maxine_thread(thread)) {
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

static void dispatch_suspend_threads(struct dbif_request *req) {
}

static void dispatch_suspend_thread(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.suspend.thread_id;
    DEBUG(1, "Suspend request for thread_id=%d.\n",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL) {
        DEBUG(1, "Thread of id=%d not found.\n", thread_id);
        rsp->ret_val = (uint64_t)-1;
    } else {
	DEBUG(1, "Thread id=%d found.\n", thread_id);
	if (!is_runnable(thread)) {
	    /* Thread is blocked but may become runnable again while the
	       debugger is in control, e.g. due to a sleep expiring.
	       So we make sure that if this happens it will arrange to
	    // debug_suspend itself. */
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
			thread_id, thread->preempt_count);
		sleep(100);
		printk("Thread's id=%d preempt_count is 0x%lx\n",
			thread_id, thread->preempt_count);
		BUG_ON(is_runnable(thread));
		BUG();
	    }
	}
	rsp->ret_val = 0;
    }
    DEBUG(1, "Returning from suspend.\n");
}

static int debugging_count = 0;

static void dispatch_single_step_thread(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.step.thread_id;
    DEBUG(1, "Step request for thread_id=%d.\n",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL) {
        DEBUG(1, "Thread of id=%d not found.\n", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }
    else if(!is_debug_suspend(thread) || is_runnable(thread) || is_running(thread)) {
	DEBUG(1, "Thread of id=%d not suspended.\n", thread_id);
	rsp->ret_val = (uint64_t)-2;
    } else {
        struct pt_regs *regs;

        if(debugging_count == 0)
            debugging_count = 15;

        DEBUG(1, "Thread %s found.\n", thread->name);
        set_stepped(thread);
        if(thread->preempt_count != 1)
            printk("Preempt count = %lx\n", thread->preempt_count);
        BUG_ON(thread->preempt_count != 1);
        regs = (struct pt_regs*)thread->regs;
        if(regs != NULL) {
            DEBUG(1, " >> Thread %s found (regs=%lx, regs=%lx).\n",
                    thread->name, thread->regs, thread->regs->rip);
            BUG_ON(thread->regs->eflags > 0xFFF);
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

        rsp->ret_val = 0;
    }
}

static void dispatch_resume_thread(struct dbif_request *req)
{
    struct dbif_response *rsp;
    uint16_t thread_id;
    struct thread *thread;

    thread_id = req->u.resume.thread_id;
    DEBUG(1, "Resume request for thread_id=%d.\n",
            thread_id);
    clear_all_req_debug_suspend();
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL) {
        DEBUG(1, "Thread of id=%d not found.\n", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }

    if(!is_debug_suspend(thread) || is_runnable(thread) || is_running(thread)) {
        DEBUG(1, "Thread of id=%d not suspended (is_debug_suspended=%lx,"
			                        "is_runnable=%lx,"
                                                "is_running=%lx).\n",
			thread_id,
			is_debug_suspend(thread),
			is_runnable(thread),
			is_running(thread));
        rsp->ret_val = (uint64_t)-2;
    } else {
        DEBUG(1, "Thread %s found (runnable=%d, stepped=%d).\n",
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
    DEBUG(1, "Get regs request for thread_id=%d.\n",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL)
    {
        DEBUG(1, "Thread of id=%d not found.\n", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }
    else
    if(is_runnable(thread) || is_running(thread))
    {
        DEBUG(1, "Thread of id=%d is not blocked.\n", thread_id);
        rsp->ret_val = (uint64_t)-2;
    }
    else
    {
        DEBUG(1, "Thread %s found.\n", thread->name);
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
    DEBUG(1, "Set ip request for thread_id=%d.\n",
            thread_id);
    thread = get_thread_by_id(thread_id);
    rsp = get_response();
    rsp->id = req->id;
    if(thread == NULL)
    {
        DEBUG(1, "Thread of id=%d not found.\n", thread_id);
        rsp->ret_val = (uint64_t)-1;
    }
    else
    if(is_runnable(thread) || is_running(thread))
    {
        DEBUG(1, "Thread of id=%d is not blocked.\n", thread_id);
        rsp->ret_val = (uint64_t)-2;
    }
    else
    if(thread->regs == NULL)
    {
        DEBUG(1, "Regs not available for thread of id=%d .\n", thread_id);
        rsp->ret_val = (uint64_t)-3;
    }
    else
    {
        DEBUG(1, "Thread %s found.\n", thread->name);
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
	DEBUG(1, "Get stack request, thread %s.\n", thread->name);
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

    DEBUG(1, "App specific1 request received.\n");
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

static void ring_thread(void *unused)
{
    int more, notify;
    struct thread *this_thread;

    DEBUG(3, "Ring thread.\n");
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
            DEBUG(3, "Got a request at %d\n", cons);
            req = RING_GET_REQUEST(&ring, cons);
            DEBUG(3, "Request type=%d\n", req->type);
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
                case REQ_SUSPEND_THREADS:
                    dispatch_suspend_threads(req);
                    break;
                case REQ_RESUME_THREAD:
                    dispatch_resume_thread(req);
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
                default:
                    BUG();
            }
            ring.req_cons++;
            nr_consumed++;
        }

        DEBUG(3, "Backend consumed: %d requests\n", nr_consumed);
        RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&ring, notify);
        DEBUG(3, "Rsp producer=%d\n", ring.sring->rsp_prod);
        DEBUG(3, "Pushed responces and notify=%d\n", notify);
        if(notify)
            notify_remote_via_evtchn(port);
        DEBUG(3, "Done notifying.\n");

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

    DEBUG(3, "DB evtchn handler.\n");
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

/*****************************************************************************/
/* DEBUGGING INTERFACE DEBUGGING, IN USE ONLY IF THE
 * "test_debugging_interface" THREAD IS CREATED IN THE INIT FUNCTION.        */
/*****************************************************************************/

USED static void stepping_thread(void *unused)
{
    struct thread *thread = (struct thread *)unused;
    struct pt_regs *regs;
    int i;
    int last_count;

    i = 0;

    sleep(500);
    printk(" >>>>> Recieved a request to step %s\n", thread->name);
    set_debug_suspend(thread);
    set_need_resched(thread);
    while(is_runnable(thread)) {
	nanosleep(RELAX_NS);
    };
    sleep(10);

    last_count = debugging_count = 5;

next_step:
    if(debugging_count != last_count)
    {
        printk(" >>>> Count changed to: %d\n", debugging_count);
        last_count = debugging_count;
    }
    set_stepped(thread);

    /* Threads scheduled for single stepping need to be preempted in a very well
     * known location (either: preempt_schedule() or preempt_schedule_irq()), in
     * both cases the threads will enter the above functions with preemption
     * enabled (preempt_count == 0), the preempt_count will be then incremented
     * (by 1) in schedule(). */
    BUG_ON(thread->preempt_count != 1);
    /* Since the thread is preemtible, we can safely set the debug trap flag.
     * If we have trap registers, we set the flag there (this will allow the
     * thread to be scheduled back in uninterrupted, and cause the trap once it
     * executes _real_ next instruction, not just an instruction from schedule).
     * If, however, we don't have the regs, it means that the thread executed
     * schedule() directly (i.e. either by direct call to schedule, or as the
     * part of preemption mechanism). In such case, we don't have an option
     * other than setting the trap flag on the top of the stack (it is popf-ed
     * as soon as the thread is scheduled back in). */
    regs = (struct pt_regs*)thread->regs;
    if(regs != NULL)
    {
        /* Bug if the flags are out of range (bugs to do with saving registers
         * were common) */
        BUG_ON(thread->regs->eflags > 0xFFF);
        thread->regs->eflags |= 0x00000100; /* Trap Flag */
    }

    /* Let the thread execute a single step (or a single preemption disabled
     * block) */
    db_wake(thread);
    while(is_runnable(thread) || is_running(thread)) {
	nanosleep(RELAX_NS);
    }
    clear_stepped(thread);

    /* Print a message every 100 steps */
    if(i%100 == 0) printk("%d steps executed (IP=%lx).\n", i, thread->regs->rip);
    i++;
    goto next_step;
}


/* This thread runs a tight loop. It prints out a message every 10M loops. If
 * the thread is suspended and single stepped it will print out a message every
 * loop. Additionally the thread executes NOW() and schedule() to test if
 * preempt_disable/_enable works fine. */
USED static void test_debugging_interface(void *unused)
{
    int i;
    s_time_t time;

    i=0;
    /* Create this thread if you want to test single stepping internally
     * (without the frontend) */
    //create_thread("stepping-thread", stepping_thread, XENOS_FLAG, current);
    printk("About to start the debugging test loop.\n");
loop_again:
    time = NOW();
    if(debugging_count > 1)
    {
        debugging_count--;
        printk("Decremented the debugging_count to %d\n", debugging_count);
    }
    if(i%10000000 == 0)
        printk("I=%d\n", i);
    if(debugging_count == 1)
    {
        printk("Stepped thead: About to schedule.\n");
        schedule();
        debugging_count = 5;
    }
    i++;
    goto loop_again;
    printk("Executed the stepping thread\n");
}

/*****************************************************************************/
/* END OF DEBUGGING INTERFACE DEBUGGING */
/*****************************************************************************/


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

    //create_thread("debugging-interface-test", test_debugging_interface, XENOS_FLAG, NULL);
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
