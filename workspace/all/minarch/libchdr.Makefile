# Makefile wrapper for libchdr (CMake-based project)
# Builds libchdr as a shared library for the target platform

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=tg5040 make)
endif

# Per-OS build outputs for desktop (see workspace/all/minarch/Makefile);
# devices keep the default.
BUILD_SUBDIR ?= $(PLATFORM)
BUILD_DIR = build/$(BUILD_SUBDIR)

# Cross-compilation settings (only for non-desktop platforms)
# Uses the toolchain file provided by the build container
ifneq ($(PLATFORM),desktop)
CMAKE_EXTRA = -DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TOOLCHAIN_FILE)
endif

.PHONY: all build clean

all: build install

build: $(BUILD_DIR)/libchdr.so

$(BUILD_DIR)/libchdr.so: | $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ../.. \
		-DBUILD_SHARED_LIBS=ON \
		-DINSTALL_STATIC_LIBS=OFF \
		-DCMAKE_BUILD_TYPE=Release \
		-DWITH_SYSTEM_ZLIB=OFF \
		-DCMAKE_SHARED_LIBRARY_SUFFIX_C=".so" \
		$(CMAKE_EXTRA)
	cd $(BUILD_DIR) && make -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
	@# Copy the .so to expected location (cmake may put it in a subdir)
	@if [ -f $(BUILD_DIR)/libchdr.so ]; then \
		echo "libchdr.so built successfully"; \
	elif [ -f $(BUILD_DIR)/src/libchdr.so ]; then \
		cp $(BUILD_DIR)/src/libchdr.so $(BUILD_DIR)/; \
	fi

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

install: $(BUILD_DIR)/libchdr.so
	mkdir -p "$(PREFIX_LOCAL)/lib"
	cp $(BUILD_DIR)/libchdr.so "$(PREFIX_LOCAL)/lib/"
	cp build/$(PLATFORM)/libchdr.so "$(PREFIX_LOCAL)/lib/libchdr.0.so" 2>/dev/null || true
	mkdir -p "$(PREFIX_LOCAL)/include/libchdr"
	cp -r include/libchdr/* "$(PREFIX_LOCAL)/include/libchdr/"

clean:
	rm -rf $(BUILD_DIR)
