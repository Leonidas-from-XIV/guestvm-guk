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
 *
 * Frontend for exec (remote process/guest creation) 
 *
 * Author: Mick Jordan, Sun Microsystems, Inc..
 */

#include <guk/fs.h>
#include <guk/os.h>
#include <guk/init.h>
#include <guk/service.h>
#include <guk/sched.h>
#include <guk/arch_sched.h>
#include <guk/xmalloc.h>
#include <guk/xenbus.h>
#include <guk/gnttab.h>
#include <guk/events.h>
#include <guk/trace.h>
#include <errno.h>


#ifdef EXEC_DEBUG
#define DEBUG(_f, _a...) \
    printk("GUK(file=exec-front.c, line=%d) " _f "\n", __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

static int exec_init = 0;
static domid_t self_id = 0;
static int exec_id = 0;

static int check(char *err) {
  if (err) {
    printk("xenbus operation failed: (%s)\n");
    free(err);
    return 1;
  } else {
    return 0;
  }
}

#define EXEC_STATUS 0
#define WAIT_STATUS 1
#define READ_STATUS 2
#define WRITE_STATUS 3
#define CLOSE_STATUS 4
#define DESTROY_STATUS 5
#define WAIT_PERIOD 10   /* Wait period in ms */
#define MAX_WAIT    50   /* Max number of WAIT_PERIODs */

static char* status_strings[] = {"execstatus", "waitstatus", "readstatus", "writestatus",
                                 "closestatus", "destroystatus"};

static int wait_for_status(int this_exec_id, int statuskind) {
  char *err, *status;
  char nodename[1024];
  int result = -EAGAIN;
  int retry = MAX_WAIT;
  char *statuskindname = status_strings[statuskind];
  /* This string must be unique to this operation */
  sprintf(nodename, "/local/domain/%d/device/exec/%d/%s", self_id, this_exec_id, statuskindname);
  while (retry > 0) {
    err = xenbus_read(XBT_NIL, nodename, &status);
    if (err) {
      free(err);
      sleep(WAIT_PERIOD);
      if (statuskind == EXEC_STATUS) retry--;
      continue;
    }
    sscanf(status, "%d", &result);
    break;
  }
  if (retry == 0) {
    printk("failed to xenbus_read %s\n", nodename);
  }
  err = xenbus_rm(XBT_NIL, nodename);
  BUG_ON(err);
  return result;
}

/* Asks exec-backend to execute "prog" with given args in directory "dir".
   Return exec_id if exec succeeded, -errno otherwise.
   Remote file descriptors written to fds[0..2]
 */
int guk_exec_create(char *prog, char *arg_block, int argc, char *dir, int *fds) {
    char *err;
    xenbus_transaction_t xbt;
    int retry = 0;
    char nodename[1024];
    char a_nodename[1024];
    char r_nodename[1024];
    char argn[4];
    int i;
    int result;
    int this_exec_id = exec_id;
     
    if (!exec_init) {
      return -ENODEV;
    }

    if (argc > 999) {
      return -E2BIG;
    }

    /* exec_id .. exec_id + 2 is used to encode the file descriptor for read */
    exec_id += 3;

    sprintf(nodename, "/local/domain/%d/device/exec/%d", self_id, this_exec_id);
    sprintf(a_nodename, "/local/domain/%d/device/exec/%d/args", self_id, this_exec_id);

again:
    err = xenbus_transaction_start(&xbt);
    if (check(err)) goto abort_transaction;

    err = xenbus_printf(xbt, nodename, "wdir", "%s", dir == NULL ? "": dir);
    if (check(err)) goto abort_transaction;

    err = xenbus_printf(xbt, nodename, "argc", "%u", argc);
    if (check(err)) goto abort_transaction;

    err = xenbus_printf(xbt, a_nodename, "0", "%s", prog);
    if (check(err)) goto abort_transaction;

    argn[0] = '1'; argn[1] = '\0';
    for (i = 1; i <= argc; i++) {
      sprintf(argn, "%d", i);
      err = xenbus_printf(xbt, a_nodename, argn, "%s", arg_block);
      if (check(err)) goto abort_transaction;
      while (*arg_block) arg_block++;
      arg_block++;
    }
    check(xenbus_transaction_end(xbt, 0, &retry));
    if (retry) {
      goto again;
    }
    /* Now write our request */
    sprintf(r_nodename,
            "/local/domain/0/backend/exec/requests/%d/%d/exec",
            self_id, this_exec_id);
    err = xenbus_write(XBT_NIL, r_nodename, nodename);
    if (err) {
      free(err);
      return -EIO;
    }
    goto done;

abort_transaction:
    check(xenbus_transaction_end(xbt, 1, &retry));
    return -EIO;

done:
    /* now wait for exec status */
    result = wait_for_status(this_exec_id, EXEC_STATUS);
    return result >= 0 ? this_exec_id : result;
}

int guk_exec_wait(int this_exec_id) {
  char nodename[1024];
  char *err;
  int result;
  sprintf(nodename, "/local/domain/0/backend/exec/requests/%d/%d/wait", self_id, this_exec_id);
  err = xenbus_write(XBT_NIL, nodename, "");
  if (err) {
    free(err);
    return -EIO;
  }
  result = wait_for_status(this_exec_id, WAIT_STATUS);
  return result;
}

void guk_exec_destroy(int this_exec_id) {
  char nodename[1024];
  char *err;
  sprintf(nodename, "/local/domain/0/backend/exec/requests/%d/%d/destroy", self_id, this_exec_id);
  err = xenbus_write(XBT_NIL, nodename, "");
  if (err) {
    free(err);
  }
  wait_for_status(this_exec_id, DESTROY_STATUS);
}

/* this_exec_id_fd encodes both the exec_id and the file descriptor we are reading on.
 */
int guk_exec_read_bytes(int this_exec_id_fd, char *buffer, int length, long file_offset) {
  char nodename[1024];
  char *err, *bytes;
  int status, result;
  sprintf(nodename, "/local/domain/0/backend/exec/requests/%d/%d", self_id, this_exec_id_fd);
  err = xenbus_printf(XBT_NIL, nodename, "read", "%u,%u", length, file_offset);
  if (err) {
    free(err);
    return -EIO;
  }
  status = wait_for_status(this_exec_id_fd, READ_STATUS);
  if (status < 0) {
    return status;
  }
  sprintf(nodename, "/local/domain/%d/device/exec/%d/%s", self_id, this_exec_id_fd, "readbytes");
  err = xenbus_read(XBT_NIL, nodename, &bytes);
  if (err) {
    printk("xenbus_read %s returned err %s\n", nodename, err);
    free(err);
    return -EIO;
  }
  err = xenbus_rm(XBT_NIL, nodename);
  BUG_ON(err);
  result = strlen(bytes);
  strcpy(buffer, bytes);
  return result;
}

/* this_exec_id_fd encodes both the exec_id and the file descriptor we are reading on.
 */
int guk_exec_write_bytes(int this_exec_id_fd, char *buffer, int length, long file_offset) {
  char nodename[1024];
  char *err;
  int status;
  char zbuffer[1024];
  BUG_ON(length > 1023);
  strncpy(zbuffer, buffer, length);
  zbuffer[length] = 0;
  sprintf(nodename, "/local/domain/0/backend/exec/requests/%d/%d", self_id, this_exec_id_fd);
  err = xenbus_printf(XBT_NIL, nodename, "write", "%u,%u,%s", length, file_offset, zbuffer);
  if (err) {
    free(err);
    return -EIO;
  }
  status = wait_for_status(this_exec_id_fd, WRITE_STATUS);
  if (status < 0) {
    return status;
  }
  return length;
}

/* this_exec_id_fd encodes both the exec_id and the file descriptor we closing.
 */
int guk_exec_close(int this_exec_id_fd) {
  char nodename[1024];
  char *err;
  int result;
  sprintf(nodename, "/local/domain/0/backend/exec/requests/%d/%d/close", self_id, this_exec_id_fd);
  err = xenbus_write(XBT_NIL, nodename, "");
  if (err) {
    free(err);
    return -EIO;
  }
  
  result = wait_for_status(this_exec_id_fd, CLOSE_STATUS);
  return result;
}

static int exec_suspend(void)
{
    if(exec_init != 1)
	return 0;

    xprintk("WARNING: exec suspend not supported\n");
    return 1;
}

static int exec_resume(void)
{
    if(exec_init != 1)
	return 0;

    xprintk("WARNING: exec resume not supported\n");
    return 1;
}

static int exec_shutdown(void)
{
    if(exec_init != 1)
	return 0;

    return 1;
}

static void exec_thread(void *cmd_line)
{
    char *value;
    char *ret = guk_xenbus_read(XBT_NIL, "/local/domain/0/backend/exec", &value);
    if (!value) {
      printk("no exec backend found: %s\n", ret);
    } else {
      self_id = xenbus_get_self_id();
      exec_init = 1;
    }
}

static int start_exec_thread(void *args)
{
    create_thread("exec-frontend", exec_thread, UKERNEL_FLAG, args);
    return 0;
}

static struct service exec_service = {
    .name = "exec service",

    .init = start_exec_thread,
    .suspend = exec_suspend,
    .resume = exec_resume,
    .shutdown = exec_shutdown,
    .arg = "",
};

USED static int init_func(void)
{
	register_service(&exec_service);
	return 0;
}
DECLARE_INIT(init_func);

