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
