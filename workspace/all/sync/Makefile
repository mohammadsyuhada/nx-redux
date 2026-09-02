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

# Per-OS build outputs for desktop: macOS (host) and Linux (docker) share
# this checkout, so packaging passes BUILD_SUBDIR=desktop-<os>-<arch> to keep
# their artifacts apart. Devices never pass it: the default keeps their
# build/<platform> paths byte-identical.
BUILD_SUBDIR ?= $(PLATFORM)

SDL?=SDL

###########################################################

TARGET = sync
INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/
include ../common/ui/ui.mk
SOURCE = $(TARGET).c ../common/utils.c ../common/api.c ../common/text_shape.c ../common/audio_manager.c ../common/config.c ../common/scaler.c $(UI_COMPONENT_SRCS) \
         $(UI_DIR)/ui_list.c \
         ../common/paths.c \
         ../../$(PLATFORM)/platform/platform.c

# Desktop's platform.c backs PLAT_wifiConnected with a real reachability probe
# (device platform.c doesn't; it uses generic_wifi.c) -- desktop-only source.
ifeq ($(PLATFORM), desktop)
SOURCE += ../common/desktop_probe.c
endif

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings -lSDL2_image -lSDL2_ttf -lm -ldl -lz -lpthread

PRODUCT= build/$(BUILD_SUBDIR)/$(TARGET).elf

all:
	mkdir -p build/$(BUILD_SUBDIR)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf build/
