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
#ifndef __XMALLOC_H__
#define __XMALLOC_H__

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
