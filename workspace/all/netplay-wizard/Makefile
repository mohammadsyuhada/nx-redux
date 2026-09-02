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

TARGET = netplay
INCDIR = -I. -I../common/ -I../netplay/ -I../../$(PLATFORM)/platform/
include ../common/ui/ui.mk
SOURCE = wizard.c wizard_wifi.c wizard_net.c wizard_sync.c \
         ../netplay/network_common.c $(UI_DIR)/ui_keyboard.c \
         ../common/utils.c ../common/api.c ../common/text_shape.c ../common/audio_manager.c ../common/config.c ../common/scaler.c \
         $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c $(UI_LISTVIEW_SRCS) \
         ../common/paths.c \
         ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99

# HAS_WIFIMG selects the real WiFi/hotspot arms of the wizard (and of the
# netplay sources generally — netplay.c, netplay_helper.c, gbalink.c, gblink.c
# test it the same way). Device platforms get wifi_direct.c and the full
# hotspot + WiFi-picker flow; desktop has no managed WiFi stack, so it builds
# without either — the wizard's #else arms reduce to "already on a network"
# (mode is always "wifi" there, and the Hotspot/WiFi menu never shows).
ifneq (,$(filter $(PLATFORM),tg5040 tg5050))
SOURCE += ../netplay/wifi_direct.c
CFLAGS += -DHAS_WIFIMG
endif

# Desktop's platform.c backs PLAT_wifiConnected with a real reachability probe
# (device platform.c doesn't; it uses generic_wifi.c) -- desktop-only source.
ifeq ($(PLATFORM), desktop)
SOURCE += ../common/desktop_probe.c
endif

LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings -lSDL2_image -lSDL2_ttf -lm -ldl -lz -lpthread

PRODUCT= build/$(BUILD_SUBDIR)/$(TARGET).elf

all:
	mkdir -p build/$(BUILD_SUBDIR)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf build/
