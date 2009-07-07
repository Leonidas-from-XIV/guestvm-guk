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
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: lib.h
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: 
 *              
 *        Date: Aug 2003
 * 
 * Environment: GuestVM microkernel evolved from Xen Minimal OS
 * Description: Random useful library functions, contains some freebsd stuff
 *
 *
 *-
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _LIB_H_
#define _LIB_H_

#include <stdarg.h>
#include <stddef.h>
#include <console.h>

/* printing */
#define _p(_x) ((void *)(unsigned long)(_x))
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char * buf, size_t size, const char *fmt, ...);
int scnprintf(char * buf, size_t size, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list args);
int sprintf(char * buf, const char *fmt, ...);
int vsscanf(const char * buf, const char * fmt, va_list args);
int sscanf(const char * buf, const char * fmt, ...);

long simple_strtol(const char *cp,char **endp,unsigned int base);
unsigned long simple_strtoul(const char *cp,char **endp,unsigned int base);
long long simple_strtoll(const char *cp,char **endp,unsigned int base);
unsigned long long simple_strtoull(const char *cp,char **endp,unsigned int base);


/* string and memory manipulation */
int    memcmp(const void *cs, const void *ct, size_t count);
void  *memcpy(void *dest, const void *src, size_t count);
int    strncmp(const char *cs, const char *ct, size_t count);
int    strcmp(const char *cs, const char *ct);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t count);
void  *memset(void *s,int c, size_t count);
size_t strnlen(const char *s, size_t count);
size_t strlen(const char *s);
char  *strchr(const char *s, int c);
char  *strstr(const char *s1, const char *s2);
char * strcat(char * dest, const char * src);
char  *strdup(const char *s);


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct kvec {
    void *iov_base;
    size_t iov_len;
};

#define ASSERT(x)                                              \
do {                                                           \
	if (!(x)) {                                                \
		printk("ASSERTION FAILED: %s at %s:%d.\n",             \
			   # x ,                                           \
			   __FILE__,                                       \
			   __LINE__);                                      \
        BUG();                                                 \
	}                                                          \
} while(0)

/* Consistency check as much as possible. */
void sanity_check(void);

#endif /* _LIB_H_ */
