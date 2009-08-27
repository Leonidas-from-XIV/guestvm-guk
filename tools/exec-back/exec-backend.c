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
 * Backend for Guest VM microkernel exec (xm create) support
 * Much of this code was taken from UNIXProcess_md.c in the Sun JDK
 *
 * Author: Mick Jordan, Sun Microsystems, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <wait.h>
#include <xenctrl.h>
#include <sys/select.h>
#include <xen/io/ring.h>
#include "exec-backend.h"

struct xs_handle *xsh = NULL;
static int export_id = 0;
int trace_level = 1;

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#define FAIL_FILENO (STDERR_FILENO + 1)
#define MAX_ACTIVE_REQUESTS 16

struct request *active_requests[MAX_ACTIVE_REQUESTS];
static int active_request_index = 0;

/* read/close request encode the fd in the request id */
static int exec_request_id(int request_id) {
  return (request_id / 3) * 3;
}

static int fd_from_request_id(int request_id) {
  return request_id % 3;
}

static struct request *find_request(int dom_id, int request_id) {
  int i;
  for (i = 0; i < MAX_ACTIVE_REQUESTS; i++) {
    struct request *r = active_requests[i];
    if (r != NULL && r->dom_id == dom_id && r->request_id == request_id) {
      return r;
    }
  }
  return NULL;
}

static void reap_requests(void) {
  char node[1024];
  int i;
  for (i = 0; i < MAX_ACTIVE_REQUESTS; i++) {
    struct request *r = active_requests[i];
    if (r != NULL) {
      /* reap any request for which the process has exited and all streams are closed */
      if (r->pid == -1 && r->stdio[0] == -1 && r->stdio[1] == -1 && r->stdio[2] == -1) {
	sprintf(node, REQUEST_NODE, r->dom_id, r->request_id);
	if (trace_level >= TRACE_OPS) {
	  printf("Reaping request %s\n", node);
	}
	/* the xs_rm causes another path changed event in await_connections which
	   we could handle by deferring the reap, but is it important? */
	//xs_rm(xsh, XBT_NULL, node);
	active_requests[i] = NULL;
      }
    }
  }
}

static int get_request_slot(void) {
  int i;
  for (i = 0; i < MAX_ACTIVE_REQUESTS; i++) {
    struct request *r = active_requests[i];
    if (r == NULL) {
      return i;
    }
  }
  return -1;
  
}

static void move_fd(int from_fd, int to_fd) {
  if (from_fd != to_fd) {
    dup2(from_fd, to_fd);
    close(from_fd);
  }
}

static int safe_close(int fd) {
  if (fd != -1) {
    return close(fd);
  }
  return 0;
}

/*
 * Reads nbyte bytes from file descriptor fd into buf,
 * The read operation is retried in case of EINTR or partial reads.
 *
 * Returns number of bytes read (normally nbyte, but may be less in
 * case of EOF).  In case of read errors, returns -1 and sets errno.
 */
static ssize_t
read_fully(int fd, void *buf, size_t nbyte)
{
    ssize_t remaining = nbyte;
    for (;;) {
        ssize_t n = read(fd, buf, remaining);
        if (n == 0) {
            return nbyte - remaining;
        } else if (n > 0) {
            remaining -= n;
            if (remaining <= 0)
                return nbyte;
            /* We were interrupted in the middle of reading the bytes.
             * Unlikely, but possible. */
            buf = (void *) (((char *)buf) + n);
        } else if (errno == EINTR) {
            /* Strange signals like SIGJVM1 are possible at any time.
             * See http://www.dreamsongs.com/WorseIsBetter.html */
        } else {
            return -1;
        }
    }
}

void write_status_id(struct request *request, char *kind, int request_id) {
  char node[1024];
  char kindstatus[32];
  sprintf(kindstatus, "%s%s", kind, "status");
  sprintf(node, FRONTEND_NODE, request->dom_id, request_id);
  xenbus_printf(xsh, XBT_NULL, node, kindstatus, "%d", request->status);
}

void write_status(struct request *request, char *kind) {
  write_status_id(request, kind, request->request_id);
}

