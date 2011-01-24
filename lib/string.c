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
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: string.c
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: 
 *              
 *        Date: Aug 2003
 * 
 * Environment: Guest VM microkernel evolved from Xen Minimal OS
 * Description: Library function for string and memory manipulation
 *              Origin unknown
 *
 */

#if !defined HAVE_LIBC

#include <guk/os.h>
#include <guk/xmalloc.h>

#include <lib.h>
#include <types.h>

int memcmp(const void * cs,const void * ct,size_t count)
{
	const unsigned char *su1, *su2;
	signed char res = 0;

	for( su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
		if ((res = *su1 - *su2) != 0)
			break;
	return res;
}

void * memcpy(void * dest,const void *src,size_t count)
{
	char *tmp = (char *) dest;
    const char *s = src;

	while (count--)
		*tmp++ = *s++;

	return dest;
}

int strncmp(const char * cs,const char * ct,size_t count)
{
	register signed char __res = 0;

	while (count) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
		count--;
	}

	return __res;
}

int strcmp(const char * cs,const char * ct)
{
        register signed char __res;

        while (1) {
                if ((__res = *cs - *ct++) != 0 || !*cs++)
                        break;
        }

        return __res;
}

char * strcpy(char * dest,const char *src)
{
        char *tmp = dest;

        while ((*dest++ = *src++) != '\0')
                /* nothing */;
        return tmp;
}

char * strncpy(char * dest,const char *src,size_t count)
{
        char *tmp = dest;

        while (count-- && (*dest++ = *src++) != '\0')
                /* nothing */;

        return tmp;
}

void * memset(void * s,int c,size_t count)
{
        char *xs = (char *) s;

        while (count--)
                *xs++ = c;

        return s;
}

size_t strnlen(const char * s, size_t count)
{
        const char *sc;

        for (sc = s; count-- && *sc != '\0'; ++sc)
                /* nothing */;
        return sc - s;
}


char * strcat(char * dest, const char * src)
{
    char *tmp = dest;
    
    while (*dest)
        dest++;
    
    while ((*dest++ = *src++) != '\0');
    
    return tmp;
}

size_t strlen(const char * s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

char * strchr(const char * s, int c)
{
        for(; *s != (char) c; ++s)
                if (*s == '\0')
                        return NULL;
        return (char *)s;
}

char * strstr(const char * s1,const char * s2)
{
        int l1, l2;

        l2 = strlen(s2);
        if (!l2)
                return (char *) s1;
        l1 = strlen(s1);
        while (l1 >= l2) {
                l1--;
                if (!memcmp(s1,s2,l2))
                        return (char *) s1;
                s1++;
        }
        return NULL;
}

char *strdup(const char *x)
{
    int l = strlen(x);
    char *res = malloc(l + 1);
	if (!res) return NULL;
    memcpy(res, x, l + 1);
    return res;
}

#endif
