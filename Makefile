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
# Common Makefile for guk.
#
# Every architecture directory below guk/arch has to have a
# Makefile and an arch.mk.
#

ifndef XEN_ROOT 
  $(error "Must set XEN_ROOT environment variable to the root of Xen tree")
endif


include $(XEN_ROOT)/Config.mk
export XEN_ROOT

# force 64 bit
XEN_COMPILE_ARCH=x86_64
XEN_TARGET_ARCH=x86_64

XEN_INTERFACE_VERSION := 0x00030205
export XEN_INTERFACE_VERSION

# Set TARGET_ARCH
override TARGET_ARCH := $(XEN_TARGET_ARCH)

# Set guk root path, used in guk.mk.
GUK_ROOT=$(PWD)
export GUK_ROOT

TARGET_ARCH_FAM = x86

# The architecture family directory below guk.
TARGET_ARCH_DIR := arch/$(TARGET_ARCH_FAM)

# Export these variables for possible use in architecture dependent makefiles.
export TARGET_ARCH
export TARGET_ARCH_DIR
export TARGET_ARCH_FAM

# This is used for architecture specific links.
# This can be overwritten from arch specific rules.
ARCH_LINKS =

# For possible special header directories.
# This can be overwritten from arch specific rules.
EXTRA_INC =

# Include the architecture family's special makerules.
# This must be before include guk.mk!
include $(TARGET_ARCH_DIR)/arch.mk

# Include common guk makerules.
include guk.mk

# Define some default flags for linking.
LDLIBS := 
LDARCHLIB := -L$(TARGET_ARCH_DIR) -l$(ARCH_LIB_NAME)
LDFLAGS_FINAL := -N -T $(TARGET_ARCH_DIR)/guk-$(TARGET_ARCH).lds

# Prefix for global API names. All other local symbols are localised before
# linking with EXTRA_OBJS.
GLOBAL_PREFIX := guk_
EXTRA_OBJS =

TARGET := guk

# Subdirectories common to guk
SUBDIRS := lib xenbus console

# The common guk objects to build.
OBJS := $(patsubst %.c,%.o,$(wildcard *.c))
OBJS += $(patsubst %.c,%.o,$(wildcard lib/*.c))
OBJS += $(patsubst %.c,%.o,$(wildcard xenbus/*.c))
OBJS += $(patsubst %.c,%.o,$(wildcard console/*.c))


.PHONY: default
default: links arch_lib $(TARGET) 

# Create special architecture specific links. The function arch_links
# has to be defined in arch.mk (see include above).
ifneq ($(ARCH_LINKS),)
$(ARCH_LINKS):
	$(arch_links)
endif

.PHONY: links
links:	$(ARCH_LINKS)
	[ -e include/xen ] || ln -sf $(XEN_ROOT)/xen/include/public include/xen

.PHONY: arch_lib
arch_lib:
	$(MAKE) --directory=$(TARGET_ARCH_DIR) || exit 1;

$(TARGET): $(OBJS) $(TARGET_ARCH_DIR)/lib$(ARCH_LIB_NAME).a
	$(LD) -r $(LDFLAGS) $(HEAD_OBJ) $(OBJS) $(LDARCHLIB) -o $@.o
	$(OBJCOPY) -w -G $(GLOBAL_PREFIX)* -G _start -G str* -G mem* -G hypercall_page $@.o $@.o
	$(LD) $(LDFLAGS) $(LDFLAGS_FINAL) $@.o $(EXTRA_OBJS) -o $@
	gzip -f -9 -c $@ >$@.gz

.PHONY: clean arch_clean

arch_clean:
	$(MAKE) --directory=$(TARGET_ARCH_DIR) clean || exit 1;

clean:	arch_clean
	for dir in $(SUBDIRS); do \
		rm -f $$dir/*.o; \
	done
	rm -f *.o *~ core $(TARGET).elf $(TARGET).raw $(TARGET) $(TARGET).gz
	find . -type l | xargs rm -f


define all_sources
     ( find . -follow -name SCCS -prune -o -name '*.[chS]' -print )
endef

.PHONY: cscope
cscope:
	$(all_sources) > cscope.files
	cscope -k -b -q

.PHONY: tags
tags:
	$(all_sources) | xargs ctags

%.S: %.c 
	$(CC) $(CFLAGS) $(CPPFLAGS) -S $< -o $@

