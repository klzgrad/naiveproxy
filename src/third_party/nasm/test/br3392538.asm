	bits 64
	default	rel

	section	.text
	global	_start
_start:

	mov	rax, 1	; write syscall
	mov	rdi, 1
	mov	rsi, msg
	mov	rdx, msglen 
	syscall

	mov	rax, 60	; exit syscall
	sub	rdi, rdi
	syscall

; either of the following lines cause: Error in `nasm': double free or corruption ; Aborted (core dumped)
foo
; warning: label alone on a line without a colon might be in error [-w+label-orphan]
	mov	r8, r9, r10
; error: invalid combination of opcode and operands
	add	r8d, byte 80h
; warning: signed byte exceeds bounds [-w+number-overflow]
	section	.data
msg	db	"Hello, world!", 10
msglen	equ	$-msg
