###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

ifeq (,$(PREFIX))
	$(error missing PREFIX for this toolchain)
endif

###########################################################

include ../../$(PLATFORM)/platform/Makefile.env

# Per-OS build outputs for desktop: macOS (host) and Linux (docker) share
# this checkout, so packaging passes BUILD_SUBDIR=desktop-<os>-<arch> to keep
# their artifacts apart. Devices never pass it: the default keeps their
# build/<platform> paths byte-identical.
BUILD_SUBDIR ?= $(PLATFORM)

SDL?=SDL

###########################################################

TARGET=gametimedb

.PHONY: build
.PHONY: clean

CC = $(CROSS_COMPILE)gcc

SYSROOT     := $(shell $(CC) --print-sysroot)

INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/
# ../common/paths.c is unconditional (mirrors ratools/Makefile): it's
# SDL-free/defines.h-free so it compiles on every platform, and utils.c +
# gametimedb.c both reference the PATHS_* globals it defines whenever
# HAS_RUNTIME_PATHS is on (desktop) -- those symbols must be resolvable
# inside this .so itself, not just in the executables that link it.
SOURCE = $(TARGET).c ../common/utils.c ../common/api.c ../common/text_shape.c ../common/paths.c ../../$(PLATFORM)/platform/platform.c

CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS += -s -lsqlite3

PRODUCT= build/$(BUILD_SUBDIR)/lib$(TARGET).so
OBJDIR = build/$(BUILD_SUBDIR)

# Objects go under $(OBJDIR), not the source dir: the desktop macOS and Linux
# (docker) flows build here concurrently, and cwd .o files would race. Only
# the three objects the .so actually links are compiled (the other SOURCE
# entries were compiled-and-ignored before; the link list is unchanged).
build:
	mkdir -p $(OBJDIR)
	$(CC) -c -Werror -fpic $(TARGET).c -o $(OBJDIR)/$(TARGET).o -Wl,--no-as-needed $(LDFLAGS) $(CFLAGS)
	$(CC) -c -Werror -fpic ../common/utils.c -o $(OBJDIR)/utils.o -Wl,--no-as-needed $(LDFLAGS) $(CFLAGS)
	$(CC) -c -Werror -fpic ../common/paths.c -o $(OBJDIR)/paths.o -Wl,--no-as-needed $(LDFLAGS) $(CFLAGS)
	$(CC) -shared -o $(PRODUCT) $(OBJDIR)/$(TARGET).o $(OBJDIR)/utils.o $(OBJDIR)/paths.o $(LDFLAGS)
	cp "$(TARGET).h" "$(PREFIX_LOCAL)/include"
	cp $(PRODUCT) "$(PREFIX_LOCAL)/lib"

clean:
	rm -f *.o
	rm -rf build
	rm -f $(PREFIX_LOCAL)/include/$(TARGET).h
	rm -f $(PREFIX_LOCAL)/lib/lib$(TARGET).so