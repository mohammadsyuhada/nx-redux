DOCKER_IMAGE = ghcr.io/loveretro/tg5040-toolchain:latest
WORKSPACE    = $(shell cd ../../.. && pwd)
SCRIPT_DIR   = $(shell pwd)
OUTPUT       = output/libSDL2-2.0.so.0
SKELETON     = ../../../skeleton/EXTRAS/Emus/tg5040/NDS.pak/libs/libSDL2-2.0.so.0

.PHONY: build deploy clean

build:
	docker run --rm \
		-v "$(WORKSPACE)":/root/workspace \
		-w /root/workspace/tg5040/other/sdl2-drastic \
		$(DOCKER_IMAGE) bash build-sdl2-drastic.sh

deploy: build
	cp $(OUTPUT) $(SKELETON)
	@echo "Deployed to $(SKELETON)"

clean:
	rm -rf output/
	rm -rf SDL_drastic/.fb_fixed SDL_drastic/.vsync_fixed SDL_drastic/.events_fixed SDL_drastic/.hotkeys_fixed SDL_drastic/.input_fixed
	@echo "Cleaned output (run 'make clean-all' to remove SDL_drastic source)"

clean-all: clean
	rm -rf SDL_drastic/ extra-deps/
