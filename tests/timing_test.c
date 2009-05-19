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
  Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
 */
#include <os.h>
#include <sched.h>
#include <spinlock.h>
#include <time.h>
#include <xmalloc.h>

extern u32 rand_int(void);
extern void seed(u32 s);

/* Objects = threads in this test */
#define NUM_OBJECTS     1000
struct object
{
    struct thread *thread;
};
struct object objects[NUM_OBJECTS];
static int remaining_threads_count;
static DEFINE_SPINLOCK(thread_count_lock);
static DEFINE_SPINLOCK(time_lock);
u64 last_time_val = 0;
int last_cpu = 0;
u64 total_time_tests = 0;


void thread_fn(void *pickled_id)
{
    struct timeval timeval_start, timeval_end; 
    u32 ms = rand_int() & 0xFFF;
    int id = (int)(u64)pickled_id;
    u64 current_time;
    
    gettimeofday(&timeval_start);
    gettimeofday(&timeval_end);
    while(SECONDS(timeval_end.tv_sec - timeval_start.tv_sec) +
          MICROSECS(timeval_end.tv_usec - timeval_start.tv_usec) <= 
          MILLISECS(ms)){
        spin_lock(&time_lock);
        gettimeofday(&timeval_end);
        current_time = SECONDS(timeval_end.tv_sec) + 
                       MICROSECS(timeval_end.tv_usec);
        if(current_time < last_time_val)
        {
            printk("Time went backwards: last_time_val=%lx (CPU=%d), "
                    "current_time=%lx (CPU=%d), difference: %lldns\n",
                    last_time_val, last_cpu,
                    current_time, smp_processor_id(),
                    (last_time_val - current_time));
            ok_exit();
        }
        last_time_val = current_time;
        last_cpu = smp_processor_id();
        total_time_tests++;
        spin_unlock(&time_lock);
    }

    spin_lock(&thread_count_lock); 
    remaining_threads_count--;
    if(remaining_threads_count == 0)
    {
        spin_unlock(&thread_count_lock); 
        printk("ALL SUCCESSFUL\n"); 
        printk("Total time tests done: %lld\n", total_time_tests);
        ok_exit();
    }
    
    spin_unlock(&thread_count_lock); 
    if(id % (NUM_OBJECTS / 100) == 0)
        printk("Success, exiting thread: %s, remaining %d.\n", 
                current->name,
                remaining_threads_count);

}

static void allocate_object(u32 id)
{
    char buffer[256];

    sprintf(buffer, "thread_bomb_%d", id);
    objects[id].thread = create_thread(strdup(buffer), thread_fn, UKERNEL_FLAG,
                        (void *)(u64)id); 
    if(id % (NUM_OBJECTS / 20) == 0)
        printk("Allocated thread id=%d\n", id);
}

static void USED thread_spawner(void *p)
{
    u32 count = 1;


    seed((u32)NOW());    
    memset(objects, 0, sizeof(struct object) * NUM_OBJECTS);
    printk("Timing tester started.\n");
    for(count=0; count < NUM_OBJECTS; count++)
    {
        spin_lock(&thread_count_lock); 
        /* The first object has been accounted for twice (in app_main, and here) 
         * we therefore don't account for the last one */
        if(count != NUM_OBJECTS - 1)
            remaining_threads_count++;
        spin_unlock(&thread_count_lock); 
        allocate_object(count);
        if(!(rand_int() & 0xFF))
            schedule();
    }
}

int guk_app_main(void *args)
{
    printk("Private appmain.\n");
    /* assign 1 to prevent an exiting thread thinking it's the last one */
    remaining_threads_count = 1;
    create_thread("thread_spawner", thread_spawner, UKERNEL_FLAG, NULL);

    return 0;
}
