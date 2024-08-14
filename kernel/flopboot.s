; asmsyntax=nasm

; BIOS loads this boot sector code to this physical address
; and jumps to it
[org 0x7c00]
[bits 16]

boot:
	cli

	; Set up stack
	mov ax, 0x8000
	mov ss, ax
	mov sp, 0xFFFF
	
	; Ensure code and data segments are the same
	push cs
	pop ds
	xor bx, bx
	mov ah, 0x0e    ; Display char
	mov si, load_message
	lodsb
.print:
	int 0x10
	lodsb
	cmp al, 0
	jne .print

	; Ask the keyboard controller to hook up A20
	clc
	mov ax, 0x2401
	int 0x15

	; That interrupt sets the carry flag if something went wrong, so
	; just dump a message if that happened (jump if carry)
	; jc a20_oof

	; Serenity-inspired hack, we just stuff the ELF image GCC
	; outputs into RAM and jump to (minimafully) the right offset
	; inside it :D
	
	mov bx, 0x1000
	mov es, bx
	xor bx, bx

	mov cx, word [cur_lba]
.sector_loop:
	call convert_lba_to_chs
	mov ah, 0x02  ; Read Disk Sectors
	mov al, 0x01  ; One sector (512B)
	mov dl, 0x00  ; From drive 0 (A)
	int 0x13

	; INT 13h sets the carry flag if something went bust
	jc read_error
	mov ah, 0x0E
	mov al, '.'
	int 0x10

	inc word [cur_lba]
	mov cx, word [cur_lba]
	cmp cx, 400; We're reading 400 * 512B = ~204KB total
	jz .sector_loop_end

	; Bump segment by 0x20, which bumps addr by 0x200 (512)
	mov bx, es
	add bx, 0x20
	mov es, bx
	xor bx, bx

	jmp .sector_loop
.sector_loop_end:

	; Turn off floppy motor
	mov dx, 0x3f2
	xor al, al
	out dx, al

	call jump_message

	lgdt [cs:initial_gdt_ptr]

	; Follow multiboot, switch to protected mode before jumping
	; to our second stage _start in boot.s
	mov eax, cr0
	;or eax, 0x80000001
	or al, 1
	mov cr0, eax

	jmp 0x08:protected_mode

protected_mode:
[bits 32]
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, 0x4000
	xor eax, eax
	xor ebx, ebx
	xor ecx, ecx
	xor edx, edx
	xor ebp, ebp
	xor esi, esi
	xor edi, edi
	jmp 0x10000   ; Should end up in _start in boot.s
	hlt

initial_gdt_ptr:
	dw (initial_gdt_end - initial_gdt)
	dd initial_gdt

initial_gdt:
	dd 0x00000000
	dd 0x00000000
	dd 0x0000FFFF
	dd 0x00CF9A00
	dd 0x0000FFFF
	dd 0x00CF9200
	dd 0x00000000
	dd 0x00000000
	dd 0x00000000
	dd 0x00000000
initial_gdt_end:

[bits 16]
a20_oof:
	xor bx, bx
	mov ah, 0x0e
	mov si, a20_message
	lodsb
.loop:
	int 0x10
	lodsb
	cmp al, 0
	jne .loop
	cli
	hlt

read_error:
	call print_hex
	xor bx, bx
	mov ah, 0x0e
	mov si, read_error_msg
	lodsb
.loop:
	int 0x10
	lodsb
	cmp al, 0
	jne .loop
	cli
	hlt

jump_message:
	xor bx, bx
	mov ah, 0x0E
	mov si, hop_message
	lodsb
.loop:
	int 0x10
	lodsb
	cmp al, 0
	jne .loop
	ret

print_hex:
	pusha
	mov cx, 0x04
.print_loop:
	rol ax, 0x04
	push ax
	and al, 0x0F
	daa
	add al, -16
	adc al, 64
	mov ah, 0x0E
	int 0x10
	pop ax
	loop .print_loop
	mov al, 'h'
	mov ah, 0x0E
	int 0x10
	mov al, ' '
	int 0x10
	popa
	ret

convert_lba_to_chs:
	mov ax, cx
	xor dx, dx
	div word [sectors_per_track]
	mov cl, dl
	inc cl
	mov ch, al
	shr ch, 1
	xor dx, dx
	div word [heads]
	mov dh, dl
	ret

cur_lba:
	dw 9        ; << Notice that we are offsetting to skip the ELF header + bootloader (1200B)
sectors_per_track:
	dw 18
heads:
	dw 2

load_message:
	db "Loading Minima", 0
load_done_message:
	db "done!", 0x0d, 0x0a, 0
a20_message:
	db "Failed to enable A20", 0x0D, 0x0A, 0
read_error_msg:
	db "Failed main read", 0x0D, 0x0A, 0
hop_message:
	db 0xD, 0xA, "Jumping to protected mode", 0x0D, 0x0A, 0

times 510-($-$$) db 0
dw 0xAA55
