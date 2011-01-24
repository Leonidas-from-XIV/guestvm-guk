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
#
# The standard target of this build is a 64-bit library that will be
# linked with the Maxine Inspector.
#
# The build also produces a 32-bit library that can run in a 32-bit dom0
# and an executable 32-bit program, db-frontend, that accepts a TCP connection
# from a 64-bit remote system (running the Maxine Inspector).
#
ifndef XEN_ROOT 
  $(error "Must set XEN_ROOT environment variable to the root of Xen tree")
endif

OSNAME ?= $(shell uname -s)
ifneq ($(OSNAME),Linux)
  $(error "db-front can only be built and installed in a Linux guest")
endif

OSARCH ?= $(shell arch)
ifneq ($(OSARCH),x86_64)
  $(error "db-front must be compiled on a 64-bit system")
endif

include $(XEN_ROOT)/tools/Rules.mk

INCLUDES += -I.

IBIN         = db-frontend
LIB          = libguk_db.so

MAJOR 		= 1.0
MINOR 		= 0

CFLAGS   += -Werror
CFLAGS   += -Wno-unused
CFLAGS   += -fno-strict-aliasing
CFLAGS   += -I $(XEN_ROOT)/tools/include -I $(XEN_LIBXC) -I $(XEN_XENSTORE)
CFLAGS   += $(INCLUDES)
CFLAGS   += -D_GNU_SOURCE

# Get gcc to generate the dependencies for us.
CFLAGS   += -Wp,-MD,.$(@F).d
DEPS      = .*.d

LIBS      := -L.
LIBS      += -L$(XEN_LIBXC)
LIBS      += -lxenctrl 
LIBS      += -L$(XEN_XENSTORE) -lxenstore

OBJS	  := db-xenbus.o
PIC_OBJS  := db-xenbus.opic db-frontend.opic

SUBDIRS := 32bit

all: links $(LIB) $(LIB).$(MAJOR) $(LIB).$(MAJOR).$(MINOR)

.PHONY: links
links:	$(ARCH_LINKS)
	[ -e xen ] || ln -sf $(XEN_ROOT)/xen/include/public xen

$(LIB): $(LIB).$(MAJOR)
	ln -sf $< $@

$(LIB).$(MAJOR): $(LIB).$(MAJOR).$(MINOR)
	ln -sf $< $@

$(LIB).$(MAJOR).$(MINOR): $(PIC_OBJS)
	$(CC) $(CFLAGS) $(LIBS) -Wl,$(SONAME_LDFLAG) -Wl,$(LIB).$(MAJOR) $(SHLIB_CFLAGS) -o $@ $^ 


install: all
	$(INSTALL_PROG) $(LIB).$(MAJOR).$(MINOR) $(DESTDIR)/usr/$(LIBDIR)
	ln -sf $(LIB).$(MAJOR).$(MINOR) $(DESTDIR)/usr/$(LIBDIR)/$(LIB).$(MAJOR)
	ln -sf $(LIB).$(MAJOR) $(DESTDIR)/usr/$(LIBDIR)/$(LIB)

clean:
	for dir in $(SUBDIRS); do \
	    (cd $$dir; make clean); \
	done
	rm -rf *.o *.opic *~ $(DEPS) xen $(LIB)*

.PHONY: clean install

-include $(DEPS)