void do_exec_request(struct request *request, int argc, char **argv, char *wdir) {
    int errnum;
    int pid = -1;
    int in[2], out[2], err[2], fail[2];
    
    in[0] = in[1] = out[0] = out[1] = err[0] = err[1] = fail[0] = fail[1] = -1;

    if ((pipe(in)   < 0) ||
	(pipe(out)  < 0) ||
	(pipe(err)  < 0) ||
	(pipe(fail) < 0)) {
	printf("pipe calls failed");
	goto fail;
    }

    pid = fork();
    if (pid < 0) {
      /* fork failed */
      errnum = errno;
      printf("Fork failed\n");
      goto fail;
    }

    if (pid == 0) {
      /* child */
      close(in[1]);
      move_fd(in[0], STDIN_FILENO);
      close(out[0]);
      move_fd(out[1], STDOUT_FILENO);
      close(err[0]);
      move_fd(err[1], STDERR_FILENO);
      close(fail[0]);
      move_fd(fail[1], FAIL_FILENO);
      

      if (wdir != NULL && chdir(wdir) < 0) {
	errnum = errno;
	printf("Failed to chdir to %s\n", wdir);
	goto execfail;
      }

      if (fcntl(FAIL_FILENO, F_SETFD, FD_CLOEXEC) == -1)
	goto execfail;

      execvp(argv[0], argv);
    execfail:
      errnum = errno;
      write(FAIL_FILENO, &errnum, sizeof(errnum));
      close(FAIL_FILENO);
      _exit(-1);
    }

    /* Parent process */
    request->pid = pid;
    close(fail[1]); fail[1] = -1;
  
    switch (read_fully(fail[0], &errnum, sizeof(errnum))) {
        case 0: break; /* Exec succeeded */
        case sizeof(errnum):
	    waitpid(pid, NULL, 0);
	    printf("Exec failed\n");
	    goto fail;
        default:
	    errnum = errno;
	    printf("Read failed\n");
	    goto fail;
    }

    request->status = 0;
    request->stdio[0] = in[1];
    request->stdio[1] = out[0];
    request->stdio[2] = err[0];
    if (trace_level >= TRACE_OPS) {
      printf("Exec fds: %u, %u, %u\n", request->stdio[0],  request->stdio[1], request->stdio[2]);
    }
    
finally:    
    /* Always clean up the child's side of the pipes */
    safe_close(in [0]);
    safe_close(out[1]);
    safe_close(err[1]);

    /* Always clean up fail descriptors */
    safe_close(fail[0]);
    safe_close(fail[1]);

    return;

fail:
    /* Clean up the parent's side of the pipes in case of failure only */
    safe_close(in [1]);
    safe_close(out[0]);
    safe_close(err[0]);
    request->status = -errnum;
    goto finally;

}

void *handle_exec_request(void *data) {
    struct request *request = (struct request *)data;
    char node[1024];
    char *argv[1024];
    char *frontend_node;
    char *wdir;
    char *prog = NULL;
    int argc;
    int i;

    if (trace_level >= TRACE_OPS) {
      printf("Exec: %d:%d\n", request->dom_id, request->request_id);
    }

    sprintf(node, REQUEST_NODE, request->dom_id, request->request_id);
    frontend_node = xs_read(xsh, XBT_NULL, node, NULL);
    sprintf(node, "%s/wdir", frontend_node);
    wdir = xs_read(xsh, XBT_NULL, node, NULL);
    if (strcmp(wdir, "") == 0) {
      wdir = NULL;
    }
    sprintf(node, "%s/argc", frontend_node);
    argc = atoi(xs_read(xsh, XBT_NULL, node, NULL));
    for (i = 0; i <= argc; i++) {
      char *arg;
      sprintf(node, "%s/args/%u", frontend_node, i);
      arg = xs_read(xsh, XBT_NULL, node, NULL);
      argv[i] = arg;
    }
    argv[argc + 1] = NULL;
	
    if (trace_level >= TRACE_OPS) {
      printf("wdir='%s', argc %u\n", wdir, argc);
      for (i = 0; i <= argc; i++) {
	printf("arg %u: %s\n", i, argv[i]);
      }
      
    }
    do_exec_request(request, argc, argv, wdir);
    /* communicate exec status to frontend */
    write_status(request, "exec");

    return NULL;
    // pthread_exit(NULL);
}
    
