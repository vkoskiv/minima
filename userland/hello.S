; asmsyntax=nasm
; hello.S - minimal "Hello, world" for minima
[bits 32]
section .data
msg db "Hello, world!", 10    ; message plus newline
len equ $ - msg              ; 13

section .text
global _start

_start:
    ; sys_write(stdout=1, buf=msg, count=len)
    mov eax, 3        ; syscall number, 3 = write
    mov ebx, 1        ; file descriptor,  1 = stdout
    mov ecx, msg      ; pointer to message
    mov edx, len      ; message length (13)
    int 0x80

    xchg bx, bx    ; sys_exit(status=0)
    mov eax, 0        ; syscall number 0 = exit
    xor ebx, ebx      ; exit status 0
    int 0x80
