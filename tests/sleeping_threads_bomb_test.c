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
  Author: Grzegorz Milos
 */
#include <guk/os.h>
#include <guk/sched.h>
#include <guk/spinlock.h>
#include <guk/time.h>
#include <guk/xmalloc.h>

extern u32 rand_int(void);
extern void seed(u32 s);

/* Objects = threads in this test */
#define NUM_OBJECTS     10000
struct object
{
    struct thread *thread;
};
struct object objects[NUM_OBJECTS];
static int remaining_threads_count;
static DEFINE_SPINLOCK(thread_count_lock);

void thread_fn(void *pickled_id)
{
    struct timeval timeval_start, timeval_end, timeval_sleep; 
    u32 ms = rand_int() & 0xFFF;
    int id = (int)(u64)pickled_id;
    
    gettimeofday(&timeval_start);
    gettimeofday(&timeval_end);
    while(SECONDS(timeval_end.tv_sec - timeval_start.tv_sec) +
          MICROSECS(timeval_end.tv_usec - timeval_start.tv_usec) <= 
          MILLISECS(ms)){
#define SLEEP_PERIOD    500        
        if(!(rand_int() & 0xFF))
        {
            gettimeofday(&timeval_sleep);
            sleep(SLEEP_PERIOD);
            gettimeofday(&timeval_end);
            if(SECONDS(timeval_end.tv_sec - timeval_sleep.tv_sec) +
               MICROSECS(timeval_end.tv_usec - timeval_sleep.tv_usec) <= 
               MILLISECS(SLEEP_PERIOD)){
                printk("Thread: %s didn't sleep for long enough.\n",
                        current->name);
                printk("FAILED.\n");
                ok_exit();
            }
               
        }
        if(!(rand_int() & 0x1))
            schedule();
        gettimeofday(&timeval_end);
    }

    spin_lock(&thread_count_lock); 
    remaining_threads_count--;
    if(remaining_threads_count == 0)
    {
        spin_unlock(&thread_count_lock); 
        printk("ALL SUCCESSFUL\n"); 
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
    printk("Sleeping threads bomb tester started.\n");
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

int guk_app_main(start_info_t *si)
{
    printk("Private appmain.\n");
    /* assign 1 to prevent an exiting thread thinking it's the last one */
    remaining_threads_count = 1;
    create_thread("thread_spawner", thread_spawner, UKERNEL_FLAG, NULL);

    return 0;
}
