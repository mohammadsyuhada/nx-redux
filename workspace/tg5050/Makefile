###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
$(error please specify PLATFORM, eg. PLATFORM=rgb30 make)
endif

###########################################################

REQUIRES_EVTEST=other/evtest
REQUIRES_JSTEST=other/jstest
REQUIRES_UNZIP60=other/unzip60
REQUIRES_FILEMGR=other/NextCommander
REQUIRES_SDL2_DRASTIC=other/sdl2-drastic

# Pinned upstream commits — update these when upgrading to a new version
SDL_DRASTIC_REPO=https://github.com/trngaje/SDL_drastic.git
SDL_DRASTIC_COMMIT=eb2e00f  # knulli2.30.8

all: readmes

early: $(REQUIRES_EVTEST) $(REQUIRES_JSTEST) $(REQUIRES_UNZIP60) $(REQUIRES_FILEMGR)
	mkdir -p other
	cd $(REQUIRES_FILEMGR) && make -j
	cd $(REQUIRES_EVTEST) && $(CROSS_COMPILE)gcc -o evtest evtest.c
	cd $(REQUIRES_JSTEST) && $(CROSS_COMPILE)gcc -o jstest jstest.c
	cd $(REQUIRES_UNZIP60) && make -f unix/Makefile.trimuismart unzip

clean:
	cd $(REQUIRES_FILEMGR) && make clean
	cd $(REQUIRES_UNZIP60) && make -f unix/Makefile.trimuismart clean

###########################################################

$(REQUIRES_FILEMGR):
	git clone --depth 1 https://github.com/LoveRetro/NextCommander.git $(REQUIRES_FILEMGR)
	cd $(REQUIRES_FILEMGR) && git apply ../../../all/other/NextCommander.patch

$(REQUIRES_EVTEST):
	git clone --depth 1 https://github.com/freedesktop-unofficial-mirror/evtest.git $(REQUIRES_EVTEST)

$(REQUIRES_JSTEST):
	git clone --depth 1 https://github.com/datrh/joyutils.git $(REQUIRES_JSTEST)

$(REQUIRES_UNZIP60):
	git clone --depth 1 https://github.com/shauninman/unzip60.git $(REQUIRES_UNZIP60)

$(REQUIRES_SDL2_DRASTIC)/SDL_drastic:
	git clone $(SDL_DRASTIC_REPO) $(REQUIRES_SDL2_DRASTIC)/SDL_drastic
	cd $(REQUIRES_SDL2_DRASTIC)/SDL_drastic && git checkout $(SDL_DRASTIC_COMMIT)
	cd $(REQUIRES_SDL2_DRASTIC)/SDL_drastic && patch -p1 < ../../../../all/other/sdl2-drastic/0006-add-hook-for-drastic.patch
	cd $(REQUIRES_SDL2_DRASTIC)/SDL_drastic && patch -p1 < ../../../../all/other/sdl2-drastic/fix-tg5050.patch

include ../all/readmes/Makefile
