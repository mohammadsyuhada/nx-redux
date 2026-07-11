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

TARGET = ratools
INCDIR = -I. -I../common/ -I../settings/ -I../minarch/ -I../../$(PLATFORM)/platform/

COMMON_PATH = ../common

include ../common/ui/ui.mk
SOURCE = ratools.c ratools_data.c ratools_browser.c ratools_sync.c ratools_prefetch.c ratools_reset.c \
         ../settings/settings_menu.c \
         $(COMMON_PATH)/utils.c $(COMMON_PATH)/api.c \
         $(COMMON_PATH)/config.c $(COMMON_PATH)/scaler.c \
         $(COMMON_PATH)/http.c $(COMMON_PATH)/ra_auth.c \
         $(COMMON_PATH)/ra_offline.c $(COMMON_PATH)/ra_offline_net.c \
         $(COMMON_PATH)/ra_hash_cdreader.c ../minarch/chd_reader.c \
         $(COMMON_PATH)/display_helper.c \
         $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c \
         $(UI_DIR)/ui_keyboard.c \
         ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
CFLAGS += -DRC_CLIENT_SUPPORTS_HASH -DHAS_CHDR
LDFLAGS += -L../../$(PLATFORM)/libmsettings -lmsettings -lrcheevos -lchdr

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h $(PREFIX_LOCAL)/lib/librcheevos.a $(PREFIX_LOCAL)/lib/libchdr.so
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make

$(PREFIX_LOCAL)/lib/librcheevos.a:
	cd ../minarch && make rcheevos

$(PREFIX_LOCAL)/lib/libchdr.so:
	cd ../minarch && make libchdr
