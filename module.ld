ENTRY(kmain)

SECTIONS {
    . = 0x100000;
    __kernel_start = .;
    .text   : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : {
        __bss_start = .;
        *(COMMON) *(.bss*)
        __bss_end = .;
    }
    __kernel_end = .;
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}

ASSERT(__kernel_end <= 0x300000, "kernel exceeds the UEFI 2-MiB reservation")
