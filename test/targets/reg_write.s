.global main

.section .data

hex_format: .asciz "%#x"	# x87 & MMX GPRs
float_format: .asciz "%.2f"	# SSE FPRs

.section .text

.macro trap
	# Trap
	movq $62, %rax
	movq %r12, %rdi
	movq $5, %rsi
	syscall	
.endm

main:
	push %rbp
	movq %rsp, %rbp

	# Get PID
	movq $39, %rax
	syscall
	movq %rax, %r12

	leaq hex_format(%rip), %rdi
	movq $0, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	trap
	
	# Print contents of MM0
	movq %mm0, %rsi

	leaq hex_format(%rip), %rdi
	movq $0, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	trap

	leaq float_format(%rip), %rdi
	movq $1, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	trap

	popq %rbp
	movq $0, %rax

	ret
