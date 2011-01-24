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
# The file contains the common make rules for building GUK.
#

debug = y

# find gcc headers
GCC_BASE=$(shell $(CC) -print-search-dirs | grep ^install | cut -f 2 -d ' ')

ifeq ($(XEN_OS),SunOS)
GCC_INCLUDE:=${GCC_BASE}install-tools/include
endif

ifeq ($(XEN_OS),Linux)
GCC_INCLUDE:=-I${GCC_BASE}include
GCC_INCLUDE+=-I${GCC_BASE}include-fixed
endif

# Define some default flags.
# NB. '-Wcast-qual' is nasty, so I omitted it.
# use -nostdinc to avoid name clashes, but include gcc standard headers
DEF_CFLAGS := -fno-builtin -nostdinc $(GCC_INCLUDE)
DEF_CFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)
DEF_CFLAGS += -Wall -Werror -Wredundant-decls -Wno-format
DEF_CFLAGS += -Wstrict-prototypes -Wnested-externs -Wpointer-arith -Winline
DEF_CFLAGS += -D__XEN_INTERFACE_VERSION__=$(XEN_INTERFACE_VERSION)
DEF_CFLAGS += -DCONFIG_PREEMPT -DCONFIG_SMP

DEF_ASFLAGS = -D__ASSEMBLY__
DEF_LDFLAGS =

ifeq ($(debug),y)
DEF_CFLAGS += -g
else
DEF_CFLAGS += -O3
endif

# Build the CFLAGS and ASFLAGS for compiling and assembling.
# DEF_... flags are the common flags,
# ARCH_... flags may be defined in arch/$(TARGET_ARCH_FAM/rules.mk
CFLAGS := $(DEF_CFLAGS) $(ARCH_CFLAGS)
ASFLAGS := $(DEF_ASFLAGS) $(ARCH_ASFLAGS)
LDFLAGS := $(DEF_LDFLAGS) $(ARCH_LDFLAGS)

# The path pointing to the architecture specific header files.
ARCH_INC := $(GUK_ROOT)/include/$(TARGET_ARCH_FAM)

# Special build dependencies.
# Rebuild all after touching this/these file(s)
EXTRA_DEPS = $(GUK_ROOT)/guk.mk \
		$(GUK_ROOT)/$(TARGET_ARCH_DIR)/arch.mk \
		$(XEN_ROOT)/Config.mk

# Find all header files for checking dependencies.
HDRS := $(wildcard $(GUK_ROOT)/include/*.h)
HDRS += $(wildcard $(GUK_ROOT)/include/xen/*.h)
HDRS += $(wildcard $(GUK_ROOT)/include/guk/*.h)
HDRS += $(wildcard $(ARCH_INC)/*.h)
# For special wanted header directories.
extra_heads := $(foreach dir,$(EXTRA_INC),$(wildcard $(dir)/*.h))
HDRS += $(extra_heads)

# Add the special header directories to the include paths.
#extra_incl := $(foreach dir,$(EXTRA_INC),-I$(GUK_ROOT)/include/$(dir))
override CPPFLAGS := -I$(GUK_ROOT)/tools/fs-back -I$(GUK_ROOT)/tools/db-front -I$(GUK_ROOT)/include $(CPPFLAGS) 
#$(extra_incl)

# The name of the architecture specific library.
# This is on x86_32: libx86_32.a
# $(ARCH_LIB) has to built in the architecture specific directory.
ARCH_LIB_NAME = $(TARGET_ARCH)
ARCH_LIB := lib$(ARCH_LIB_NAME).a

# This object contains the entrypoint for startup from Xen.
# $(HEAD_ARCH_OBJ) has to be built in the architecture specific directory.
HEAD_ARCH_OBJ := $(TARGET_ARCH).o
HEAD_OBJ := $(TARGET_ARCH_DIR)/$(HEAD_ARCH_OBJ)


%.o: %.c $(HDRS) Makefile $(EXTRA_DEPS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

%.o: %.S $(HDRS) Makefile $(EXTRA_DEPS)
	$(CC) $(ASFLAGS) $(CPPFLAGS) -c $< -o $@

%.E: %.c $(HDRS) Makefile $(EXTRA_DEPS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -E $< -o $@






