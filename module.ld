ENTRY(kmain)

SECTIONS {
    . = 0x100000;
    .text   : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(COMMON) *(.bss*) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
