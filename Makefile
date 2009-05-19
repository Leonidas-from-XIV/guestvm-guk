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
# Common Makefile for guestvm-ukernel.
#
# Every architecture directory below guestvm-ukernel/arch has to have a
# Makefile and a arch.mk.
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

$(TARGET): $(OBJS)
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

