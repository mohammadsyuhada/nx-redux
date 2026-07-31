# NXRedux

# NOTE: this runs on the host system (eg. macOS) not in a docker image
# it has to, otherwise we'd be running a docker in a docker and oof

# prevent accidentally triggering a full build with invalid calls.
# DEVICE is included because `deploy` is the default goal: without it,
# `make DEVICE=brick` with no target runs a full build and pushes to a
# connected device.
ifneq (,$(PLATFORM)$(DEVICE))
ifeq (,$(MAKECMDGOALS))
$(error found PLATFORM/DEVICE arg but no target, did you mean "make deploy DEVICE=$(DEVICE)" or "make PLATFORM=$(PLATFORM) shell"?)
endif
endif

ifeq (,$(PLATFORMS))
#PLATFORMS = miyoomini trimuismart rg35xx rg35xxplus my355 tg5040 zero28 rgb30 m17 gkdpixel my282 magicmini
PLATFORMS = tg5040 tg5050
endif

# Device variants: device=platform,overlay_res,bg_res,osd_res
# Each device produces a separate release zip. osd_res is spelled out rather
# than derived from bg_res so the OSD layer a device gets is readable here.
DEVICES = brick=tg5040,768p,1024,1024x768 brickpro=tg5040,768p,1024,1024x768 smartpro=tg5040,720p,1280,1280x720 smartpros=tg5050,720p,1280,1280x720

# Pinned upstream commits — update these when upgrading to a new version
DRASTIC_REPO=https://github.com/trngaje/advanced_drastic
DRASTIC_COMMIT=2b87c96a805758a249127ac979e0b33a64dd7199# 2026-01-21

###########################################################

BUILD_HASH:=$(shell git rev-parse --short HEAD)
BUILD_TAG:=$(shell git describe --tags --abbrev=0 2>/dev/null || echo "untagged")
BUILD_BRANCH:=$(shell (git symbolic-ref --short HEAD 2>/dev/null || git rev-parse --short HEAD) | sed 's/\//-/g')
RELEASE_TIME:=$(shell TZ=GMT date +%Y%m%d)
ifeq ($(BUILD_BRANCH),main)
  RELEASE_BETA :=
else
  RELEASE_BETA := -$(BUILD_BRANCH)
endif
ifeq ($(PLATFORM), desktop)
	TOOLCHAIN_FILE := Makefile.native
else
	TOOLCHAIN_FILE := Makefile.toolchain
endif
RELEASE_NAME ?= NXRedux-$(RELEASE_TIME)$(RELEASE_BETA)

# Extra paks to ship
VENDOR_DEST := ./build/VENDOR/Tools
PACKAGE_URL_MAPPINGS := \
	# add more URLs as needed

###########################################################

.PHONY: build

export MAKEFLAGS=--no-print-directory

ifeq (,$(PLATFORM)$(DEVICE))
deploy:
	$(error DEVICE or PLATFORM is required for deploy (e.g. make deploy DEVICE=brickpro))
else
# Payloads are per-device, not per-platform: a tg5040 zip built for smartpro
# carries only osd-smartpro, so pushing it to a Brick Pro would leave that
# device without an OSD overlay. DEVICE= names the payload directly; PLATFORM=
# resolves to that platform's first entry in DEVICES and says which it picked.
deploy: setup $(PLATFORMS) special package
	@dev="$(DEVICE)"; \
	if [ -n "$$dev" ] && [ -n "$(PLATFORM)" ]; then \
		echo "DEVICE=$$dev overrides PLATFORM=$(PLATFORM) (ignoring PLATFORM)"; \
	fi; \
	if [ -z "$$dev" ]; then \
		for entry in $(DEVICES); do \
			d=$$(echo $$entry | cut -d= -f1); \
			p=$$(echo $$entry | cut -d= -f2 | cut -d, -f1); \
			if [ "$$p" = "$(PLATFORM)" ]; then dev=$$d; break; fi; \
		done; \
		if [ -z "$$dev" ]; then echo "no device in DEVICES for PLATFORM=$(PLATFORM)" >&2; exit 1; fi; \
		echo "PLATFORM=$(PLATFORM) -> deploying '$$dev' (pass DEVICE= to choose another)"; \
	fi; \
	zip=./build/BASE/MinUI-$$dev.zip; \
	if [ ! -f "$$zip" ]; then echo "missing $$zip (unknown DEVICE '$$dev'?)" >&2; exit 1; fi; \
	adb push "$$zip" /mnt/SDCARD/MinUI.zip && adb shell reboot
