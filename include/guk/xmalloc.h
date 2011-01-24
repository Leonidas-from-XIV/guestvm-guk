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
#ifndef __XMALLOC_H__
#define __XMALLOC_H__

#include <lib.h>

/* Allocate space for typed object. */
#define xmalloc(_type) ((_type *)guk_xmalloc(sizeof(_type), __alignof__(_type)))

/* Allocate space for array of typed objects. */
#define xmalloc_array(_type, _num) ((_type *)guk_xmalloc_array(sizeof(_type), __alignof__(_type), _num))

/* Free any of the above. */
extern void guk_xfree(const void *);

/* Underlying functions */
extern void *guk_xmalloc(size_t size, size_t align);
extern void *guk_xrealloc(const void *, size_t size, size_t align);
static inline void *guk_xmalloc_array(size_t size, size_t align, size_t num)
{
	/* Check for overflow. */
	if (size && num > UINT_MAX / size)
		return NULL;
 	return guk_xmalloc(size * num, align);
}

#define malloc(size) guk_xmalloc(size, 4)
#define realloc(ptr, size) guk_xrealloc(ptr, size, 4)
#define free(ptr) guk_xfree(ptr)

#endif /* __XMALLOC_H__ */
