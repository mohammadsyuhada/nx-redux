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

SDL?=SDL

###########################################################

TARGET=gametimedb

.PHONY: build
.PHONY: clean

CC = $(CROSS_COMPILE)gcc

SYSROOT     := $(shell $(CC) --print-sysroot)

INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/
SOURCE = $(TARGET).c ../common/utils.c ../common/api.c ../common/text_shape.c ../../$(PLATFORM)/platform/platform.c

CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS += -s -lsqlite3

PRODUCT= build/$(PLATFORM)/lib$(TARGET).so
OBJDIR = build/$(PLATFORM)

# Objects go under $(OBJDIR), not the source dir. Only the objects the .so
# actually links are compiled (the other SOURCE entries were
# compiled-and-ignored before; the link list is unchanged).
build:
	mkdir -p $(OBJDIR)
	$(CC) -c -Werror -fpic $(TARGET).c -o $(OBJDIR)/$(TARGET).o -Wl,--no-as-needed $(LDFLAGS) $(CFLAGS)
	$(CC) -c -Werror -fpic ../common/utils.c -o $(OBJDIR)/utils.o -Wl,--no-as-needed $(LDFLAGS) $(CFLAGS)
	$(CC) -shared -o $(PRODUCT) $(OBJDIR)/$(TARGET).o $(OBJDIR)/utils.o $(LDFLAGS)
	cp "$(TARGET).h" "$(PREFIX_LOCAL)/include"
	cp $(PRODUCT) "$(PREFIX_LOCAL)/lib"

clean:
	rm -f *.o
	rm -rf build
	rm -f $(PREFIX_LOCAL)/include/$(TARGET).h
	rm -f $(PREFIX_LOCAL)/lib/lib$(TARGET).so