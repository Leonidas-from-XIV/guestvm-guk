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
 * Tracing support.
 * 
 * Author: Mick Jordan, Sun Microsystems, Inc.
 */

#include <lib.h>
#include <trace.h>
#include <spinlock.h>
#include <sched.h>

//static long base_time = 0;

#define TRACE_HYP_CONSOLE 1
#define TRACE_RING_CONSOLE 0

static int trace_buffering = 0;
static int trace_destination = TRACE_HYP_CONSOLE;
static int tracing = 0;

static DEFINE_SPINLOCK(trace_spinlock);

#define TRACE_VAR_IMPL(name) \
int trace_state_##name = 0; 

TRACE_VAR_IMPL(sched)
TRACE_VAR_IMPL(startup)
TRACE_VAR_IMPL(blk)
TRACE_VAR_IMPL(db_back)
TRACE_VAR_IMPL(fs_front)
TRACE_VAR_IMPL(gnttab)
TRACE_VAR_IMPL(mm)
TRACE_VAR_IMPL(mmpt)
TRACE_VAR_IMPL(net)
TRACE_VAR_IMPL(service)
TRACE_VAR_IMPL(smp)
TRACE_VAR_IMPL(xenbus)
TRACE_VAR_IMPL(traps)

#define TRACE_HEADER_SIZE 12

/*
 * For large quantities of tracing, buffering is essential and we try to
 * have minimal impact on the system and hold no locks.
 *
 * The buffer is broken up into segments of size TRACE_ELEMENT_SIZE.
 * Each guk_tprintk call atomically claims a segment and writes the trace to it.
 * The first byte of the segment contains the length of the trace and
 * is used when flushing the segments. vsnprintf is used to format the trace,
 * which always places a null byte at the end, even if the trace overflows the buffer.
 * I.e., Only TRACE_ELEMENT_SIZE - 2 bytes are available for the actual trace content.
 * In the case of overflow, vsnprintf returns the number of bytes that would
 * have been written had the buffer segment been large enough. We check this
 * case and make sure that the segment terminates with a newline, and record
 * that the truncation happened.
 */
#define TRACE_ELEMENT_SIZE 82
#define TRACE_BUFFERSIZE (TRACE_ELEMENT_SIZE * 10000)


static char trace_buffer[TRACE_BUFFERSIZE];
static char* trace_buffer_ptr = &trace_buffer[0];
static char* trace_buffer_limit = &trace_buffer[0] + TRACE_BUFFERSIZE;
static int wrap_count = 0;
static int truncate_count = 0;
static int flushing = 0;

/* We try to atomically store the address of the next trace record (rntbp) in trace_buffer_ptr
 * while checking that the stored value matched the one we initially read (rtbp), using cmpxchg.
 * If the cmpxchg fails, another trace got there first and updated the value, so we try again
 * (conveniently the cmpxchg puts the updated value in rax for us).
 * We check for trace buffer overflow, wrapping back to the beginning in that case.
 */
#define cmpxchg_loop \
    "1:\n\t" \
    "movq %[rtbp], %[rntbp]\n\t" \
    "addq %[tes], %[rntbp]\n\t" \
    "cmpq %[tbl], %[rntbp]\n\t" \
    "jb 2f\n\t" \
    "movq %[tb], %[rntbp]\n\t" \
    "incl %[wc]\n\t" \
    "2:\n\t" \
    "lock; cmpxchg %[rntbp], %[tbp]\n\t" \
    "jnz 1b\n"

void guk_tprintk(const char *fmt, ...) {
  if (!tracing || flushing) return;
  va_list       args;
  va_start(args, fmt);
  if (trace_buffering) {
    int n;
    register char *rtbp asm ("rax") = trace_buffer_ptr;
    register char *rntbp = NULL; /* rtbp + TRACE_ELEMENT_SIZE; */
    __asm__ __volatile__(
      cmpxchg_loop : [tbp] "=m" (trace_buffer_ptr)
      : [rtbp] "r" (rtbp), [rntbp] "r" (rntbp), [tes] "i" (TRACE_ELEMENT_SIZE),
        [tbl] "m" (trace_buffer_limit), [tb] "r" (trace_buffer), [wc] "m" (wrap_count)
      : "memory", "cc");
    char *tbp = rtbp;
    n = vsnprintf(rtbp + 1, TRACE_ELEMENT_SIZE - 1, fmt, args);
    if (n >= TRACE_ELEMENT_SIZE - 1) {
      /* overflow */
      tbp[TRACE_ELEMENT_SIZE - 2] = '\n';
      n = TRACE_ELEMENT_SIZE - 2;
      truncate_count++;
    }
    *tbp = (char) n;
  } else {
    /* It is important to disable events as well as grabbing the lock, otherwise
       an event can trigger a recursive call (i.e. when tracing the scheduler),
       which results in a stuck spinlock.
    */
    unsigned long flags;
    spin_lock_irqsave(&trace_spinlock, flags);
    cprintk(trace_destination, fmt, args);
    spin_unlock_irqrestore(&trace_spinlock, flags);
  }
  va_end(args);
}