endif

all: setup $(PLATFORMS) special package done
	
shell:
	make -f $(TOOLCHAIN_FILE) PLATFORM=$(PLATFORM)

name: 
	@echo $(RELEASE_NAME)

build:
	# ----------------------------------------------------
	make build -f $(TOOLCHAIN_FILE) PLATFORM=$(PLATFORM) COMPILE_CORES=$(COMPILE_CORES)
	# ----------------------------------------------------

build-cores:
	make build-cores -f $(TOOLCHAIN_FILE) PLATFORM=$(PLATFORM) COMPILE_CORES=true
	# ----------------------------------------------------

cores-json:
	@cat workspace/$(PLATFORM)/cores/Makefile | grep ^CORES | cut -d' ' -f2 | jq  --raw-input .  | jq --slurp -cM .

build-core:
ifndef CORE
	$(error CORE is not set)
endif
	make build-core -f $(TOOLCHAIN_FILE) PLATFORM=$(PLATFORM) COMPILE_CORES=true CORE=$(CORE)

system:
	make -f ./workspace/$(PLATFORM)/platform/Makefile.copy PLATFORM=$(PLATFORM)
	
	# populate system
ifneq ($(PLATFORM), desktop)
	cp ./workspace/$(PLATFORM)/keymon/keymon.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/$(PLATFORM)/sleepmon/sleepmon.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/syncsettings/build/$(PLATFORM)/syncsettings.elf ./build/SYSTEM/$(PLATFORM)/bin/
	# taskset: used for CPU-affinity pinning by N64.pak, DC.pak, and (tg5050)
	# PS.pak launch.sh. Built from source here so a full `make deploy` stays
	# reproducible; see workspace/all/taskset/Makefile for why it must be
	# dynamically linked (NOT -static) on this toolchain/kernel combination.
	cp ./workspace/all/taskset/build/$(PLATFORM)/taskset ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/audiomon/build/$(PLATFORM)/audiomon.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/show2/build/$(PLATFORM)/show2.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/screenshot/build/$(PLATFORM)/screenshot.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/screenrecorder/build/$(PLATFORM)/screenrecorder.elf ./build/SYSTEM/$(PLATFORM)/bin/

	# game time tracking
	cp ./workspace/all/libgametimedb/build/$(PLATFORM)/libgametimedb.so ./build/SYSTEM/$(PLATFORM)/lib
	cp ./workspace/all/gametimectl/build/$(PLATFORM)/gametimectl.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/gametime/build/$(PLATFORM)/gametime.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/Game\ Tracker.pak/
endif
	cp ./workspace/$(PLATFORM)/libmsettings/libmsettings.so ./build/SYSTEM/$(PLATFORM)/lib
	cp ./workspace/all/nextui/build/$(PLATFORM)/nextui.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/minarch/build/$(PLATFORM)/minarch.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/nextval/build/$(PLATFORM)/nextval.elf ./build/SYSTEM/$(PLATFORM)/bin/
	cp ./workspace/all/settings/build/$(PLATFORM)/settings.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/Settings.pak/
	cp ./workspace/all/musicplayer/build/$(PLATFORM)/musicplayer.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/Music\ Player.pak/
	cp ./workspace/all/mediaplayer/build/$(PLATFORM)/mediaplayer.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/Media\ Player.pak/
	cp ./workspace/all/portmaster/build/$(PLATFORM)/portmaster.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/PortMaster.pak/
	cp ./workspace/all/sync/build/$(PLATFORM)/sync.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/Device\ Sync.pak/
	cp ./workspace/all/scraper/build/$(PLATFORM)/scraper.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/Artwork\ Manager.pak/
ifneq (,$(filter $(PLATFORM),tg5040 tg5050))
ifeq ($(PLATFORM), tg5040)
	# Limbo fix
	cp ./workspace/$(PLATFORM)/poweroff_next/build/$(PLATFORM)/poweroff_next.elf ./build/SYSTEM/$(PLATFORM)/bin/poweroff_next
