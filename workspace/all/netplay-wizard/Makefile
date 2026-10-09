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
INCDIR = -I. -I../common/ -I../netplay/ -I../usblink/ -I../../$(PLATFORM)/platform/
include ../common/ui/ui.mk
SOURCE = wizard.c wizard_wifi.c wizard_net.c wizard_sync.c wizard_usb.c wiz_caps.c wiz_modes.c \
         ../netplay/network_common.c ../usblink/usblink_state.c $(UI_DIR)/ui_keyboard.c \
         ../common/utils.c ../common/api.c ../common/text_shape.c ../common/audio_manager.c ../common/config.c ../common/scaler.c \
         $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c $(UI_LISTVIEW_SRCS) \
         ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99

# HAS_WIFIMG selects the real WiFi/hotspot arms of the wizard (and of the
# netplay sources generally — netplay.c, netplay_helper.c, gbalink.c, gblink.c
# test it the same way). Device platforms get wifi_direct.c and the full
# hotspot + WiFi-picker flow.
ifneq (,$(filter $(PLATFORM),tg5040 tg5050))
SOURCE += ../netplay/wifi_direct.c
CFLAGS += -DHAS_WIFIMG
endif

LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings -lSDL2_image -lSDL2_ttf -lm -ldl -lz -lpthread

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf build/
