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

TARGET = settings
INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/

COMMON_PATH = ../common
PLATFORM_PATH = ../../$(PLATFORM)

SOURCE = settings.c settings_menu.c settings_wifi.c settings_bt.c settings_led.c settings_developer.c settings_updater.c settings_input.c settings_clock.c settings_bootlogo.c \
         $(COMMON_PATH)/utils.c $(COMMON_PATH)/api.c \
         $(COMMON_PATH)/config.c $(COMMON_PATH)/scaler.c \
         $(COMMON_PATH)/http.c $(COMMON_PATH)/ra_auth.c \
         $(COMMON_PATH)/ui_components.c $(COMMON_PATH)/ui_list.c \
         $(COMMON_PATH)/ui_keyboard.c \
         $(COMMON_PATH)/display_helper.c \
         $(PLATFORM_PATH)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\"
LDFLAGS += -L../../$(PLATFORM)/libmsettings -lmsettings


PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make
