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

#include <maxine_ls.h>

// NUM_LOCAL_SPACE_MEMBERS needs to be at least as large as the maxine thread local space.
// This is currently conservative so allows for maxine changes without altering this code.

#define NUM_LOCAL_SPACE_MEMBERS 64

static struct local_space {
    struct local_space *members[NUM_LOCAL_SPACE_MEMBERS];
} _fake_local_space;

void init_maxine(void) {
  int i;
  for (i=0; i < NUM_LOCAL_SPACE_MEMBERS; ++i) {
    _fake_local_space.members[i] = &_fake_local_space;
  }
}

void *get_local_space(void) {
  return &_fake_local_space;
}
