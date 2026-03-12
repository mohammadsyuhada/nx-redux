ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

TARGET = taskset
PRODUCT = build/$(PLATFORM)/$(TARGET)

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Os -s -Wl,--gc-sections -static

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(TARGET).c -o $(PRODUCT) $(CFLAGS)
clean:
	rm -f $(PRODUCT)
