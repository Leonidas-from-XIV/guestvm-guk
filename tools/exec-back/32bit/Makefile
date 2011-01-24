#
# Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#
ifndef XEN_ROOT 
  $(error "Must set XEN_ROOT environment variable to the root of Xen tree")
endif

OSNAME ?= $(shell uname -s)
ifneq ($(OSNAME),Linux)
  $(error "exec-back can only be built and installed in a Linux guest")
endif

OSARCH ?= $(shell arch)
ifneq ($(OSARCH),x86_64)
  $(error "exec-back must be compiled on a 64-bit system")
endif

ifndef XEN_LIBS32
  $(error "Must set XEN_LIBS32 to directory containing 32 bit versions of libxenstore...")
endif

include $(XEN_ROOT)/tools/Rules.mk

INCLUDES += -I.

IBIN         = exec-backend 
INST_DIR     = /usr/sbin

CFLAGS   = -m32
CFLAGS   += -Werror
CFLAGS   += -Wno-unused
CFLAGS   += -fno-strict-aliasing
CFLAGS   +=  -I $(XEN_ROOT)/tools/include -I $(XEN_LIBXC) -I $(XEN_XENSTORE)
CFLAGS   += -I.. $(INCLUDES)
CFLAGS   += -D_GNU_SOURCE

# Get gcc to generate the dependencies for us.
CFLAGS   += -Wp,-MD,.$(@F).d
DEPS      = .*.d

LIBS      := -L. -L$(XEN_LIBS32) -lxenctrl -lxenstore -lpthread -lrt 

OBJS	  := exec-xenbus.o

all: links $(IBIN)

.PHONY: links
links:	$(ARCH_LINKS)
	[ -e xen ] || ln -sf $(XEN_ROOT)/xen/include/public xen
	[ -e exec-xenbus.c ] || ln -sf ../exec-xenbus.c
	[ -e exec-backend.c ] || ln -sf ../exec-backend.c

exec-backend: $(OBJS) exec-backend.c
	$(CC) $(CFLAGS) -o exec-backend $(OBJS) $(LIBS) exec-backend.c

install: all
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(INST_DIR)

clean:
	rm -rf *.o *~ $(DEPS) xen $(IBIN) $(LIB)

.PHONY: clean install

-include $(DEPS)
