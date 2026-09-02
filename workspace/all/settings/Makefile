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

TARGET = settings
INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/

COMMON_PATH = ../common
PLATFORM_PATH = ../../$(PLATFORM)

include ../common/ui/ui.mk
SOURCE = settings.c settings_menu.c settings_wifi.c settings_bt.c settings_led.c settings_developer.c settings_updater.c updater_url.c settings_input.c settings_clock.c settings_bootlogo.c settings_audio.c \
         $(COMMON_PATH)/utils.c $(COMMON_PATH)/api.c $(COMMON_PATH)/text_shape.c $(COMMON_PATH)/audio_manager.c \
         $(COMMON_PATH)/config.c $(COMMON_PATH)/scaler.c \
         $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c $(UI_LISTVIEW_SRCS) \
         $(UI_DIR)/ui_keyboard.c \
         $(COMMON_PATH)/wget_fetch.c $(COMMON_PATH)/wifi.c \
         $(COMMON_PATH)/display_helper.c \
         $(COMMON_PATH)/paths.c \
         $(PLATFORM_PATH)/platform/platform.c

# Desktop's platform.c backs PLAT_wifiConnected with a real reachability probe
# (device platform.c doesn't; it uses generic_wifi.c) -- desktop-only source.
ifeq ($(PLATFORM), desktop)
SOURCE += ../common/desktop_probe.c
endif

CC = $(CROSS_COMPILE)gcc
CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\"
LDFLAGS += -L../../$(PLATFORM)/libmsettings -lmsettings


PRODUCT= build/$(BUILD_SUBDIR)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(BUILD_SUBDIR)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make
