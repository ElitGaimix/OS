[bits 32]

section .text
global enter_long_mode

enter_long_mode:
    cli
    mov esi, [esp + 8]              ; entree du kernel
    mov eax, [esp + 4]              ; adresse PML4
    mov ebx, [esp + 12]             ; adresse de la carte E820
    mov ebp, [esp + 16]             ; nombre d'entrees E820

    mov ecx, cr4
    or ecx, 1 << 5                  ; PAE
    mov cr4, ecx
    mov cr3, eax

    mov ecx, 0xC0000080             ; EFER
    rdmsr
    or eax, 1 << 8                  ; LME
    wrmsr

    mov eax, cr0
    or eax, 1 << 31                 ; pagination
    mov cr0, eax
    jmp 0x18:long_mode_entry

[bits 64]
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, 0x90000
    mov r11d, esi                    ; sauvegarde de l'entree du kernel
    mov edi, ebx                     ; premier argument SysV: carte E820
    mov esi, ebp                     ; deuxieme argument SysV: nombre d'entrees
    call r11

.halt:
    cli
    hlt
    jmp .halt