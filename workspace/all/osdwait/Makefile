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

###########################################################

include ../../$(PLATFORM)/platform/Makefile.env

###########################################################

# LD_PRELOAD shim for trimui_osdd's SDL_WaitEvent (see osdwait.c). Deliberately
# links nothing but libdl: it must load into the stock daemon's process.
TARGET = osdwait
PRODUCT = build/$(PLATFORM)/lib$(TARGET).so

CC = $(CROSS_COMPILE)gcc

all:
	mkdir -p build/$(PLATFORM)
	$(CC) -shared -fpic -Os -Wall -Werror -std=gnu99 $(TARGET).c -o $(PRODUCT) -ldl -s

clean:
	rm -f $(PRODUCT)
