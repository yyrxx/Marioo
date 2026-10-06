ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT environment variable is not set")
endif

CC      := ppu-gcc
CFLAGS  := -O2 -Wall -mcpu=cell -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include
LDFLAGS := -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib
LIBS    := -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

TARGET  := minimario

all: $(TARGET).self

$(TARGET).elf: main.c
	$(CC) $(CFLAGS) main.c -o $@ $(LDFLAGS) $(LIBS)

$(TARGET).self: $(TARGET).elf
	sprxlinker $<
	make_self $< $@

clean:
	rm -f $(TARGET).elf $(TARGET).self
