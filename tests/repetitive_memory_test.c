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

#define NUM_OBJECTS     1000
#define MAX_OBJ_SIZE    0x1FFF
struct object
{
    int size;
    void *pointer;
    int allocated;
    int read;
};
struct object objects[NUM_OBJECTS];

static void verify_object(struct object *object)
{
    u32 id = ((unsigned long)object - (unsigned long)objects) / sizeof(struct object);
    uint8_t *test_value = (uint8_t *)object->pointer, *pointer;
    int i;

    if(!object->allocated)
        return;
    object->read = 1; 
    for(i=0; i<object->size; i++)
        if(test_value[i] != (id & 0xFF))
            goto fail;
    return;
    
fail:                
    printk("Failed verification for object id=%d, size=%d, i=%d,"
           " got=%x, expected=%x\n",
            id, object->size, i, test_value[i], id & 0xFF);
 
    pointer = (uint8_t *)objects[id].pointer;

    printk("Object %d, size: %d, pointer=%lx\n",
               id, objects[id].size, objects[id].pointer);
    for(i=0; i<objects[id].size; i++)
         printk("%2x", pointer[i]);
    printk("\n");

  
    ok_exit();
}


static void allocate_object(u32 id)
{
    /* If object already allocated, don't allocate again */
    if(objects[id].allocated)
        return;
    /* +1 protects against 0 allocation */
    objects[id].size = (rand_int() & MAX_OBJ_SIZE) + 1;
    objects[id].pointer = malloc(objects[id].size);
    objects[id].read = 0;
    objects[id].allocated = 1;
    memset(objects[id].pointer, id & 0xFF, objects[id].size);
    if(id % (NUM_OBJECTS / 20) == 0)
        printk("Allocated object size=%d, pointer=%p.\n",
                objects[id].size, objects[id].pointer);
}

static void free_object(struct object *object)
{
    
    if(!object->allocated)
        return;
    free(object->pointer);
    object->size = 0;
    object->read = 0;
    object->allocated = 0;
    object->pointer = 0;
}

static void USED mem_allocator(void *p)
{
    u32 count, loop_count;


    loop_count = 0;
    seed((u32)NOW());    
again:    
    memset(objects, 0, sizeof(struct object) * NUM_OBJECTS);
    printk("Repetitive memory allocator tester started.\n");
    for(count=0; count < NUM_OBJECTS; count++)
    {
        allocate_object(count);
        /* Randomly read an object off */
        if(rand_int() & 1)
        {
            u32 to_read = count & rand_int();

            verify_object(&objects[to_read]);
        }
        /* Randomly free an object */
        if(rand_int() & 1)
        {
            u32 to_free = count & rand_int();

            free_object(&objects[to_free]);
        }
    }
    
    printk("Destroying remaining objects.\n"); 
    for(count = 0; count < NUM_OBJECTS; count++)
    {
        if(objects[count].allocated)
        {
            verify_object(&objects[count]);
            free_object(&objects[count]);
        }
    } 

    loop_count++;
    if(loop_count < 50)
    {
        printk("Finished loop %d. Repeating the test.\n", loop_count);
        goto again;
    }

    printk("SUCCESSFUL\n"); 
    do_exit();
}

int guk_app_main(start_info_t *si)
{
    printk("Private appmain.\n");
    create_thread("mem_allocator", mem_allocator, UKERNEL_FLAG, NULL);

    return 0;
}
