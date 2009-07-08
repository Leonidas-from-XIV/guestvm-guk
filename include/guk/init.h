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
 * automatic init features
 * needs a special section (called .GUK_init) in the linker script
 *
 * author: Harald Roeck, Sun Microsystems, Inc., summer intern 2008.
 */

#ifndef INIT_H
#define INIT_H

/* an init function is automatically executed during bootup; 
 * init functions should be static to the file they are declared in, take no arguments
 * and return 0 on success and non-zero on failure. init functions may not use any scheduler
 * related mechanisms like sleep or creating threads.
 */

/* type of an init function */
typedef int (*init_function)(void);

/* macro to put a pointer to the init function into a special section of the code */
#define DECLARE_INIT(func) \
    static init_function __init_##func __attribute__((__used__))\
           __attribute__((section (".GUK_init"))) = &func

/* pointers to the init section in the binary */
extern init_function __init_start[], __init_end[];

#endif /* INIT_H */
