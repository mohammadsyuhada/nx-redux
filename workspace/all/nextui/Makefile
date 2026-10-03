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
# gameinfo.c: ratools data code + RA offline cache, game time DB
INCDIR += -I../ratools -I../libgametimedb
include ../common/ui/ui.mk
SOURCE = $(TARGET).c types.c recents.c content.c emulist_model.c launcher.c imgloader.c artbg.c shortcuts.c gamelist.c gameswitcher.c search.c menutabs.c menutabs_model.c list_window.c menuart.c area_scale.c menulogo.c gameinfo.c gameinfo_text.c collcount.c collcount_model.c collname.c infoband.c infoband_layout.c home.c home_layout.c home_strip.c homeart.c homeart_model.c row_model.c home_stats.c home_stats_model.c grid_layout.c tiles.c placeholder_art.c gridview.c rowview.c stack_model.c stackview.c caption_fit.c menu_transition.c contentdim.c ../ratools/ratools_data.c ../common/ra_offline.c cpu_policy.c ../common/arcade_names.c ../common/display_helper.c ../common/scaler.c ../common/utils.c ../common/config.c ../common/api.c ../common/text_shape.c ../common/audio_manager.c ../common/wifi.c $(UI_COMPONENT_SRCS) $(UI_DIR)/ui_list.c $(UI_LISTVIEW_SRCS) $(UI_DIR)/ui_listdialog.c $(UI_DIR)/ui_keyboard.c $(UI_DIR)/ui_contextmenu.c ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc
CFLAGS  += $(OPT)
CFLAGS  += $(INCDIR) -I../../$(PLATFORM)/libmsettings -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS	 += -L../../$(PLATFORM)/libmsettings -lmsettings
LDFLAGS	 += -lgametimedb -lsqlite3 -lrcheevos -lpthread

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: $(PREFIX_LOCAL)/include/msettings.h $(PREFIX_LOCAL)/include/gametimedb.h $(PREFIX_LOCAL)/lib/librcheevos.a
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../../$(PLATFORM)/libmsettings && make

$(PREFIX_LOCAL)/include/gametimedb.h:
	cd ../libgametimedb && make

# nextui builds before minarch in workspace/Makefile, so also fetch the
# libretro-common checkout rcheevos' rc_libretro.c includes (no-op once present)
$(PREFIX_LOCAL)/lib/librcheevos.a:
	cd ../minarch && make libretro-common rcheevos
