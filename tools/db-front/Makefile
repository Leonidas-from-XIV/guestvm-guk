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
  $(error "db-front can only be built and installed in a Linux guest")
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
CFLAGS   += -I $(XEN_LIBXC) -I $(XEN_XENSTORE)
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
	rm -rf *.o *.opic *~ $(DEPS) xen $(LIB)*

.PHONY: clean install

-include $(DEPS)
