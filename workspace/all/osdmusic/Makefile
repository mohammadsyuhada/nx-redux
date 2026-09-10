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

###########################################################

# Resident renderer for the trimui_osdd Music widget (see osdmusic.c and
# skeleton/SYSTEM/osd/common/widgets/app_music/launch.sh).
TARGET = osdmusic
PRODUCT = build/$(PLATFORM)/$(TARGET).elf

CC = $(CROSS_COMPILE)gcc
MY_CFLAGS = -Os -Wall -std=gnu99 $(CFLAGS) -DPLATFORM=\"$(PLATFORM)\"
MY_LDFLAGS = $(LDFLAGS) -Wl,--gc-sections -s

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(TARGET).c -o $(PRODUCT) $(MY_CFLAGS) $(MY_LDFLAGS)

clean:
	rm -f $(PRODUCT)
