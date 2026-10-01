; bootloader.asm - un seul secteur de 512 octets
; 1) demarre en 16 bits (BIOS)  2) charge boot.bin (le code C) en 0x10000
; 3) active A20  4) passe en mode protege 32 bits  5) saute dans boot.c
[org 0x7c00]
[bits 16]

%ifndef C_SECTORS
%define C_SECTORS 8                 ; passe par le Makefile / build.bat (-DC_SECTORS=n)
%endif

C_ADDR equ 0x10000                  ; adresse ou boot.c est charge (voir linker.ld)
E820_BUFFER equ 0x5000
E820_MAX equ 16

; Initialise le processeur, charge boot.bin et collecte la carte memoire E820.
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti                             ; INT 13h a parfois besoin des IRQ
    cld
    mov [boot_drive], dl            ; disque de boot donne par le BIOS

    mov si, msg_boot
    call print

    ; Lecture LBA (INT 13h ext) : boot.bin juste apres ce secteur (LBA 1)
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Carte memoire BIOS E820, avant le passage en mode protege
    xor ebx, ebx
    mov word [map_count], 0
.e820_next:
    cmp word [map_count], E820_MAX
    jae .e820_done
    mov di, [map_count]
    shl di, 4
    mov ax, [map_count]
    shl ax, 3
    add di, ax
    add di, E820_BUFFER
    xor ax, ax
    mov es, ax
    mov dword [es:di+20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150           ; signature "SMAP"
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc word [map_count]
    test ebx, ebx
    jnz .e820_next
.e820_done:

    ; A20 (methode rapide)
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    cli                             ; pas d'IDT : plus d'interruptions
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1                       ; PE
    mov cr0, eax
    jmp 0x08:init_pm                ; far jump : charge CS en 32 bits

disk_error:
    mov si, msg_err
    call print
    jmp $

; Affiche la chaine terminee par zero pointee par SI via le BIOS.
print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

[bits 32]
; Configure les segments 32 bits puis appelle boot_main avec la carte E820.
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    movzx eax, word [map_count]
    push eax                        ; second argument: nombre d'entrees E820
    push dword E820_BUFFER          ; first argument: adresse du tampon
    call C_ADDR                     ; -> boot_main(buffer, count) dans boot.c
    cli
.hang:
    hlt
    jmp .hang

; --- donnees ---
boot_drive db 0
map_count dw 0

dap:                                ; Disk Address Packet
    db 0x10, 0
    dw C_SECTORS
    dw 0x0000, 0x1000               ; offset:segment -> 0x1000:0x0000 = 0x10000
    dq 1                            ; LBA de depart

gdt_start:
    dq 0
    dw 0xFFFF, 0x0000               ; 0x08 : code 32 bits, base 0, limite 4 Go
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000               ; 0x10 : donnees 32 bits, base 0, limite 4 Go
    db 0x00, 0x92, 0xCF, 0x00
    dq 0x00AF9A000000FFFF           ; 0x18 : code 64 bits
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

msg_boot db "Boot 16 bits...", 0x0D, 0x0A, 0
msg_err  db "Erreur disque", 0

times 510-($-$$) db 0
dw 0xAA55
