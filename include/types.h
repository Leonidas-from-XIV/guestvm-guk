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
#undef _LIBC_LIMITS_H_

/* FreeBSD compat types */
typedef unsigned char       u_char;
typedef unsigned int        u_int;
typedef unsigned long       u_long;
typedef long                quad_t;
typedef unsigned long       u_quad_t;
typedef unsigned long       uintptr_t;
typedef long int            intptr_t;

typedef struct { unsigned long pte; } pte_t;

#define __int8_t_defined
typedef  unsigned char uint8_t;
typedef  char int8_t;
typedef  unsigned short uint16_t;
typedef  short int16_t;
typedef  unsigned int uint32_t;
typedef  int int32_t;
typedef  unsigned long uint64_t;
typedef  long int64_t;

// do not define u8 since it is used in hotspot for julong types
typedef  uint16_t u16;
typedef  int16_t  s16;
typedef  uint32_t u32;
typedef  int32_t  s32;
typedef  uint64_t u64;
typedef  int64_t  s64;

typedef signed long     ssize_t;
#define _SYS_INT_TYPES_H

#endif /* _TYPES_H_ */
