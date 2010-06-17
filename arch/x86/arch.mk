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

