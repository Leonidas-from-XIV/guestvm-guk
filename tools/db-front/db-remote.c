/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include "gx.h"

int gx_remote_dbg;          /* enable debug trace output for debugging */

static int process_remote_request(char *remote_buf) {
}

/*
 * usage: port
 */
int main(int argc, char **argv) {
        #define BUFSIZE 4096
        char *remote_buf;
        int exit_rc = 0;

	remote_buf = malloc(BUFSIZE);
	if (remote_buf == NULL) {
	  printf("failed to allocate communication buffer\n");
	  exit(3);
	}

        if (gx_remote_open(argv[0]) == -1) {
                return 1;
        }

        /* we've a connection at this point, process requests */
        while(gx_getpkt(remote_buf) > 0) {
                if ((exit_rc=process_remote_request(remote_buf)))
                        break;
                if (gx_putpkt(remote_buf) == -1) {
                        exit_rc = 1;
                        break;
                }
        }
        /* unpause and let the guest continue */
        if (exit_rc == 0) {
                printf("Exiting.. Remote side has terminated connection\n");
        }
        gx_remote_close();
        return exit_rc;
    
}

