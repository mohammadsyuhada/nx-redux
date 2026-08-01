###########################################################
# Cross build (PLATFORM/CROSS_COMPILE come from the toolchain env,
# same convention as workspace/all/emu-options/Makefile)
ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

TARGET  = m64p-server
SOURCE  = main.c tcp.c udp.c
CFLAGS  += -O2 -std=gnu99 -Wall -Wextra
PRODUCT = build/$(PLATFORM)/$(TARGET).elf

all: $(PRODUCT)
$(PRODUCT): $(SOURCE) server.h
	@mkdir -p build/$(PLATFORM)
	$(CROSS_COMPILE)gcc $(CFLAGS) -o $(PRODUCT) $(SOURCE)

# Native build for protocol tests (runs on the dev machine)
native: $(SOURCE) server.h
	cc $(CFLAGS) -o $(TARGET)-native $(SOURCE)

test: native
	cc $(CFLAGS) -o tests/test_client tests/test_client.c
	sh tests/run_tests.sh

clean:
	rm -rf build $(TARGET)-native tests/test_client
