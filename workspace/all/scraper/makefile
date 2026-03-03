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

include ../../$(PLATFORM)/platform/makefile.env
SDL?=SDL

###########################################################

TARGET = scraper
INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/

COMMON_PATH = ../common
PLATFORM_PATH = ../../$(PLATFORM)

SOURCE = scraper.c scraper_api.c scraper_compositor.c scraper_systems.c \
         $(COMMON_PATH)/utils.c $(COMMON_PATH)/api.c \
         $(COMMON_PATH)/config.c $(COMMON_PATH)/scaler.c \
         $(COMMON_PATH)/http.c \
         $(COMMON_PATH)/ui_components.c $(COMMON_PATH)/ui_list.c \
         $(COMMON_PATH)/ui_keyboard.c \
         $(COMMON_PATH)/display_helper.c \
         $(COMMON_PATH)/cjson/cJSON.c \
         $(PLATFORM_PATH)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS += -L../../$(PLATFORM)/libmsettings -lmsettings -lpthread

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make
