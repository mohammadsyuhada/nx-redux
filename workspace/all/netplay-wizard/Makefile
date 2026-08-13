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
SDL?=SDL

###########################################################

TARGET = netplay
INCDIR = -I. -I../common/ -I../netplay/ -I../../$(PLATFORM)/platform/
include ../common/ui/ui.mk
SOURCE = wizard.c wizard_wifi.c wizard_net.c wizard_sync.c \
         ../netplay/wifi_direct.c ../netplay/network_common.c $(UI_DIR)/ui_keyboard.c \
         ../common/utils.c ../common/api.c ../common/text_shape.c ../common/audio_manager.c ../common/config.c ../common/scaler.c \
         $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c $(UI_LISTVIEW_SRCS) \
         ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
# HAS_WIFIMG is what selects the real WiFi/hotspot arms of the netplay sources
# (netplay.c, netplay_helper.c, gbalink.c, gblink.c — their #else arm is "WiFi not
# available on this platform"). wizard_wifi.c ports netplay_helper.c code without
# carrying those #ifdefs (this target only builds for tg5040/tg5050, which have
# WiFi), and none of the files in SOURCE tests the flag, so it is inert today —
# kept as the build's statement of intent and for any netplay source added later.
CFLAGS  += -DHAS_WIFIMG
LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings -lSDL2_image -lSDL2_ttf -lm -ldl -lz -lpthread

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf build/
