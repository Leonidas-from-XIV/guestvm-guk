#ifndef _BUG_H_
#define _BUG_H_

#include <guk/console.h>

#if __x86_64__
static inline void **get_bp(void)
{
    void **bp;
    asm ("movq %%rbp, %0" : "=r"(bp));
    return bp;
}

static inline void **get_sp(void)
{
    void **sp;
    asm ("movq %%rsp, %0" : "=r"(sp));
    return sp;
}

#else
static inline void **get_bp(void)
{
    void **bp;
    asm ("movl %%ebp, %0" : "=r"(bp));
    return bp;
}

#endif

extern void guk_crash_exit(void);
extern void guk_crash_exit_backtrace(void);
extern void guk_crash_exit_msg(char *msg);
extern void guk_ok_exit(void);

#define crash_exit guk_crash_exit
#define crash_exit_backtrace guk_crash_exit_backtrace
#define ok_exit guk_ok_exit
#define crash_exit_msg guk_crash_exit_msg

#define BUG_ON(x) do { \
    if (x) {xprintk("BUG at %s:%d\n", __FILE__, __LINE__); crash_exit_backtrace(); } \
} while (0)

#define BUG() BUG_ON(1)

#endif
