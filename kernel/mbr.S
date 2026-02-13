; asmsyntax=nasm

; BIOS loads this boot sector code to this physical address
; and jumps to it
[org 0x7c00]
[bits 16]

boot:
	cli

	; Set up stack
	push 0x8000
	pop ss
	mov sp, 0xFFFF
	
	call enable_a20

	; Ensure code and data segments are the same
	push cs
	pop ds

	; Serenity-inspired hack, we just stuff the ELF image GCC
	; outputs into RAM and jump to (minimafully) the right offset
	; inside it :D
	
	push 0x1000
	pop es
	xor bx, bx

	mov cx, word [cur_lba]
.sector_loop:
	call convert_lba_to_chs
	; https://www.ctyme.com/intr/rb-0607.htm
	mov ah, 0x02  ; Read Disk Sectors
	mov al, 0x01  ; One sector (512B)
	mov dl, 0x00  ; From drive 0 (A)
	int 0x13

	; INT 13h sets the carry flag if something went bust
	; status is in AH
	jc read_error
	mov al, '.'
	call print_char

	inc word [cur_lba]
	mov cx, word [cur_lba]
	cmp cx, 400; We're reading 400 * 512B = ~204KB total
	; TODO: cmp ax, 400 <-- 1 byte smaller than cmp cx, 400!
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

	; Note: On QEMU, this reports 64MB max
	call get_mem_kb

	mov bx, hop_message
	call print_str

	lgdt [cs:initial_gdt_ptr]

	; Follow multiboot, switch to protected mode before jumping
	; to our second stage _start in _start.c
	mov eax, cr0
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
	; We need to align the stack to 16 bytes. So push 12 bytes of nothing
	; in addition to [mem_kb]
	push dword 0
	push dword 0
	push word  0
	push word [mem_kb] ; Pass memory amount to _start
	call 0x10000   ; Should end up in _start in _start.c
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
initial_gdt_end:

[bits 16]
enable_a20:
	call check_a20
	jnz a20_okay

	; First, ask the BIOS to hook up A20
	clc
	mov ax, 0x2401
	int 0x15
	;   ^ That interrupt sets CF on error, but we ignore that here
	; and just test for wraparound directly.

	call check_a20
	jnz a20_okay

	; Ask the kbd controller to hook it up
	call set_a20_8042
	; TODO: Might need to call this in a loop a few times
	; on some systems with a slow keyboard controller
	call check_a20
	jnz a20_okay

	; Chipsets on IBM PS/2 and newer may support this
	call set_a20_fast
	call check_a20
	jnz a20_okay

	; Some chipsets apparently also support reading from port 0xEE
	; to enable A20, but I'll skip that for now.

	mov bx, a20_message
	jmp fatal_error
a20_okay:
	ret

set_a20_8042:
	call .wait_8042_cmd
	mov al, 0xAD ; Disable keyboard
	out 0x64, al
	call .wait_8042_cmd
	mov al, 0xD0 ; Read from input
	out 0x64, al
	call .wait_8042_data
	in al, 0x60 ; Read byte
	push ax
	call .wait_8042_cmd
	mov al, 0xD1 ; Write to output
	out 0x64, al
	call .wait_8042_cmd
	pop ax
	or al, 2
	out 0x60, al
	call .wait_8042_cmd
	mov al,0xAE ; Enable kbd
	out 0x64, al
	call .wait_8042_cmd
	ret
.wait_8042_cmd:
	in al, 0x64
	test al, 2
	jnz .wait_8042_cmd
	ret
.wait_8042_data:
	in al, 0x64
	test al, 1
	jz .wait_8042_data
	ret

set_a20_fast:
	in al, 0x92
	or al, 0x02   ; Enable A20
	and al, ~0x01 ; Don't reset machine
	out 0x92, al
	ret

; Adapted from https://www.independent-software.com/operating-system-development-enabling-a20-line.html
; Trimmed down a bit by me
;     ZF=1 -> A20 disabled
;     ZF=0 -> A20 enabled
check_a20:
	xor ax, ax
	mov es, ax
	mov di, 0x0500 ; es:di = 0x0000:0x0500 = 0x00000500
	not ax
	mov ds, ax
	mov si, 0x0510 ; ds:si = 0xffff:0x0510 = 0x00100500
	; Stash bytes currently at these two addresses to stack
	mov al, es:[di]
	push ax
	mov al, ds:[si]
	push ax
	mov es:[di], 0x00 ; Set 0x00000500 to 0x00
	mov ds:[si], 0xff ; Set 0x00100500 to 0xff
	cmp es:[di], 0xff ; Is 0x00000500 now 0xff? ZF is set to 1 if so.
	; Restore original values to be safe
	pop ax
	mov ds:[si], al
	pop ax
	mov es:[di], al
	ret

read_error:
	call print_hex
	mov bx, read_error_msg
fatal_error:
	call print_str
	hlt

; ax = hex word
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
	call print_char
	pop ax
	loop .print_loop
	mov al, 'h'
	call print_char
	mov al, ' '
	call print_char
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

; https://www.ctyme.com/intr/rb-1529.htm
; After calling this, ax = number of contiguous KB starting at absolute address 0x100000
; CF clear on success, set on error. AH = Status
; On Bochs with 'memory: guest=32', this reports 0x7C00 = 32MB
; Gentoo on 486 box reports that it uses this same method, and reports 32MB RAM.
; See 486_gentoo_boot.txt for details.
get_mem_kb:
	pusha
	clc
	mov ah, 0x88
	int 0x15
	jc .mem_map_error
	mov word [mem_kb], ax
	mov bx, mem_success
	call print_str
	call print_hex ; print kilobytes in hex
	popa
	ret
.mem_map_error:
	mov bx, mem_error
	jmp fatal_error

; bx = str address
print_str:
	pusha
	mov si, bx
	xor bx, bx
	cld
	lodsb
.print:
	call print_char
	lodsb
	cmp al, 0
	jne .print
	popa
	ret

; AL = char
print_char:
	mov ah, 0xE
	int 0x10
	out 0xE9, al
	ret

cur_lba:
	dw 9        ; << Notice that we are offsetting to skip the ELF header + bootloader (0x1200 B)
sectors_per_track:
	dw SECTORS_PER_TRACK
heads:
	dw 2
mem_kb:
	dw 2

a20_message:
	db 0xD, 0xA, "A20?", 0
read_error_msg:
	db 0xD, 0xA, "R?", 0
hop_message:
	db 0xD, 0xA, "32", 0
mem_success:
	db 0xD, 0xA, "M:", 0
mem_error:
	db 0xD, 0xA, "M?", 0

%assign padding (510 - ($ - $$))
%if padding < 0
	%error not enough space: padding
%else
	%warning padding bytes remaining
%endif
times padding db 0x00
dw 0xAA55
