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
  Extracted from mm.c.
 */

#ifndef _BITMAP_H_
#define _BITMAP_H_


#define ENTRIES_PER_MAPWORD (sizeof(unsigned long) * 8)

#define map_size_in_bytes(_s) \
(_s < 8 ? 1 : (_s + 8) / 8)

#define allocated_in_map(_map, _s) \
(_map[(_s)/ENTRIES_PER_MAPWORD] & (1UL<<((_s)&(ENTRIES_PER_MAPWORD-1))))

#define set_map(_map, _s) \
_map[(_s)/ENTRIES_PER_MAPWORD] |= (1UL<<((_s)&(ENTRIES_PER_MAPWORD-1)))

#define clear_map(_map, _s) \
_map[(_s)/ENTRIES_PER_MAPWORD] &= ~(1UL<<((_s)&(ENTRIES_PER_MAPWORD-1)))

#endif /* _BITMAP_H_ */
