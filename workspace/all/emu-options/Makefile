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

TARGET = options
INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/
include ../common/ui/ui.mk
SOURCE = options.c opts_override.c opts_minarch.c \
         ../common/emu_overlay_cfg.c ../common/cjson/cJSON.c \
         ../common/utils.c ../common/api.c ../common/audio_manager.c ../common/config.c ../common/scaler.c \
         $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c \
         ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
# Raised emu_overlay_cfg limits so a minarch options.json always fits.
# MUST stay in lockstep with OPTS_SCHEMA_MAX_* in ../minarch/ma_opts_schema.h
# (and with the -D line in tests/test_opts_minarch.c's build command).
CFLAGS  += -DEMU_OVL_MAX_SECTIONS=32 -DEMU_OVL_MAX_ITEMS=64 -DEMU_OVL_MAX_VALUES=32 -DEMU_OVL_MAX_DESC=512
LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings -lSDL2_image -lSDL2_ttf -lm -ldl -lz -lpthread

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf build/
