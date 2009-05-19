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
    u8 *test_value = (u8 *)object->pointer, *pointer;
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
 
    pointer = (u8 *)objects[id].pointer;

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
