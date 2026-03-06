# PPSSPP v1.20.1 build for TG5050 (Mali-G57)
#
# Run inside Docker:
#   docker run --rm \
#     -v "$(pwd)/workspace":/root/workspace \
#     -w /root/workspace/tg5050/other/ppsspp \
#     ghcr.io/loveretro/tg5050-toolchain:latest make build
#
# First build requires ffmpeg:
#   make ffmpeg && make build
#
# Output: ppsspp/build/PPSSPPSDL_stripped
# Deploy to: skeleton/EXTRAS/Emus/tg5050/PSP.pak/PPSSPPSDL

BUILD_DIR   = ppsspp/build
OVERLAY_DIR = /root/workspace/all/common
STRIP       = aarch64-nextui-linux-gnu-strip
PAK_DIR       = ../../../../skeleton/EXTRAS/Emus/tg5050/PSP.pak
SHARED_DIR = ../../../../skeleton/EXTRAS/shared/ppsspp
LIBC_INC    = /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/include
LIBC_LIB    = /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/lib
EXTRA_HDRS  = /root/workspace/all/include

.PHONY: all ffmpeg build deploy clean install-headers

all: build

ffmpeg:
	cd ppsspp/ffmpeg && bash linux_nextui_arm64.sh

# TG5050 toolchain is missing GLES3/EGL/KHR headers and libEGL symlink
install-headers:
	cp -rn $(EXTRA_HDRS)/GLES3 $(LIBC_INC)/ 2>/dev/null || true
	cp -rn $(EXTRA_HDRS)/EGL $(LIBC_INC)/ 2>/dev/null || true
	cp -rn $(EXTRA_HDRS)/KHR $(LIBC_INC)/ 2>/dev/null || true
	ln -sf libmali.so $(LIBC_LIB)/libEGL.so 2>/dev/null || true

build: install-headers
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake \
		-DCMAKE_TOOLCHAIN_FILE=../../toolchain-aarch64.cmake \
		-DUSING_GLES2=ON \
		-DUSING_EGL=ON -DUSING_FBDEV=ON \
		-DUSE_WAYLAND_WSI=OFF -DUSING_X11_VULKAN=OFF -DVULKAN=OFF \
		-DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF -DHEADLESS=OFF \
		-DUNITTEST=OFF -DSIMULATOR=OFF -DANDROID=OFF \
		-DUSE_SYSTEM_LIBPNG=OFF -DFT_DISABLE_PNG=ON -DUSE_FFMPEG=ON \
		-DEMU_OVERLAY=ON -DEMU_OVERLAY_DIR=$(OVERLAY_DIR) \
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
