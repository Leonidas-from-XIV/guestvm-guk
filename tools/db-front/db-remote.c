/*
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
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

