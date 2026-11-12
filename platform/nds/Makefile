# Nintendo DS (ARM9) build with devkitARM and libnds.   usage: make -C platform/nds   (DEVKITPRO and DEVKITARM set, the nds-dev group installed)
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set)
endif
DEVKITPRO ?= /opt/devkitpro
LIBNDS    ?= $(DEVKITPRO)/libnds
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

TARGET := pacnjump
ROOT   := ../..
SRC    := main.c $(ROOT)/core/pnj_core.c
ARCH   := -mthumb -mthumb-interwork
CFLAGS := -std=gnu99 -O2 -Wall -march=armv5te -mtune=arm946e-s -fomit-frame-pointer $(ARCH) -DARM9 -DPNJ_FLOAT -DPNJ_NDS -I$(LIBNDS)/include
LDFLAGS := -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(TARGET).map
LIBS   := -L$(LIBNDS)/lib -lnds9 -lm
CC     := arm-none-eabi-gcc

all: $(TARGET).nds

$(TARGET).nds: $(TARGET).elf
	ndstool -c $@ -9 $<

$(TARGET).elf: $(SRC:.c=.o)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(ROOT)/core/*.o $(TARGET).elf $(TARGET).nds $(TARGET).map
