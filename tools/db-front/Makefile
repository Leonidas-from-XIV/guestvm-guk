#
# Copyright (c) 2009 Sun Microsystems, Inc., 4150 Network Circle, Santa
# Clara, California 95054, U.S.A. All rights reserved.
# 
# U.S. Government Rights - Commercial software. Government users are
# subject to the Sun Microsystems, Inc. standard license agreement and
# applicable provisions of the FAR and its supplements.
# 
# Use is subject to license terms.
# 
# This distribution may include materials developed by third parties.
# 
# Parts of the product may be derived from Berkeley BSD systems,
# licensed from the University of California. UNIX is a registered
# trademark in the U.S.  and in other countries, exclusively licensed
# through X/Open Company, Ltd.
# 
# Sun, Sun Microsystems, the Sun logo and Java are trademarks or
# registered trademarks of Sun Microsystems, Inc. in the U.S. and other
# countries.
# 
# This product is covered and controlled by U.S. Export Control laws and
# may be subject to the export or import laws in other
# countries. Nuclear, missile, chemical biological weapons or nuclear
# maritime end uses or end users, whether direct or indirect, are
# strictly prohibited. Export or reexport to countries subject to
# U.S. embargo or to entities identified on U.S. export exclusion lists,
# including, but not limited to, the denied persons and specially
# designated nationals lists is strictly prohibited.
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
OSARCH ?= $(shell arch)
ifneq ($(OSNAME),Linux)
  $(error "db-front can only be built and installed in a Linux guest")
endif
ifneq ($(OSARCH),x86_64)
  $(error "db-front must be compiled on a 64-bit system")
endif

include $(XEN_ROOT)/tools/Rules.mk

INCLUDES += -I.

IBIN32       = db-frontend
LIB          = libguk_db.so

MAJOR 		= 1.0
MINOR 		= 0

CFLAGS     += -Werror -Wno-unused -fno-strict-aliasing -I $(XEN_INCLUDE) -I $(XEN_LIBXC) -I $(XEN_XENSTORE) $(INCLUDES) -D_GNU_SOURCE
CFLAGS32   += -m32 -Werror -Wno-unused -fno-strict-aliasing -I $(XEN_INCLUDE) -I $(XEN_LIBXC) -I $(XEN_XENSTORE) $(INCLUDES) -D_GNU_SOURCE

# Get gcc to generate the dependencies for us.
CFLAGS     += -Wp,-MD,.$(@F).d
CFLAGS32   += -Wp,-MD,.$(@F).d
DEPS      = .*.d

LIBS      := -L.
LIBS      += -L$(XEN_LIBXC)
LIBS      += -lxenctrl 
LIBS      += -L$(XEN_XENSTORE) -lxenstore

OBJS	  := db-xenbus.o db-frontend.o
PIC_OBJS  := db-xenbus.opic db-frontend.opic
#OBJS32    := db-xenbus_32.o db-frontend_32.o gx_comm_32.o db-remote_32.o gx_utils_32.o
OBJS32    := gx_comm_32.o db-remote_32.o gx_utils_32.o
LIBS32    :=

all: links $(LIB) $(LIB).$(MAJOR) $(LIB).$(MAJOR).$(MINOR) $(IBIN32)

.PHONY: links
links:	$(ARCH_LINKS)
	[ -e xen ] || ln -sf $(XEN_ROOT)/xen/include/public xen

$(LIB): $(LIB).$(MAJOR)
	ln -sf $< $@

$(LIB).$(MAJOR): $(LIB).$(MAJOR).$(MINOR)
	ln -sf $< $@

$(LIB).$(MAJOR).$(MINOR): $(PIC_OBJS)
	$(CC) $(CFLAGS) $(LIBS) -Wl,$(SONAME_LDFLAG) -Wl,$(LIB).$(MAJOR) $(SHLIB_CFLAGS) -o $@ $^ 

db-xenbus_32.o: db-xenbus.c
	$(CC) $(CFLAGS32) -c -o db-xenbus_32.o $^

db-frontend_32.o: db-frontend.c
	$(CC) $(CFLAGS32) -c -o db-frontend_32.o $^

db-remote_32.o: db-remote.c
	$(CC) $(CFLAGS32) -c -o db-remote_32.o $^

gx_comm_32.o: gx_comm.c
	$(CC) $(CFLAGS32) -c -o gx_comm_32.o $^

gx_utils_32.o: gx_utils.c
	$(CC) $(CFLAGS32) -c -o gx_utils_32.o $^

$(IBIN32): $(OBJS32)
	$(CC)  $(CFLAGS32) -o db-frontend $(OBJS32) $(LIBS32)


install: all
	$(INSTALL_PROG) $(LIB).$(MAJOR).$(MINOR) $(DESTDIR)/usr/$(LIBDIR)
	ln -sf $(LIB).$(MAJOR).$(MINOR) $(DESTDIR)/usr/$(LIBDIR)/$(LIB).$(MAJOR)
	ln -sf $(LIB).$(MAJOR) $(DESTDIR)/usr/$(LIBDIR)/$(LIB)

clean:
	rm -rf *.o *.opic *~ $(DEPS) xen $(LIB)*

.PHONY: clean install

-include $(DEPS)
