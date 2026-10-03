CC   = clang
LD   = ld.lld
OBJCOPY = objcopy
NASM = nasm
QEMU_IMG = qemu-img
QEMU = qemu-system-x86_64

CFLAGS = --target=i686-elf -m32 -Iinclude -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -nostdlib -O2 -Wall -c
KERNEL_CFLAGS = --target=x86_64-elf -m64 -Iinclude -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -O2 -Wall -c
USER_CFLAGS = --target=x86_64-elf -m64 -Iinclude -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -O2 -Wall -c

KERNEL_SOURCES := $(shell find src/kernel -type f -name '*.c')
KERNEL_OBJECTS := $(patsubst src/%.c,build/obj/%.o,$(KERNEL_SOURCES))
KERNEL_SECTORS = $$((($$(stat -c%s build/kernel.bin) + 511) / 512))

# On ne compile qu'un seul programme utilisateur pour le moment : src/user/program.c
USER_SOURCES := src/user/program.c
USER_OBJECTS := $(patsubst src/%.c,build/obj/%.o,$(USER_SOURCES))
USER_LBA = 2048
USER_ELF = build/user.elf
USER_BIN = build/user.bin
USER_SECTORS = $$((($$(stat -c%s $(USER_BIN)) + 511) / 512))

all: build/os.img

build/kernel.bin: $(KERNEL_OBJECTS) module.ld
	@mkdir -p build
	$(LD) -m elf_x86_64 --oformat binary -T module.ld $(KERNEL_OBJECTS) -o $@

$(USER_ELF): $(USER_OBJECTS) user.ld
	@mkdir -p build
	$(LD) -m elf_x86_64 -T user.ld -o $@ $(USER_OBJECTS)

$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) $< -o $@

build/obj/user/%.o: src/user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $< -o $@

build/obj/bootloader/longmode.o: src/bootloader/longmode.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

build/obj/bootloader/boot.o: src/bootloader/boot.c build/kernel.bin build/user.bin
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DKERNEL_SECTORS=$(KERNEL_SECTORS) -DUSER_LBA=$(USER_LBA) -DUSER_SECTORS=$(USER_SECTORS) $< -o $@

build/boot.bin: build/obj/bootloader/boot.o build/obj/bootloader/longmode.o linker.ld
	@mkdir -p build
	$(LD) -m elf_i386 --oformat binary -T linker.ld build/obj/bootloader/boot.o build/obj/bootloader/longmode.o -o $@

build/bootloader.bin: src/bootloader/bootloader.asm build/boot.bin
	@mkdir -p build
	$(NASM) -f bin -DC_SECTORS=$$(( ($$(stat -c%s build/boot.bin) + 511) / 512 )) $< -o $@

build/os-base.img: build/bootloader.bin build/boot.bin build/kernel.bin build/user.bin
	@sectors=$$((($$(stat -c%s build/boot.bin) + 511) / 512)); test $$sectors -lt 16 || { echo "boot.bin overlaps kernel.bin at LBA 16"; exit 1; }
	@kernel_sectors=$$((($$(stat -c%s build/kernel.bin) + 511) / 512)); test $$((16 + kernel_sectors)) -le $(USER_LBA) || { echo "kernel.bin overlaps user.bin at LBA $(USER_LBA)"; exit 1; }
	cat build/bootloader.bin build/boot.bin > $@
	@kernel_sectors=$$((($$(stat -c%s build/kernel.bin) + 511) / 512)); \
	dd if=build/kernel.bin of=$@ bs=512 seek=16 conv=notrunc status=none; \
	truncate -s $$(((16 + kernel_sectors) * 512)) $@
	@user_sectors=$$((($$(stat -c%s build/user.bin) + 511) / 512)); \
	dd if=build/user.bin of=$@ bs=512 seek=$(USER_LBA) conv=notrunc status=none; \
	truncate -s $$((($(USER_LBA) + user_sectors) * 512)) $@

build/os.img: build/os-base.img
	cd build && $(QEMU_IMG) create -f qcow2 -F raw -b os-base.img os.img 5000000000

run: build/os.img
	$(QEMU) -m 8G -drive format=qcow2,file=build/os.img

clean:
	rm -rf build

.PHONY: all run clean
