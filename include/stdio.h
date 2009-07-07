
#ifndef GUK_STDIO_H
#define GUK_STDIO_H
#define _LIBC_LIMITS_H_

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#include <limits.h>
#include <float.h>

/* fake FILE type */
typedef void *FILE;
#define _NFILE 60

#undef _LIBC_LIMITS_H_
#endif
