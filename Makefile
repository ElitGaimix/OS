CC   = gcc
LD   = ld
NASM = nasm
QEMU_IMG = qemu-img
QEMU = qemu-system-i386
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -nostdlib -O2 -Wall -c

C_SOURCES := $(shell find src -type f -name '*.c')
MODULE_SOURCES := $(shell find src/system -type f -name '*.c')
KERNEL_SOURCES := $(filter-out $(MODULE_SOURCES),$(C_SOURCES))
OBJECTS := $(patsubst src/%.c,build/obj/%.o,$(KERNEL_SOURCES))
MODULE_OBJECTS := $(patsubst src/%.c,build/obj/%.o,$(MODULE_SOURCES))
ENTRY_SECTORS = $$((($$(stat -c%s build/entry.bin) + 511) / 512))

all: build/os.img

build/entry.bin: $(MODULE_OBJECTS) module.ld
	@mkdir -p build
	$(LD) -m elf_i386 --oformat binary -T module.ld $(MODULE_OBJECTS) -o $@

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

build/obj/boot.o: src/boot.c build/entry.bin
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DENTRY_SECTORS=$(ENTRY_SECTORS) $< -o $@

build/boot.bin: $(OBJECTS) linker.ld
	@mkdir -p build
	$(LD) -m elf_i386 --oformat binary -T linker.ld $(OBJECTS) -o $@

build/bootloader.bin: src/asm/bootloader.asm build/boot.bin
	@mkdir -p build
	$(NASM) -f bin -DC_SECTORS=$$(( ($$(stat -c%s build/boot.bin) + 511) / 512 )) $< -o $@

build/os-base.img: build/bootloader.bin build/boot.bin build/entry.bin
	@sectors=$$((($$(stat -c%s build/boot.bin) + 511) / 512)); test $$sectors -lt 16 || { echo "boot.bin overlaps entry.bin at LBA 16"; exit 1; }
	cat build/bootloader.bin build/boot.bin > $@
	@module_sectors=$$((($$(stat -c%s build/entry.bin) + 511) / 512)); \
	dd if=build/entry.bin of=$@ bs=512 seek=16 conv=notrunc status=none; \
	truncate -s $$(((16 + module_sectors) * 512)) $@

build/os.img: build/os-base.img
	cd build && $(QEMU_IMG) create -f qcow2 -F raw -b os-base.img os.img 5000000000

run: build/os.img
	$(QEMU) -m 8G -drive format=qcow2,file=build/os.img

clean:
	rm -rf build

.PHONY: all run clean