endif
	# Audio resampling
	cp ./workspace/all/minarch/build/$(PLATFORM)/libsamplerate.* ./build/SYSTEM/$(PLATFORM)/lib/

	# fdk-aac for music player
	cp ./workspace/all/musicplayer/include/fdk_aac/lib/libfdk-aac.so* ./build/SYSTEM/$(PLATFORM)/lib/

	# ROM decompression and SRM support
	cp ./workspace/all/minarch/build/$(PLATFORM)/libzip.* ./build/SYSTEM/$(PLATFORM)/lib/
	cp ./workspace/all/minarch/build/$(PLATFORM)/libbz2.* ./build/SYSTEM/$(PLATFORM)/lib/
	cp ./workspace/all/minarch/build/$(PLATFORM)/liblzma.* ./build/SYSTEM/$(PLATFORM)/lib/
	cp ./workspace/all/minarch/build/$(PLATFORM)/libzstd.* ./build/SYSTEM/$(PLATFORM)/lib/

	# libchdr for RetroAchievements CHD hashing
	cp ./workspace/all/minarch/build/$(PLATFORM)/libchdr.so.* ./build/SYSTEM/$(PLATFORM)/lib/

	# RetroAchievements tools pak
	cp ./workspace/all/ratools/build/$(PLATFORM)/ratools.elf ./build/SYSTEM/$(PLATFORM)/paks/Tools/RetroAchievements.pak/

	# Dreamcast netplay pre-launch wizard (run bare off PATH by DC.pak/launch.sh);
	# gated here, not with the other SYSTEM bin copies, because it is only built
	# for tg5040/tg5050 (see workspace/Makefile).
	cp ./workspace/all/netplay-wizard/build/$(PLATFORM)/netplay.elf ./build/SYSTEM/$(PLATFORM)/bin/

	# Pre-launch emulator options editor (run bare off PATH by options.sh and
	# the Emulator Settings tool)
	cp ./workspace/all/emu-options/build/$(PLATFORM)/options.elf ./build/SYSTEM/$(PLATFORM)/bin/

ifeq ($(PLATFORM), tg5040)
	# liblz4 for Rewind support
	cp -L ./workspace/all/minarch/build/$(PLATFORM)/liblz4.so.1 ./build/SYSTEM/$(PLATFORM)/lib/
endif
endif

ifeq ($(PLATFORM), desktop)
cores:
	# stock cores
	#cp ./workspace/$(PLATFORM)/cores/output/gambatte_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	#cp ./workspace/$(PLATFORM)/cores/output/gpsp_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	#cp ./workspace/$(PLATFORM)/cores/output/mgba_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/MGBA.pak