int wait_for_process_exit(struct request *request) {
    int status;
    /* Wait for the child process to exit.  This returns immediately if
       the child has already exited. */
    while (waitpid(request->pid, &status, 0) < 0) {
	switch (errno) {
	case ECHILD: return 0;
	case EINTR: break;
	default: return -1;
	}
    }

    if (WIFEXITED(status)) {
        /*
         * The child exited normally; get its exit code.
         */
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        /* The child exited because of a signal.
	 * The best value to return is 0x80 + signal number,
	 * because that is what all Unix shells do, and because
	 * it allows callers to distinguish between process exit and
	 * process death by signal.
         */
	return 0x80 + WTERMSIG(status);
    } else {
        /*
         * Unknown exit code; pass it through.
         */
	return status;
    }
}

static void handle_wait_request(struct request *request) {
    request->status = wait_for_process_exit(request);
    /* communicate wait status to frontend */
    write_status(request, "wait");
    /* mark exited */
    request->pid = -1;
}

static void handle_read_request(struct request *request, int request_id) {
    char node[1024];
    char *read_info;
    int fd, rfd, length, file_offset;
    char buffer[81];
    int result;

    /* the request_id encodes the exec_id and the fd */
    sprintf(node, READ_REQUEST_NODE, request->dom_id, request_id);
    read_info = xs_read(xsh, XBT_NULL, node, NULL);
    sscanf(read_info, "%u,%u", &length, &file_offset);
    fd = fd_from_request_id(request_id);
    rfd = request->stdio[fd];
    printf("Read %u[%u],%u,%u\n", fd, rfd, length, file_offset);
    /* we limit reads to max(length, 80) */
    if (length > 80) length = 80;
    result = read_fully(rfd, buffer, length);
    request->status = result < 0 ? -errno : 0;
    if (result >= 0) {
      buffer[result] = '\0';
      sprintf(node, FRONTEND_NODE, request->dom_id, request_id);
      xenbus_printf(xsh, XBT_NULL, node, "readbytes", "%s", buffer);
    }
    write_status_id(request, "read", request_id);
}

static void handle_close_request(struct request *request, int request_id) {
    char *close_info;
    int fd, rfd, result;
    fd = fd_from_request_id(request_id);
    rfd = request->stdio[fd];
    printf("Close %u[%u]\n", fd, rfd);
    result = safe_close(rfd);
    request->stdio[fd] = -1;
    request->status = result < 0 ? -errno : 0;
    write_status_id(request, "close", request_id);
}

static void handle_destroy_request(struct request *request) {
    char node[1024];
    int result;
    printf("Destroy %u\n", request->pid);
    kill(request->pid, SIGTERM);
    request->status = 0;
    write_status(request, "close");
}

static void write_bad_request(int dom_id, int request_id, char *rkind) {
   struct request *request;
  printf("Can't find request %d\n", request_id);
  request = (struct request*)malloc(sizeof(struct request));
  request->dom_id = dom_id;
  request->request_id = request_id;
  request->status = -EBADF;
  write_status(request, rkind);
  free(request);
}

static struct request *check_find_request(int dom_id, int request_id, char *rkind) {
  struct request *request = find_request(dom_id, request_id);
  if (request == NULL) {
    write_bad_request(dom_id, request_id, rkind);
    return NULL;
  }
  return request;
}

