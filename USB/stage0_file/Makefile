CC		:= ppu-gcc
CXX		:= ppu-g++
LD		:= ppu-ld 
OBJCOPY		:= ppu-objcopy
AR 		:= ppu-ar
STRIP		:= ppu-strip
INCLUDE		= ../lv2/include ../lv1/include ../debug/include
LIBSDIR         =
LIBS            =
TARGET		= stage0


CFLAGS = -ffunction-sections -fdata-sections -Os -m64 -fno-builtin -fno-exceptions \
			   -Os -Wall $(foreach dir,$(INCLUDE),-I$(dir)) \
			   -DLV2 -DFIRMWARE_3_55 -ffreestanding 
CFLAGS += --std=gnu99
LDFLAGS=-nostartfiles -nostdlib -mbig-endian
LDFLAGS= -T $(TARGET).ld -nostartfiles -nostdlib -nodefaultlibs -Wl,-static -Wl,-s -L. $(foreach dir,$(LIBSDIR),-L$(dir)) $(LIBS) -Wl,--gc-sections -Wl

OBJS = start.o main.o ../lv1/src/hvcall.o

all: $(TARGET).bin

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@	

$(TARGET).elf: $(OBJS) $(TARGET).ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.elf *.bin ../lv1/src/*.o
