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
REQUIRES_MUPEN64PLUS=other/mupen64plus
REQUIRES_PPSSPP=other/ppsspp
REQUIRES_SDL2_DRASTIC=other/sdl2-drastic

# Pinned upstream commits — update these when upgrading to a new version
PPSSPP_REPO=https://github.com/hrydgard/ppsspp.git
PPSSPP_COMMIT=eb859735feddf88dbe651763f366a7705612113a  # v1.20.1
SDL_DRASTIC_REPO=https://github.com/trngaje/SDL_drastic.git
SDL_DRASTIC_COMMIT=eb2e00f  # knulli2.30.8
MUPEN64PLUS_CORE_REPO=https://github.com/mupen64plus/mupen64plus-core.git
MUPEN64PLUS_CORE_COMMIT=d747ccee850799a8f30152cbbaa200639813a0de  # 2026-02-24
MUPEN64PLUS_UI_REPO=https://github.com/mupen64plus/mupen64plus-ui-console.git
MUPEN64PLUS_UI_COMMIT=927abd08a390325ee3712a99a5e11d94855ddaee  # 2026-02-19
MUPEN64PLUS_AUDIO_REPO=https://github.com/mupen64plus/mupen64plus-audio-sdl.git
MUPEN64PLUS_AUDIO_COMMIT=2ed23eda04f10f1285fdc1d26807b5c34f9f3faa  # 2026-02-19
GLIDEN64_REPO=https://github.com/gonetz/GLideN64.git
GLIDEN64_COMMIT=c8ef81c7d9aede9f67f6ed3d3426c90541f9f13e  # 2026-01-25

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

$(REQUIRES_EVTEST):
	git clone --depth 1 https://github.com/freedesktop-unofficial-mirror/evtest.git $(REQUIRES_EVTEST)

$(REQUIRES_JSTEST):
	git clone --depth 1 https://github.com/datrh/joyutils.git $(REQUIRES_JSTEST)

$(REQUIRES_UNZIP60):
	git clone --depth 1 https://github.com/shauninman/unzip60.git $(REQUIRES_UNZIP60)

$(REQUIRES_MUPEN64PLUS)/mupen64plus-core:
	mkdir -p $(REQUIRES_MUPEN64PLUS)
	git clone $(MUPEN64PLUS_CORE_REPO) $(REQUIRES_MUPEN64PLUS)/mupen64plus-core
	cd $(REQUIRES_MUPEN64PLUS)/mupen64plus-core && git checkout $(MUPEN64PLUS_CORE_COMMIT)
	git clone $(MUPEN64PLUS_UI_REPO) $(REQUIRES_MUPEN64PLUS)/mupen64plus-ui-console
	cd $(REQUIRES_MUPEN64PLUS)/mupen64plus-ui-console && git checkout $(MUPEN64PLUS_UI_COMMIT)
	cd $(REQUIRES_MUPEN64PLUS)/mupen64plus-ui-console && git apply ../mupen64plus-ui-console.patch
	git clone $(MUPEN64PLUS_AUDIO_REPO) $(REQUIRES_MUPEN64PLUS)/mupen64plus-audio-sdl
	cd $(REQUIRES_MUPEN64PLUS)/mupen64plus-audio-sdl && git checkout $(MUPEN64PLUS_AUDIO_COMMIT)
	cd $(REQUIRES_MUPEN64PLUS)/mupen64plus-audio-sdl && git apply ../mupen64plus-audio-sdl.patch
	git clone $(GLIDEN64_REPO) $(REQUIRES_MUPEN64PLUS)/GLideN64-standalone
	cd $(REQUIRES_MUPEN64PLUS)/GLideN64-standalone && git checkout $(GLIDEN64_COMMIT) && git submodule update --init --recursive
	cd $(REQUIRES_MUPEN64PLUS)/GLideN64-standalone && git apply ../GLideN64-standalone.patch

$(REQUIRES_PPSSPP)/ppsspp:
	git clone $(PPSSPP_REPO) $(REQUIRES_PPSSPP)/ppsspp
	cd $(REQUIRES_PPSSPP)/ppsspp && git checkout $(PPSSPP_COMMIT)
	cd $(REQUIRES_PPSSPP)/ppsspp && git submodule update --init --recursive
	cd $(REQUIRES_PPSSPP)/ppsspp && git apply ../ppsspp.patch

$(REQUIRES_SDL2_DRASTIC)/SDL_drastic:
	git clone $(SDL_DRASTIC_REPO) $(REQUIRES_SDL2_DRASTIC)/SDL_drastic
	cd $(REQUIRES_SDL2_DRASTIC)/SDL_drastic && git checkout $(SDL_DRASTIC_COMMIT)
	cd $(REQUIRES_SDL2_DRASTIC)/SDL_drastic && patch -p1 < ../0006-add-hook-for-drastic.patch
	cd $(REQUIRES_SDL2_DRASTIC)/SDL_drastic && patch -p1 < ../fix-tg5040.patch

include ../all/readmes/Makefile
