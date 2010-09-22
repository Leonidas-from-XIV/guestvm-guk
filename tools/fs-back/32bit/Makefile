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
#
ifndef XEN_ROOT 
  $(error "Must set XEN_ROOT environment variable to the root of Xen tree")
endif

OSNAME ?= $(shell uname -s)
ifneq ($(OSNAME),Linux)
  $(error "fs-back can only be built and installed in a Linux guest")
endif

OSARCH ?= $(shell arch)
ifneq ($(OSARCH),x86_64)
  $(error "fs-back must be compiled on a 64-bit system")
endif

ifndef XEN_LIBS32
  $(error "Must set XEN_LIBS32 to directory containing 32 bit versions of libxenstore...")
endif

include $(XEN_ROOT)/tools/Rules.mk

INCLUDES += -I.

IBIN         = fs-backend 
INST_DIR     = /usr/sbin

CFLAGS   = -m32
CFLAGS   += -Werror
CFLAGS   += -Wno-unused
CFLAGS   += -fno-strict-aliasing
CFLAGS   += -I $(XEN_ROOT)/tools/include -I $(XEN_LIBXC) -I $(XEN_XENSTORE)
CFLAGS   += -I.. $(INCLUDES)
CFLAGS   += -D_GNU_SOURCE

# Get gcc to generate the dependencies for us.
CFLAGS   += -Wp,-MD,.$(@F).d
DEPS      = .*.d

LIBS      := -L. -L$(XEN_LIBS32) -lxenctrl -lxenstore -lpthread -lrt 

OBJS	  := fs-xenbus.o fs-ops.o

all: links $(IBIN)

.PHONY: links
links:	$(ARCH_LINKS)
	[ -e xen ] || ln -sf $(XEN_ROOT)/xen/include/public xen
	[ -e fs-xenbus.c ] || ln -sf ../fs-xenbus.c
	[ -e fs-backend.c ] || ln -sf ../fs-backend.c
	[ -e fs-ops.c ] || ln -sf ../fs-ops.c


fs-backend: $(OBJS) fs-backend.c
	$(CC) $(CFLAGS) -o fs-backend $(OBJS) $(LIBS) fs-backend.c

install: all
	$(INSTALL_PROG) $(IBIN) $(DESTDIR)$(INST_DIR)

%.E: %.c 
	$(CC) $(CFLAGS) $(CPPFLAGS) -E $< -o $@

clean:
	rm -rf *.o *~ $(DEPS) xen $(IBIN) $(LIB)

.PHONY: clean install

-include $(DEPS)
