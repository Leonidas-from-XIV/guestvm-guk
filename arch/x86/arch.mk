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
# Architecture special makerules for x86 family
# (including x86_32, x86_32y and x86_64).
#

ifeq ($(TARGET_ARCH),x86_32)
ARCH_CFLAGS  := -m32 -march=i686
ARCH_LDFLAGS := -m elf_i386
ARCH_ASFLAGS := -m32
EXTRA_INC += $(TARGET_ARCH_FAM)/$(TARGET_ARCH)
EXTRA_SRC += arch/$(EXTRA_INC)

ifeq ($(XEN_TARGET_X86_PAE),y)
ARCH_CFLAGS  += -DCONFIG_X86_PAE=1
ARCH_ASFLAGS += -DCONFIG_X86_PAE=1
endif
endif

ifeq ($(TARGET_ARCH),x86_64)
ARCH_CFLAGS := -m64 -mno-red-zone -fPIC -fno-reorder-blocks
ARCH_CFLAGS += -fno-asynchronous-unwind-tables
ARCH_CFLAGS += -ffixed-r14
ARCH_ASFLAGS := -m64
ARCH_LDFLAGS := -m elf_x86_64
EXTRA_INC += $(TARGET_ARCH_FAM)/$(TARGET_ARCH)
EXTRA_SRC += arch/$(EXTRA_INC)
endif

