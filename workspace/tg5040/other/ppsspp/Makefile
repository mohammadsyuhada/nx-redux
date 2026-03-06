# PPSSPP v1.20.1 build for TG5040 (PowerVR GE8300)
#
# Run inside Docker:
#   docker run --rm \
#     -v "$(pwd)/workspace":/root/workspace \
#     -w /root/workspace/tg5040/other/ppsspp \
#     ghcr.io/loveretro/tg5040-toolchain:latest make build
#
# First build requires ffmpeg:
#   make ffmpeg && make build
#
# Output: ppsspp/build/PPSSPPSDL_stripped
# Deploy to: skeleton/EXTRAS/Emus/tg5040/PSP.pak/PPSSPPSDL

BUILD_DIR     = ppsspp/build
STRIP         = aarch64-nextui-linux-gnu-strip
PAK_DIR       = ../../../../skeleton/EXTRAS/Emus/tg5040/PSP.pak
SHARED_DIR = ../../../../skeleton/EXTRAS/shared/ppsspp

.PHONY: all ffmpeg build deploy clean

all: build

ffmpeg:
	cd ppsspp/ffmpeg && bash linux_nextui_arm64.sh

build:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake \
		-DCMAKE_TOOLCHAIN_FILE=../../toolchain-aarch64.cmake \
		-DUSING_GLES2=ON \
		-DNATIVE_GLES3=ON \
		-DUSING_EGL=OFF -DUSING_FBDEV=ON \
		-DPVR_LIBS_DIR=$(CURDIR)/pvr_libs \
		-DUSE_WAYLAND_WSI=OFF -DUSING_X11_VULKAN=OFF \
		-DVULKAN=OFF \
		-DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF -DHEADLESS=OFF \
		-DUNITTEST=OFF -DSIMULATOR=OFF -DANDROID=OFF \
		-DUSE_SYSTEM_LIBPNG=OFF -DUSE_FFMPEG=ON \
		-DEMU_OVERLAY=ON \
		-DEMU_OVERLAY_DIR=/root/nextui/workspace/all/common \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_SKIP_RPATH=ON \
		..
	cd $(BUILD_DIR) && $(MAKE) -j$$(nproc)
	$(STRIP) -o $(BUILD_DIR)/PPSSPPSDL_stripped $(BUILD_DIR)/PPSSPPSDL

deploy:
	cp $(BUILD_DIR)/PPSSPPSDL_stripped $(PAK_DIR)/PPSSPPSDL
	rm -rf $(SHARED_DIR)/assets
	cp -r ppsspp/assets $(SHARED_DIR)/assets

clean:
	rm -rf $(BUILD_DIR)