void flush_trace_buffer(void) {
  char *p = &trace_buffer[0];
  flushing = 1;
  while (p < trace_buffer_ptr) {
    int len = *p;
    if (trace_destination == TRACE_RING_CONSOLE) {
      printbytes(p + 1, len);
    } else {
      (void)HYPERVISOR_console_io(CONSOLEIO_write, len, p + 1);
    }
    p += TRACE_ELEMENT_SIZE;
  }
}

void flush_trace(void) {
  if (trace_buffering) {
    tprintk("Trace: wrap count %d, truncate count %d, buffer used %d\n", wrap_count,
	    truncate_count, trace_buffer_ptr - &trace_buffer[0]);
    flush_trace_buffer();
  }
}

void init_trace(char *cmd_line) {
  while (*cmd_line != 0) {
    char *trace_arg = strstr(cmd_line, TRACE_HEADER);
    if (trace_arg == NULL) break;
    tracing = 1;
    trace_arg += TRACE_HEADER_SIZE;
    if (*trace_arg == ':') {
      do {
        trace_arg++;
        char subarg[32];
        int s = 0;
        while (*trace_arg != ' ' && *trace_arg != ':' && *trace_arg != 0 && s < 30) {
 	  subarg[s++] = *trace_arg++;
        }
        subarg[s] = '\0';
        if (strcmp(subarg, "startup") == 0) trace_state_startup = 1;
        else if (strcmp(subarg, "sched") == 0) trace_state_sched = 1;
        else if (strcmp(subarg, "blk") == 0) trace_state_blk = 1;
        else if (strcmp(subarg, "net") == 0) trace_state_net = 1;
        else if (strcmp(subarg, "dbback") == 0) trace_state_db_back = 1;
        else if (strcmp(subarg, "fsfront") == 0) trace_state_fs_front = 1;
        else if (strcmp(subarg, "gnttab") == 0) trace_state_gnttab = 1;
        else if (strcmp(subarg, "mm") == 0) trace_state_mm = 1;
        else if (strcmp(subarg, "mmpt") == 0) trace_state_mmpt = 1;
        else if (strcmp(subarg, "service") == 0) trace_state_service = 1;
        else if (strcmp(subarg, "smp") == 0) trace_state_smp = 1;
        else if (strcmp(subarg, "xenbus") == 0) trace_state_xenbus = 1;
        else if (strcmp(subarg, "traps") == 0) trace_state_traps = 1;
        else if (strcmp(subarg, "buffer") == 0) trace_buffering = 1;
        else if (strcmp(subarg, "toring") == 0) trace_destination = TRACE_RING_CONSOLE;
        else xprintk("GUKTrace:%s invalid\n", subarg);
      } while (*trace_arg == ':');
    } else {
      break;
    }
    cmd_line = trace_arg;
  }
  if (tracing) ttprintk("IT %lx\n", &trace_spinlock);
}

int guk_set_trace_state(int trace_var_ord, int value) {
  int previous = 0;
  switch (trace_var_ord) {
  case 0: previous = trace_state_sched; trace_state_sched = value; break;
  case 1: previous = trace_state_startup; trace_state_startup = value; break;
  case 2: previous = trace_state_blk; trace_state_blk = value; break;
  case 3: previous = trace_state_db_back; trace_state_db_back = value; break;
  case 4: previous = trace_state_fs_front; trace_state_fs_front = value; break;
  case 5: previous = trace_state_gnttab; trace_state_gnttab = value; break;
  case 6: previous = trace_state_mm; trace_state_mm = value; break;
  case 7: previous = trace_state_mmpt; trace_state_mmpt = value; break;
  case 8: previous = trace_state_net; trace_state_net = value; break;
  case 9: previous = trace_state_service; trace_state_service = value; break;
  case 10: previous = trace_state_smp; trace_state_smp = value; break;
  case 11: previous = trace_state_xenbus; trace_state_xenbus = value; break;
  case 12: previous = trace_state_traps; trace_state_traps = value; break;
  }
  return previous;
}

int guk_get_trace_state(int trace_var_ord) {
  switch (trace_var_ord) {
  case 0: return trace_state_sched;
  case 1: return trace_state_startup;
  case 2: return trace_state_blk;
  case 3: return trace_state_db_back;
  case 4: return trace_state_fs_front;
  case 5: return trace_state_gnttab;
  case 6: return trace_state_mm;
  case 7: return trace_state_mmpt;
  case 8: return trace_state_net;
  case 9: return trace_state_service;
  case 10: return trace_state_smp;
  case 11: return trace_state_xenbus;
  case 12: return trace_state_traps;
  default: return 0;
  }
}

