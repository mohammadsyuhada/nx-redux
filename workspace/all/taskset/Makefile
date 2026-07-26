ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

TARGET = taskset
PRODUCT = build/$(PLATFORM)/$(TARGET)

CC = $(CROSS_COMPILE)gcc
# NOT -static: a statically-linked glibc binary's own startup code
# (_dl_non_dynamic_init(), only run for static executables) compares the
# running kernel version against __LINUX_KERNEL_VERSION - a floor baked in
# at compile time from the kernel headers the toolchain's glibc was built
# against - and aborts with "FATAL: kernel too old" if the real kernel is
# older, regardless of which toolchain built the binary. This is NOT the
# .note.ABI-tag ELF note: that note is just metadata and is not what gates
# this failure, confirmed because this repo's own *dynamic* binaries carry
# a ".note.ABI-tag" of "for GNU/Linux 4.19.0" and run fine on the Brick's
# real 4.9.191 kernel - if the ABI-tag note itself were enforced, those
# would fail too. On the Brick (tg5040, real kernel 4.9.191) every static
# build tried here - even one built with tg5040's own toolchain - aborted
# identically at startup, matching the original (tg5050-toolchain-built)
# binary this replaced; only the __LINUX_KERNEL_VERSION-gated static path
# was the difference. Dynamic linking sidesteps the whole class of bug: it
# resolves libc.so.6 from the device's own /usr/lib64 at runtime (same
# NEEDED-entry resolution flycast itself relies on - see
# workspace/all/other/flycast/README.md's "Library-resolution decision"
# section) instead of embedding the toolchain's own glibc + its version
# gate, and was confirmed working on-device (task-11) where every prior
# static attempt was not. Conclusion (build dynamic) unchanged from the
# original fix; only this comment's explanation of the mechanism changed.
CFLAGS = -Os -s -Wl,--gc-sections

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(TARGET).c -o $(PRODUCT) $(CFLAGS)
clean:
	rm -f $(PRODUCT)