static void handle_connection(int dom_id, int request_id, char *rkind)
{
    struct request *request;
    pthread_t handling_thread;

    if (trace_level >= TRACE_OPS) printf("Handling '%s' request from dom_id=%d, request_id %d\n", rkind, dom_id, request_id);


    if (strcmp(rkind, "exec") == 0) {
      request = (struct request*)malloc(sizeof(struct request));
      request->dom_id = dom_id;
      request->request_id = request_id;
      request->slot = get_request_slot();
      if ( request->slot < 0) {
	printf("Too many outstanding requests\n");
	request->status = -EAGAIN;
	write_status(request, "exec");
	return;
      }
      active_requests[request->slot] = request;
      // pthread_create(&handling_thread, NULL, &handle_exec_request, request);
      handle_exec_request(request);
    } else if (strcmp(rkind, "wait") == 0) {
      request = check_find_request(dom_id, request_id, rkind);
      if (request == NULL) {
	return;
      }
      handle_wait_request(request);
      /* cant free yet, frontend calls wait immediately in a reaper thread */
      //free(request);
      //active_requests[request->slot] = NULL;
    } else if (strcmp(rkind, "read") == 0) {
      request = check_find_request(dom_id, exec_request_id(request_id), rkind);
      if (request == NULL) {
	return;
      }      
      handle_read_request(request, request_id);
    } else if (strcmp(rkind, "close") == 0) {
      request = check_find_request(dom_id, exec_request_id(request_id), rkind);
      if (request == NULL) {
	return;
      }
      handle_close_request(request, request_id);
    } else if (strcmp(rkind, "destroy") == 0) {
      request = check_find_request(dom_id, request_id, rkind);
      if (request == NULL) {
	return;
      }
      handle_destroy_request(request);
    } else {
      printf("Unknown request type %s\n", rkind);
    }
}

static void await_connections(void)
{
    int fd, ret, dom_id, request_id; 
    fd_set fds;
    char **watch_paths;
    char rkind[16];
    unsigned int len;

    assert(xsh != NULL);
    fd = xenbus_get_watch_fd(); 
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    /* Infinite watch loop */
    do {
        ret = select(fd+1, &fds, NULL, NULL, NULL);
        assert(ret == 1);
        watch_paths = xs_read_watch(xsh, &len);
        assert(len == 2);
        printf("watch_paths: [0] %s, [1] %s\n",  watch_paths[0],  watch_paths[1]);
        assert(strcmp(watch_paths[1], "conn-watch") == 0);
        if(strcmp(watch_paths[0], WATCH_NODE) == 0)
            goto next_select;
        dom_id = -1;
        if (trace_level >= TRACE_RING) printf("Path changed %s\n", watch_paths[0]);
        sscanf(watch_paths[0], REQUEST_NODE_TM, &dom_id, &request_id, &rkind[0]);
        if(dom_id >= 0) handle_connection(dom_id, request_id, rkind);
	reap_requests();
next_select:        
        printf("Awaiting next connection.\n");
        /* TODO - we need to figure out what to free */
        //free(watch_paths[0]);
        //free(watch_paths[1]);
    } while (1);
}

void test_exec(void) {
  char *argv[3];
  char buffer[1024];
  struct request *request = (struct request*)malloc(sizeof(struct request));

  printf("testing exec on echo\n");
  argv[0] = "echo";
  argv[1] = "exec-back exec ok!";
  argv[2] = NULL;
  request->dom_id = 0;
  request->request_id = 0;
  request->slot = get_request_slot();
  active_requests[request->slot] = request;
  do_exec_request(request, 2, argv, NULL);
  if (request->status == 0) {
    int nbytes = read_fully(request->stdio[1], buffer, 1023);
    buffer[nbytes] = '\0';
    printf("exec output: %s", buffer);
    wait_for_process_exit(request);
  }
  free(request);
  active_requests[request->slot] = NULL;
}

extern void xenbus_register_export(void);
int main(int argc, char*argv[])
{
  int i;
  if (argc > 1) sscanf(argv[1], "%d", &trace_level);

  for (i = 0; i < MAX_ACTIVE_REQUESTS; i++) {
    active_requests[i] == NULL;
  }
  
  if (trace_level >= TRACE_OPS) {
    test_exec();
  }

  /* Open the connection to XenStore first */
  xsh = xs_domain_open();
  assert(xsh != NULL);
  xs_rm(xsh, XBT_NULL, ROOT_NODE);
  /* Create watch node */
  xenbus_create_request_node();
    
  await_connections();
  /* Close the connection to XenStore when we are finished with everything */
  xs_daemon_close(xsh);
}