else
cores: # TODO: can't assume every platform will have the same stock cores (platform should be responsible for copy too)
	# stock cores
	cp ./workspace/$(PLATFORM)/cores/output/fceumm_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	cp ./workspace/$(PLATFORM)/cores/output/gambatte_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	cp ./workspace/$(PLATFORM)/cores/output/gpsp_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	cp ./workspace/$(PLATFORM)/cores/output/picodrive_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	cp ./workspace/$(PLATFORM)/cores/output/snes9x_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	cp ./workspace/$(PLATFORM)/cores/output/pcsx_rearmed_libretro.so ./build/SYSTEM/$(PLATFORM)/cores
	
	# extras
	cp ./workspace/$(PLATFORM)/cores/output/a5200_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/A5200.pak
	cp ./workspace/$(PLATFORM)/cores/output/prosystem_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/A7800.pak
	cp ./workspace/$(PLATFORM)/cores/output/stella2014_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/A2600.pak
	cp ./workspace/$(PLATFORM)/cores/output/handy_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/LYNX.pak
	cp ./workspace/$(PLATFORM)/cores/output/fake08_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/P8.pak
	cp ./workspace/$(PLATFORM)/cores/output/mgba_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/MGBA.pak
	cp ./workspace/$(PLATFORM)/cores/output/mgba_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/SGB.pak
	cp ./workspace/$(PLATFORM)/cores/output/mednafen_pce_fast_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/PCE.pak
	cp ./workspace/$(PLATFORM)/cores/output/pokemini_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/PKM.pak
	cp ./workspace/$(PLATFORM)/cores/output/race_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/NGP.pak
	cp ./workspace/$(PLATFORM)/cores/output/race_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/NGPC.pak
	cp ./workspace/$(PLATFORM)/cores/output/fbneo_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/FBN.pak
	cp ./workspace/$(PLATFORM)/cores/output/mednafen_supafaust_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/SUPA.pak
	cp ./workspace/$(PLATFORM)/cores/output/mednafen_vb_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/VB.pak
	cp ./workspace/$(PLATFORM)/cores/output/cap32_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/CPC.pak
	cp ./workspace/$(PLATFORM)/cores/output/puae2021_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/PUAE.pak
	cp ./workspace/$(PLATFORM)/cores/output/prboom_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/PRBOOM.pak
	cp ./workspace/$(PLATFORM)/cores/output/vice_x64_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/C64.pak
	cp ./workspace/$(PLATFORM)/cores/output/vice_x128_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/C128.pak
	cp ./workspace/$(PLATFORM)/cores/output/vice_xplus4_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/PLUS4.pak
	cp ./workspace/$(PLATFORM)/cores/output/vice_xpet_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/PET.pak
	cp ./workspace/$(PLATFORM)/cores/output/vice_xvic_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/VIC.pak
	cp ./workspace/$(PLATFORM)/cores/output/bluemsx_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/MSX.pak
	cp ./workspace/$(PLATFORM)/cores/output/gearcoleco_libretro.so ./build/SYSTEM/$(PLATFORM)/paks/Emus/COLECO.pak
endif

common: build system cores
	
format:
	git ls-files '*.c' '*.h' | xargs clang-format -i

