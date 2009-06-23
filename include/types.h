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
 /****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: types.h
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: 
 *              
 *        Date: May 2003
 * 
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: a random collection of type definitions
 *
 */

#ifndef _TYPES_H_
#define _TYPES_H_
#define _LIBC_LIMITS_H_
#include <limits.h>

typedef signed char         s8;
typedef unsigned char       u8;
typedef signed short        s16;
typedef unsigned short      u16;
typedef signed int          s32;
typedef unsigned int        u32;
#ifdef __i386__
typedef signed long long    s64;
typedef unsigned long long  u64;
#elif defined(__x86_64__) || defined(__ia64__)
typedef signed long         s64;
typedef unsigned long       u64;
#endif

/* FreeBSD compat types */
typedef unsigned char       u_char;
typedef unsigned int        u_int;
typedef unsigned long       u_long;
#ifdef __i386__
typedef long long           quad_t;
typedef unsigned long long  u_quad_t;
typedef unsigned int        uintptr_t;

#if !defined(CONFIG_X86_PAE)
typedef struct { unsigned long pte_low; } pte_t;
#else
typedef struct { unsigned long pte_low, pte_high; } pte_t;
#endif /* CONFIG_X86_PAE */

#elif defined(__x86_64__) || defined(__ia64__)
typedef long                quad_t;
typedef unsigned long       u_quad_t;
typedef unsigned long       uintptr_t;

typedef struct { unsigned long pte; } pte_t;
#endif /* __i386__ || __x86_64__ */

typedef  u8 uint8_t;
typedef  s8 int8_t;
typedef u16 uint16_t;
typedef s16 int16_t;
typedef u32 uint32_t;
typedef s32 int32_t;
typedef u64 uint64_t;
typedef s64 int64_t;


typedef signed long     ssize_t;

#endif /* _TYPES_H_ */
