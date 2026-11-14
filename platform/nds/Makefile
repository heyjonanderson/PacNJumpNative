# Nintendo DS (ARM9) build with devkitARM, calico and libnds.   usage: make -C platform/nds   (DEVKITPRO and DEVKITARM set, the nds-dev group installed)
# The program is the shared shell (platform/sdl/main.c) with nds_sdl.h standing in for SDL; see that file. The ROM holds no game data: it reads
# pnj_ds.pack (tools/make_ndspack.py) from the SD card.
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif
DEVKITPRO ?= /opt/devkitpro
LIBNDS    ?= $(DEVKITPRO)/libnds
CALICO    ?= $(DEVKITPRO)/calico
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

TARGET  := pacnjump
ROOT    := ../..
VERSION ?= dev
ARCH    := -mthumb -mthumb-interwork
CFLAGS  := -std=gnu99 -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-misleading-indentation -Wno-format-truncation \
           -march=armv5te -mtune=arm946e-s -fomit-frame-pointer -ffunction-sections -fdata-sections $(ARCH) \
           -D__NDS__ -DARM9 -DPNJ_FLOAT -DPNJ_NDS -DPNJ_VERSION=\"$(VERSION)\" -I$(LIBNDS)/include -I$(CALICO)/include -I$(ROOT)/platform/sdl
LDFLAGS := -specs=$(CALICO)/share/ds9.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(TARGET).map
LIBS    := -L$(CALICO)/lib -L$(LIBNDS)/lib -lfat -lnds9 -lcalico_ds9 -lm
CC      := arm-none-eabi-gcc

all: $(TARGET).nds

$(TARGET).nds: $(TARGET).elf
	ndstool -c $@ -9 $< -7 $(CALICO)/bin/ds7_maine.elf -b $(CALICO)/share/nds-icon.bmp "Pac'N-Jump;native port $(VERSION);needs pnj_ds.pack"

$(TARGET).elf: main.o pnj_core.o
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
	arm-none-eabi-size $@

main.o: $(ROOT)/platform/sdl/main.c $(wildcard *.h) $(wildcard $(ROOT)/platform/sdl/*.h)
	$(CC) $(CFLAGS) -c $< -o $@

pnj_core.o: $(ROOT)/core/pnj_core.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET).elf $(TARGET).nds $(TARGET).map
