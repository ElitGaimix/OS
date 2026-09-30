ENTRY(boot_main)

SECTIONS {
    . = 0x10000;
    .text   : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(COMMON) *(.bss*) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