compile-commands:
	@echo "Generating compile_commands.json..."
	# clangd shim: the device build installs rcheevos headers into the
	# toolchain prefix as <rcheevos/*.h>; recreate that layout with symlinks
	@mkdir -p .clangd-shim/rcheevos
	@for h in workspace/all/minarch/rcheevos/src/include/*.h workspace/all/minarch/rcheevos/src/src/rc_libretro.h; do \
		[ -f "$$h" ] && ln -sf "$(CURDIR)/$$h" .clangd-shim/rcheevos/ || true; \
	done
	# clangd shim: Linux-only headers (dbus, libudev, kernel uapi) only exist
	# in the ARM toolchain sysroot; extract once from the docker image.
	# Hand-written stubs for glibc APIs live in scripts/clangd/include.
	@if [ ! -f .clangd-shim/.sysroot-extracted ] && command -v docker >/dev/null 2>&1; then \
		mkdir -p .clangd-shim/dbus; \
		docker run --rm -v "$(CURDIR)/.clangd-shim":/out ghcr.io/loveretro/tg5050-toolchain:latest /bin/bash -c \
			'SYSROOT=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr; \
			 cp $$SYSROOT/include/dbus-1.0/dbus/*.h /out/dbus/ && \
			 cp $$SYSROOT/lib/dbus-1.0/include/dbus/dbus-arch-deps.h /out/dbus/ && \
			 cp $$SYSROOT/include/libudev.h /out/ && \
			 cp -r $$SYSROOT/include/linux $$SYSROOT/include/asm $$SYSROOT/include/asm-generic $$SYSROOT/include/tinyalsa /out/' \
			2>/dev/null; \
		chmod -R u+w .clangd-shim 2>/dev/null; \
		[ -f .clangd-shim/linux/input.h ] && touch .clangd-shim/.sysroot-extracted || true; \
	fi
	# clangd shim: GLES3 headers for emu_overlay_sdl.c (not shipped by any
	# toolchain or the macOS SDK); fetched from the Khronos registry
	@if [ ! -f .clangd-shim/GLES3/gl3.h ] && command -v curl >/dev/null 2>&1; then \
		mkdir -p .clangd-shim/GLES3 .clangd-shim/KHR; \
		curl -sf --max-time 30 https://registry.khronos.org/OpenGL/api/GLES3/gl3.h -o .clangd-shim/GLES3/gl3.h || true; \
		curl -sf --max-time 30 https://registry.khronos.org/OpenGL/api/GLES3/gl3platform.h -o .clangd-shim/GLES3/gl3platform.h || true; \
		curl -sf --max-time 30 https://registry.khronos.org/EGL/api/KHR/khrplatform.h -o .clangd-shim/KHR/khrplatform.h || true; \
	fi
	@echo '[' > compile_commands.json
	@first=1; \
	git ls-files '*.c' | \
	grep -v 'cores/src' | \
		grep -v 'libretro-common' | \
		grep -v 'libchdr' | \
		grep -v 'rcheevos/src' | \
		grep -v 'mediaplayer/include/ffplay' | \
	while read -r file; do \
		extra_flags=""; \
		case "$$file" in \
			workspace/all/include/*) \
				extra_flags="\"-I$(CURDIR)/workspace/all/include\", \"-I$(CURDIR)/workspace/all/include/mbedtls_lib\", \"-DMBEDTLS_CONFIG_FILE=<mbedtls_config.h>\", "; \
				;; \
			workspace/all/musicplayer/include/libopus/*) \
				extra_flags="\"-I$(CURDIR)/workspace/all/musicplayer/include/libopus\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libopus/include\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libopus/src\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libopus/celt\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libopus/silk\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libopus/silk/float\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libogg\", \"-DOPUS_BUILD\", \"-DVAR_ARRAYS\", \"-DHAVE_LRINTF\", "; \
				;; \
			workspace/all/musicplayer/*) \
				extra_flags="\"-I$(CURDIR)/workspace/all/musicplayer\", \"-I$(CURDIR)/workspace/all/musicplayer/include\", \"-I$(CURDIR)/workspace/all/musicplayer/include/fdk_aac\", \"-I$(CURDIR)/workspace/all/musicplayer/include/yxml\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libogg\", \"-I$(CURDIR)/workspace/all/musicplayer/include/libopus/include\", \"-I$(CURDIR)/workspace/all/musicplayer/include/opusfile/include\", \"-I$(CURDIR)/workspace/all/include\", \"-I$(CURDIR)/workspace/all/include/mbedtls_lib\", \"-I$(CURDIR)/workspace/all/musicplayer/audio\", \"-DMBEDTLS_CONFIG_FILE=<mbedtls_config.h>\", \"-DOPUS_BUILD\", \"-DHAVE_LRINTF\", \"-DOP_DISABLE_HTTP\", \"-DOP_DISABLE_FLOAT_API\", "; \
				;; \
			workspace/all/mediaplayer/*) \
				extra_flags="\"-I$(CURDIR)/workspace/all/mediaplayer\", \"-I$(CURDIR)/workspace/all/include\", \"-I$(CURDIR)/workspace/all/include/mbedtls_lib\", \"-DMBEDTLS_CONFIG_FILE=<mbedtls_config.h>\", \"-DOPUS_BUILD\", \"-DHAVE_LRINTF\", \"-DOP_DISABLE_HTTP\", \"-DOP_DISABLE_FLOAT_API\", "; \
				;; \
		esac; \
		if [ "$$first" = "1" ]; then first=0; else echo ',' >> compile_commands.json; fi; \
		printf '  {"directory": "%s", "file": "%s/%s", "arguments": ["clang", "-std=gnu99", "-DUSE_SDL2", "-DUSE_GLES", "-DGL_GLEXT_PROTOTYPES", "-DPLATFORM=\\"tg5040\\"", "-DHAS_CHEEVOS", "-DRC_CLIENT_SUPPORTS_HASH", "-DRC_DISABLE_LUA", "-DBUILD_DATE=\\"dev\\"", "-DBUILD_HASH=\\"dev\\"", %s"-I%s/workspace/all/common", "-I%s/workspace/all/common/ui", "-I%s/workspace/all/nextui", "-I%s/workspace/all/minarch", "-I%s/workspace/all/minarch/libretro-common/include", "-I%s/workspace/all/netplay", "-I%s/workspace/all/settings", "-I%s/workspace/all/libgametimedb", "-I%s/.clangd-shim", "-I%s/scripts/clangd/include", "-I%s/workspace/tg5040/platform", "-I%s/workspace/tg5050/platform", "-I%s/workspace/desktop/platform", "-I%s/workspace/tg5040/libmsettings", "-I%s/workspace/tg5050/libmsettings", "-I%s/workspace/desktop/libmsettings", "-I/opt/homebrew/include", "-c", "%s/%s"]}' \
			"$(CURDIR)" "$(CURDIR)" "$$file" \
			"$$extra_flags" \
			"$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" "$(CURDIR)" \
			"$(CURDIR)" "$$file" >> compile_commands.json; \
	done
	@echo '' >> compile_commands.json
	@echo ']' >> compile_commands.json
	@echo "Done. Generated compile_commands.json"

clean:
	rm -rf ./build
	make clean -f $(TOOLCHAIN_FILE) PLATFORM=$(PLATFORM) COMPILE_CORES=$(COMPILE_CORES)

setup: name
	# ----------------------------------------------------
	# make sure we're running in an input device
	tty -s || echo "No tty detected"
	
	# ready fresh build
	rm -rf ./build
	mkdir -p ./releases
	cp -R ./skeleton ./build
	# skeleton/SYSTEM/osd is layered OSD *source*, assembled per-device at
	# package time by scripts/assemble-osd.sh. It is never shipped verbatim,
	# so keep build/ a faithful picture of what ships.
	rm -rf ./build/SYSTEM/osd
	# stock OSD zips are consumed per-device at package time from skeleton/
	# (manifest .md5 files must not ship either)
	rm -rf ./build/SYSTEM/osd-stock

	# Fetch advanced drastic emulator (pinned to $(DRASTIC_COMMIT))
	@echo "Fetching advanced drastic..."
	curl -sL $(DRASTIC_REPO)/archive/$(DRASTIC_COMMIT).tar.gz | tar xz -C /tmp
	mkdir -p ./build/BASE/Emus/shared/drastic
	cp -Rf /tmp/advanced_drastic-$(DRASTIC_COMMIT)/* ./build/BASE/Emus/shared/drastic/
	rm -rf /tmp/advanced_drastic-$(DRASTIC_COMMIT)
	rm -f ./build/BASE/Emus/shared/drastic/history.md ./build/BASE/Emus/shared/drastic/launch.sh
	rm -rf ./build/BASE/Emus/shared/drastic/images
	# Overlay custom drastic resources (bg, fonts) on top of upstream
	cp -Rf ./skeleton/BASE/Emus/shared/drastic/resources/ ./build/BASE/Emus/shared/drastic/resources/

	# remove authoring detritus
	cd ./build && find . -type f -name '.keep' -delete
	cd ./build && find . -type f -name '*.meta' -delete
	echo $(BUILD_HASH) > ./workspace/hash.txt
	
	# copy readmes to workspace so we can use Linux fmt instead of host's
	mkdir -p ./workspace/readmes
	cp ./skeleton/BASE/README.txt ./workspace/readmes/BASE-in.txt
	
done:
	# say "done" 2>/dev/null || true

special:
	# setup miyoomini/trimui/magicx family .tmp_update in BOOT
	mv ./build/BOOT/common ./build/BOOT/.tmp_update
#	mv ./build/BOOT/miyoo ./build/BASE/
	mv ./build/BOOT/trimui ./build/BASE/
#	mv ./build/BOOT/magicx ./build/BASE/
#	cp -R ./build/BOOT/.tmp_update ./build/BASE/miyoo/app/
	cp -R ./build/BOOT/.tmp_update ./build/BASE/trimui/app/
#	cp -R ./build/BOOT/.tmp_update ./build/BASE/magicx/
#	cp -R ./build/BASE/miyoo ./build/BASE/miyoo354
#	cp -R ./build/BASE/miyoo ./build/BASE/miyoo355
#ifneq (,$(findstring my355, $(PLATFORMS)))
#	cp -R ./workspace/my355/init ./build/BASE/miyoo355/app/my355
#	cp -r ./workspace/my355/other/squashfs/output/* ./build/BASE/miyoo355/app/my355/payload/
#endif

tidy:
	@for dev_entry in $(DEVICES); do \
		dev=$$(echo $$dev_entry | cut -d= -f1); \
		rm -f releases/$(RELEASE_NAME)-$$dev.zip; \
	done
	# ----------------------------------------------------
	# copy update from merged platform to old pre-merge platform bin so old cards update properly

package: tidy
	# ----------------------------------------------------
	# zip up build

	# move formatted readmes from workspace to build
	cp ./workspace/readmes/BASE-out.txt ./build/BASE/README.txt
	rm -rf ./workspace/readmes

	cd ./build/SYSTEM && printf "%s\n%s\n%s\n" "$(RELEASE_NAME)" "$(BUILD_HASH)" "$(BUILD_TAG)" > version.txt
	./commits.sh > ./build/SYSTEM/commits.txt
	cd ./build && find . -type f -name '.DS_Store' -delete

	# Fetch, rename, and stage vendored packages
	mkdir -p $(VENDOR_DEST)
	@for entry in $(PACKAGE_URL_MAPPINGS); do \
		url=$$(echo $$entry | awk '{print $$1}'); \
		target=$$(echo $$entry | awk '{print $$2}'); \
		echo "Downloading $$url → $(VENDOR_DEST)/$$target"; \
		curl -Ls -o "$(VENDOR_DEST)/$$target" "$$url"; \
	done

	# Move renamed .pakz files into base folder
	mkdir -p ./build/BASE
	-mv $(VENDOR_DEST)/* ./build/BASE/ 2>/dev/null; true

	# --- Per-device packaging ---
	# DEVICES format: device=platform,overlay_res,bg_res,osd_res
	@for dev_entry in $(DEVICES); do \
		dev=$$(echo $$dev_entry | cut -d= -f1); \
		dev_config=$$(echo $$dev_entry | cut -d= -f2); \
		plat=$$(echo $$dev_config | cut -d, -f1); \
		overlay_res=$$(echo $$dev_config | cut -d, -f2); \
		bg_res=$$(echo $$dev_config | cut -d, -f3); \
		osd_res=$$(echo $$dev_config | cut -d, -f4); \
		\
		echo "# ===== Packaging $$dev (platform=$$plat, overlays=$$overlay_res, bg=$$bg_res, osd=$$osd_res) ====="; \
		rm -rf ./build/PAYLOAD-$$dev; \
		mkdir -p ./build/PAYLOAD-$$dev/.system; \
		\
		echo "  assembling .system (contents merge from $$plat)"; \
		cp -R ./build/SYSTEM/$$plat/. ./build/PAYLOAD-$$dev/.system/; \
		echo "  installing legacy-boot compat shims"; \
		mkdir -p ./build/PAYLOAD-$$dev/.system/$$plat/bin "./build/PAYLOAD-$$dev/.system/$$plat/paks/MinUI.pak"; \
		cp ./workspace/$$plat/install/install-shim.sh ./build/PAYLOAD-$$dev/.system/$$plat/bin/install.sh; \
		cp ./workspace/$$plat/install/minui-launch-shim.sh "./build/PAYLOAD-$$dev/.system/$$plat/paks/MinUI.pak/launch.sh"; \
		cp -R ./build/SYSTEM/res      ./build/PAYLOAD-$$dev/.system/res; \
		cp -R ./build/SYSTEM/shared   ./build/PAYLOAD-$$dev/.system/shared; \
		cp ./build/SYSTEM/version.txt ./build/PAYLOAD-$$dev/.system/version.txt; \
		cp ./build/SYSTEM/commits.txt ./build/PAYLOAD-$$dev/.system/commits.txt; \
		\
		echo "  assembling OSD ($$osd_res)"; \
		./scripts/assemble-osd.sh $$dev $$plat $$osd_res ./build/PAYLOAD-$$dev/.system || exit 1; \
		\
		if [ -f ./skeleton/SYSTEM/osd-stock/$$dev.zip ]; then \
			echo "  assembling osd-stock ($$dev)"; \
			mkdir -p ./build/PAYLOAD-$$dev/.system/osd-stock; \
			cp ./skeleton/SYSTEM/osd-stock/$$dev.zip ./build/PAYLOAD-$$dev/.system/osd-stock/ || exit 1; \
		else \
			echo "  (no stock OSD tree for $$dev; Settings restore stays hidden)"; \
		fi; \
		\
		echo "  assembling .tmp_update"; \
		cp -R ./build/BOOT/.tmp_update ./build/PAYLOAD-$$dev/.tmp_update; \
		\
		echo "  assembling Emus/shared"; \
		mkdir -p ./build/PAYLOAD-$$dev/Emus; \
		cp -R ./build/BASE/Emus/shared ./build/PAYLOAD-$$dev/Emus/shared; \
		\
		echo "  assembling Tools/.media ($$bg_res)"; \
		mkdir -p ./build/PAYLOAD-$$dev/Tools/.media; \
		cp ./build/BASE/Tools/.media/bg-$$bg_res.png ./build/PAYLOAD-$$dev/Tools/.media/bg.png; \
		\
		echo "  creating device marker $$plat-$$dev"; \
		touch ./build/PAYLOAD-$$dev/$$plat-$$dev; \
		\
		echo "  creating MinUI.zip"; \
		cd ./build/PAYLOAD-$$dev && zip -r MinUI.zip .system .tmp_update Emus Tools $$plat-$$dev && cd ../..; \
		cp ./build/PAYLOAD-$$dev/MinUI.zip ./build/BASE/MinUI-$$dev.zip; \
		\
		echo "  resolving overlays for $$dev ($$overlay_res)"; \
		for overlay_root in ./build/BASE/Overlays; do \
			if [ -d "$$overlay_root" ]; then \
				find "$$overlay_root" -mindepth 1 -maxdepth 1 -type d | while read emu_dir; do \
					if [ -d "$$emu_dir/$$overlay_res" ]; then \
						cp -f "$$emu_dir/$$overlay_res"/* "$$emu_dir/"; \
					fi; \
				done; \
			fi; \
		done; \
		\
		echo "  resolving bg images for $$dev ($$bg_res)"; \
		find ./build/BASE/Collections ./build/BASE/Favorites \
			"./build/BASE/Recently Played" ./build/BASE/Roms \
			-path '*/.media/bg-'"$$bg_res"'.png' 2>/dev/null \
		| while read f; do \
			cp "$$f" "$$(dirname "$$f")/bg.png"; \
		done; \
		\
		echo "  creating release zip"; \
		cd ./build/BASE && zip -r ../../releases/$(RELEASE_NAME)-$$dev.zip \
			Bios Cheats Collections Favorites Music Overlays \
			"Recently Played" Roms Saves Shaders trimui *.pakz README.txt \
			-x '*/bg-*.png' -x '*/720p/*' -x '*/768p/*' \
			&& cd ../..; \
		cd ./build/PAYLOAD-$$dev && zip -r ../../releases/$(RELEASE_NAME)-$$dev.zip MinUI.zip && cd ../..; \
		if [ -d ./build/PAKZ/$$plat ]; then \
			cd ./build/PAKZ/$$plat && zip -r ../../../releases/$(RELEASE_NAME)-$$dev.zip *.pakz && cd ../../..; \
		fi; \
		\
		echo "  cleaning up generated files"; \
		find ./build/BASE/Collections ./build/BASE/Favorites \
			"./build/BASE/Recently Played" ./build/BASE/Roms \
			-name "bg.png" -path '*/.media/*' -delete 2>/dev/null; \
		for overlay_root in ./build/BASE/Overlays; do \
			if [ -d "$$overlay_root" ]; then \
				find "$$overlay_root" -mindepth 1 -maxdepth 1 -type d | while read emu_dir; do \
					if [ -d "$$emu_dir/720p" ] || [ -d "$$emu_dir/768p" ]; then \
						for res_file in "$$emu_dir"/*.png; do \
							[ -f "$$res_file" ] && base=$$(basename "$$res_file") && \
							if [ -f "$$emu_dir/720p/$$base" ] || [ -f "$$emu_dir/768p/$$base" ]; then \
								rm -f "$$res_file"; \
							fi; \
						done; \
					fi; \
				done; \
			fi; \
		done; \
		\
		rm -rf ./build/PAYLOAD-$$dev; \
		echo "# ===== Done: $$dev ====="; \
	done
	
###########################################################

.DEFAULT:
	# ----------------------------------------------------
	# $@
	@echo "$(PLATFORMS)" | grep -q "\b$@\b" && (make common PLATFORM=$@) || (exit 1)
	
