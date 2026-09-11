###########################################################

# pi-lens-ignore: SC1073, SC1065, SC1064, SC1072
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

TARGET = osdmusic
PRODUCT = build/$(PLATFORM)/$(TARGET).elf

CC = $(CROSS_COMPILE)gcc
MY_CFLAGS = -Os -Wall -std=gnu99 $(CFLAGS) -DPLATFORM=\"$(PLATFORM)\" \
	-DPLATFORM_$(shell echo $(PLATFORM) | tr a-z A-Z) \
	-I../musicplayer -I../../$(PLATFORM)/libmsettings -DUSE_SDL2
MY_LDFLAGS = $(LDFLAGS) -L../../$(PLATFORM)/libmsettings \
	$(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2") \
	-lSDL2_image -lSDL2_ttf -lmsettings -ldl -lrt -Wl,--gc-sections -s
SOURCE = $(TARGET).c ../musicplayer/music_client.c ../musicplayer/music_service_client.c ../musicplayer/music_balance.c

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(MY_CFLAGS) $(MY_LDFLAGS)

clean:
	rm -f $(PRODUCT)
