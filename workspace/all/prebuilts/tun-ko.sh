#!/bin/bash
#
# Build the tun.ko that ships in SYSTEM/tg5050/lib/modules. The USB cable
# netplay link (all/usblink) needs /dev/net/tun to route IP over the cable,
# but the tg5050 stock kernel is built with CONFIG_TUN off and no module.
# usblink.elf insmods this on demand.
#
# tg5050-only: the tg5040 kernel already has TUN. Any other PLATFORM is
# refused.
#
# The module is mainline drivers/net/tun.c from linux-5.15.147, built as an
# external module against the device's own config (committed as
# tg5050/other/kernel/config-5.15.147, from /proc/config.gz). The kernel has
# no MODVERSIONS and no module signing, so all that has to match is the
# vermagic string, which the script checks before copying the result out.
# The device kernel was built with gcc 10.3; a different toolchain gcc only
# changes CONFIG_CC_VERSION_TEXT, not the vermagic.
#
# Runs INSIDE the toolchain container:
#   docker run --rm -v "$PWD/workspace":/root/workspace \
#     ghcr.io/loveretro/tg5050-toolchain:latest /bin/bash -c \
#     '. ~/.bashrc && cd /root/workspace && PLATFORM=tg5050 bash all/prebuilts/tun-ko.sh'
#
# Output: all/prebuilts/output/tg5050/SYSTEM/tg5050/lib/modules/tun.ko
# (debug info stripped). Scratch lives in /tmp/tun-ko-build inside the
# container (the kernel tarball alone is ~126 MB).
#
set -e

KVER=5.15.147
KURL="https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-${KVER}.tar.xz"
KSHA256=56c1e65625d201db431efda7a3816e7b424071e7cb0245b2ba594d15b1fdfcd4
VERMAGIC="5.15.147 SMP preempt mod_unload aarch64"

if [ "$PLATFORM" != "tg5050" ]; then
    echo "tun.ko is tg5050-only (got '${PLATFORM}')" >&2
    exit 1
fi
if [ -z "$CROSS_COMPILE" ]; then
    echo "CROSS_COMPILE is not set: run this inside the toolchain container" >&2
    exit 1
fi

WS=$(pwd) # /root/workspace
CFG="$WS/tg5050/other/kernel/config-${KVER}"
OUT="$WS/all/prebuilts/output/tg5050/SYSTEM/tg5050/lib/modules"
B=/tmp/tun-ko-build

# The image's env is set up for userland cross builds (CC, LD, ARCH=aarch64,
# PKG_CONFIG_* pointing into the target sysroot). Kbuild picks its own
# compilers from CROSS_COMPILE, but host tools (scripts/extract-cert, via
# pkg-config libcrypto) would get target-sysroot flags and fail to link, so
# drop all of that for the kernel make.
KMAKE="env -u CC -u CXX -u CPP -u LD -u AR -u AS -u ARCH -u PKG_CONFIG_PATH \
    -u PKG_CONFIG_SYSROOT_DIR -u CMAKE_TOOLCHAIN_FILE \
    make ARCH=arm64 CROSS_COMPILE=$CROSS_COMPILE"

# Kernel build host tools and headers the toolchain image may lack
NEED=
for t in flex bison bc; do command -v $t >/dev/null || NEED=1; done
[ -f /usr/include/openssl/bio.h ] && [ -f /usr/include/gelf.h ] || NEED=1
if [ -n "$NEED" ]; then
    apt-get update -qq
    apt-get install -y -qq flex bison bc libelf-dev libssl-dev
fi

mkdir -p "$B"
cd "$B"
if [ ! -f "linux-${KVER}.tar.xz" ]; then
    if command -v wget >/dev/null; then
        wget -q "$KURL"
    else
        curl -sSfLO "$KURL"
    fi
fi
echo "${KSHA256}  linux-${KVER}.tar.xz" | sha256sum -c -
[ -d "linux-${KVER}" ] || tar xf "linux-${KVER}.tar.xz"

cd "linux-${KVER}"
cp "$CFG" .config
./scripts/config --module TUN
$KMAKE olddefconfig
$KMAKE -j"$(nproc)" modules_prepare

# Build only tun.c, out of tree: no full kernel build needed. Without a full
# build there is no Module.symvers, so modpost cannot see the kernel's
# exports and would fail on every import; KBUILD_MODPOST_WARN turns that into
# warnings (the kernel resolves them at insmod, and all are exported there).
rm -rf "$B/mod"
mkdir -p "$B/mod"
cp drivers/net/tun.c "$B/mod/"
echo 'obj-m := tun.o' >"$B/mod/Makefile"
$KMAKE M="$B/mod" KBUILD_MODPOST_WARN=1 modules

"${CROSS_COMPILE}strip" --strip-debug "$B/mod/tun.ko"
if ! strings "$B/mod/tun.ko" | grep -qx "vermagic=${VERMAGIC}"; then
    echo "vermagic mismatch, expected '${VERMAGIC}':" >&2
    strings "$B/mod/tun.ko" | grep vermagic >&2
    exit 1
fi

mkdir -p "$OUT"
cp "$B/mod/tun.ko" "$OUT/tun.ko"
echo "tun.ko -> $OUT/tun.ko"
