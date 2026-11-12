# Nintendo DS (ARM9) build with devkitARM and libnds.   usage: make -C platform/nds   (DEVKITPRO and DEVKITARM set, the nds-dev group installed)
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif
DEVKITPRO ?= /opt/devkitpro
LIBNDS    ?= $(DEVKITPRO)/libnds
CALICO    ?= $(DEVKITPRO)/calico
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

TARGET := pacnjump
ROOT   := ../..
SRC    := main.c $(ROOT)/core/pnj_core.c
ARCH   := -mthumb -mthumb-interwork
CFLAGS := -std=gnu99 -O2 -Wall -march=armv5te -mtune=arm946e-s -fomit-frame-pointer $(ARCH) -D__NDS__ -DARM9 -DPNJ_FLOAT -DPNJ_NDS -I$(LIBNDS)/include -I$(CALICO)/include
LDFLAGS := -specs=$(CALICO)/share/ds9.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS   := -L$(CALICO)/lib -L$(LIBNDS)/lib -lnds9 -lcalico_ds9 -lm
CC     := arm-none-eabi-gcc

all: $(TARGET).nds

$(TARGET).nds: $(TARGET).elf
	ndstool -c $@ -9 $< -7 $(CALICO)/bin/ds7_maine.elf -b $(CALICO)/share/nds-icon.bmp "Pac'N-Jump;core benchmark;built with devkitARM"

$(TARGET).elf: $(SRC:.c=.o)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(ROOT)/core/*.o $(TARGET).elf $(TARGET).nds $(TARGET).map
