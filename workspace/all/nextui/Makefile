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

TARGET = nextui
INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/
include ../common/ui/ui.mk
SOURCE = $(TARGET).c types.c recents.c content.c launcher.c imgloader.c shortcuts.c gamelist.c gameswitcher.c search.c ../common/display_helper.c ../common/scaler.c ../common/utils.c ../common/config.c ../common/api.c $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c $(UI_DIR)/ui_listdialog.c $(UI_DIR)/ui_keyboard.c $(UI_DIR)/ui_connect.c $(UI_DIR)/ui_contextmenu.c ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make
