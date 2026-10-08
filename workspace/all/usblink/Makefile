###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

include ../../$(PLATFORM)/platform/Makefile.env

###########################################################

TARGET = usblink.elf

.PHONY: all clean

CC = $(CROSS_COMPILE)gcc

CFLAGS += -Wall -Wextra -O2 -std=gnu99 -I.
# Makefile.env links SDL/GLES for the UI apps; this daemon only needs pthreads.
LDFLAGS = -lpthread

SRCS = usblink.c usblink_frame.c usblink_link.c usblink_state.c usblink_tun.c usblink_gadget.c usblink_host.c
OBJS = $(addprefix build/$(PLATFORM)/,$(SRCS:.c=.o))

PRODUCT = build/$(PLATFORM)/$(TARGET)

all: $(PRODUCT)

build/$(PLATFORM):
	mkdir -p $@

$(PRODUCT): build/$(PLATFORM) $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

build/$(PLATFORM)/%.o: %.c | build/$(PLATFORM)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(PRODUCT) $(OBJS)
