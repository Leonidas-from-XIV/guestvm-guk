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
 * This is some support that is specific to the Maxine Java virtual machine
 * that is difficult to factor out completely. However, it is essentially harmless
 * in a non-Maxine environment.
 *
 * Maxine requires that R14 points to an area called the "local space".
 * Maxine compiled code assumes in particular that it can execute a MOV R14, [R14]
 * instruction at certain points as part of its GC safepoint mechanism.
 * Maxine threads set this up explictly, but in case a microkernel thread
 * execute some Java code, e.g., in response to an interrupt, we make sure
 * that R14 has a fake local space value for all microkernel threads.
 * 
 */

#ifndef _MAXINE_LS_H_
#define _MAXINE_LS_H_

void init_maxine(void);
void *get_local_space(void);

#endif /* _MAXINE_LS_H_ */

