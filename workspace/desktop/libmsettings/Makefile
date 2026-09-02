ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif
ifeq (,$(PREFIX))
$(error missing PREFIX for this toolchain)
endif

TARGET=msettings

.PHONY: build
.PHONY: clean

CC = $(CROSS_COMPILE)gcc

ifeq ($(PROFILE), 1)
CFLAGS  += -pg
LDFLAGS += -pg
endif

#CFLAGS = -g
#LDFLAGS = -ldl -lrt -s
LDFLAGS = -ldl

# macOS (desktop PLATFORM, built directly on the host, not cross-compiled in
# the Linux docker toolchain): reserve header room so package-macos.sh
# (dylibbundler + install_name_tool) can rewrite this dylib's id post-link
# without a full relink.
ifeq ($(shell uname -s),Darwin)
LDFLAGS += -Wl,-headerpad_max_install_names
endif

OPTM=-Ofast

# Per-OS build outputs: macOS (host) and Linux (docker) both build this
# desktop-only library in the same checkout, so objects/.so go under a
# per-OS subdir (packaging passes BUILD_SUBDIR=desktop-<os>-<arch>).
# Consumers link -lmsettings from $(PREFIX_LOCAL)/lib — per-OS by nature
# (host /var/tmp vs container /var/tmp) — where this installs the .so.
BUILD_SUBDIR ?= desktop
OUT = build/$(BUILD_SUBDIR)

build:
	mkdir -p "$(OUT)"
	$(CC) -c -Werror -fpic "$(TARGET).c" -o "$(OUT)/$(TARGET).o" $(CFLAGS) -Wl,--no-as-needed $(LDFLAGS)
	$(CC) -shared -o "$(OUT)/lib$(TARGET).so" "$(OUT)/$(TARGET).o" $(LDFLAGS)
	mkdir -p "$(PREFIX_LOCAL)/include"
	mkdir -p "$(PREFIX_LOCAL)/lib"
	cp "$(TARGET).h" "$(PREFIX_LOCAL)/include"
	cp "$(OUT)/lib$(TARGET).so" "$(PREFIX_LOCAL)/lib"

clean:
	rm -rf build
	rm -f *.o
	rm -f "lib$(TARGET).so"
	rm -f $(PREFIX_LOCAL)/include/$(TARGET).h
	rm -f $(PREFIX_LOCAL)/lib/lib$(TARGET).so