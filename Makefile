CC   = gcc
LD   = ld
NASM = nasm
QEMU_IMG = qemu-img
QEMU = qemu-system-x86_64
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -nostdlib -O2 -Wall -c
KERNEL_CFLAGS = $(subst -m32,-m64,$(CFLAGS)) -mgeneral-regs-only

KERNEL_SOURCES := $(shell find src/kernel src/system -type f -name '*.c')
KERNEL_OBJECTS := $(patsubst src/%.c,build/obj/%.o,$(KERNEL_SOURCES))
KERNEL_SECTORS = $$((($$(stat -c%s build/kernel.bin) + 511) / 512))

all: build/os.img

build/kernel.bin: $(KERNEL_OBJECTS) module.ld
	@mkdir -p build
	$(LD) -m elf_x86_64 --oformat binary -T module.ld $(KERNEL_OBJECTS) -o $@

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $< -o $@

build/obj/bootloader/longmode.o: src/bootloader/longmode.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

build/obj/bootloader/boot.o: src/bootloader/boot.c build/kernel.bin
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DKERNEL_SECTORS=$(KERNEL_SECTORS) $< -o $@

build/boot.bin: build/obj/bootloader/boot.o build/obj/bootloader/longmode.o linker.ld
	@mkdir -p build
	$(LD) -m elf_i386 --oformat binary -T linker.ld build/obj/bootloader/boot.o build/obj/bootloader/longmode.o -o $@

build/bootloader.bin: src/bootloader/bootloader.asm build/boot.bin
	@mkdir -p build
	$(NASM) -f bin -DC_SECTORS=$$(( ($$(stat -c%s build/boot.bin) + 511) / 512 )) $< -o $@

build/os-base.img: build/bootloader.bin build/boot.bin build/kernel.bin
	@sectors=$$((($$(stat -c%s build/boot.bin) + 511) / 512)); test $$sectors -lt 16 || { echo "boot.bin overlaps kernel.bin at LBA 16"; exit 1; }
	cat build/bootloader.bin build/boot.bin > $@
	@kernel_sectors=$$((($$(stat -c%s build/kernel.bin) + 511) / 512)); \
	dd if=build/kernel.bin of=$@ bs=512 seek=16 conv=notrunc status=none; \
	truncate -s $$(((16 + kernel_sectors) * 512)) $@

build/os.img: build/os-base.img
	cd build && $(QEMU_IMG) create -f qcow2 -F raw -b os-base.img os.img 5000000000

run: build/os.img
	$(QEMU) -m 8G -drive format=qcow2,file=build/os.img

clean:
	rm -rf build

.PHONY: all run clean
