# Convenience wrapper: builds the hooked SDL2 for DraStic (NDS.pak) and deploys
# it into the skeleton. Sources are cloned per platform by the platform Makefile
# (`make other/sdl2-drastic/SDL_drastic` from workspace/<platform>); build output
# lands in workspace/<platform>/other/sdl2-drastic/output/.
#
# Usage: make build PLATFORM=tg5040   (or tg5050)

PLATFORM ?= tg5040

DOCKER_IMAGE = ghcr.io/loveretro/$(PLATFORM)-toolchain:latest
WORKSPACE    = $(shell cd ../../.. && pwd)
PLAT_DIR     = ../../../$(PLATFORM)/other/sdl2-drastic
OUTPUT       = $(PLAT_DIR)/output/libSDL2-2.0.so.0
SKELETON     = ../../../../skeleton/SYSTEM/$(PLATFORM)/paks/Emus/NDS.pak/libs/libSDL2-2.0.so.0

.PHONY: build deploy clean clean-all

build:
	docker run --rm \
		-v "$(WORKSPACE)":/root/workspace \
		-w /root/workspace/$(PLATFORM)/other/sdl2-drastic \
		$(DOCKER_IMAGE) bash /root/workspace/all/other/sdl2-drastic/build-sdl2-drastic-$(PLATFORM).sh

deploy: build
	cp $(OUTPUT) $(SKELETON)
	@echo "Deployed to $(SKELETON)"

clean:
	rm -rf $(PLAT_DIR)/output/
	rm -rf $(PLAT_DIR)/SDL_drastic/.fb_fixed $(PLAT_DIR)/SDL_drastic/.vsync_fixed $(PLAT_DIR)/SDL_drastic/.events_fixed $(PLAT_DIR)/SDL_drastic/.hotkeys_fixed $(PLAT_DIR)/SDL_drastic/.input_fixed
	@echo "Cleaned output (run 'make clean-all' to remove SDL_drastic source)"

clean-all: clean
	rm -rf $(PLAT_DIR)/SDL_drastic/ $(PLAT_DIR)/extra-deps/
