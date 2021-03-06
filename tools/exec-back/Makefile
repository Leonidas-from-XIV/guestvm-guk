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
# x86 architecture specific makefiles.
# x86_64 only currently.
#
ifndef XEN_ROOT 
  $(error "Must set XEN_ROOT environment variable to the root of Xen tree")
endif

include $(XEN_ROOT)/tools/Rules.mk

INCLUDES += -I.

IBIN         = exec-backend 
INST_DIR     = /usr/sbin

CFLAGS   += -Werror
CFLAGS   += -Wno-unused
CFLAGS   += -fno-strict-aliasing
CFLAGS   += -I $(XEN_LIBXC) -I $(XEN_XENSTORE)
CFLAGS   += $(INCLUDES)
CFLAGS   += -D_GNU_SOURCE

# Get gcc to generate the dependencies for us.
CFLAGS   += -Wp,-MD,.$(@F).d
DEPS      = .*.d

LIBS      := -L.
LIBS      += -L$(XEN_LIBXC)
LIBS      += -lxenctrl -lpthread -lrt 
LIBS      += -L$(XEN_XENSTORE) -lxenstore

OBJS	  := exec-xenbus.o

all: links $(IBIN)

.PHONY: links
links:	$(ARCH_LINKS)
	[ -e xen ] || ln -sf $(XEN_ROOT)/xen/include/public xen

exec-backend: $(OBJS) exec-backend.c
	$(CC) $(CFLAGS) -o exec-backend $(OBJS) $(LIBS) exec-backend.c

install: all
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(INST_DIR)

clean:
	rm -rf *.o *~ $(DEPS) xen $(IBIN) $(LIB)

.PHONY: clean install

-include $(DEPS)
