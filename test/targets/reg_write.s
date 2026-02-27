.global main

.section .data

hex_format: 		.asciz "%#x"	# x86 & MMX GPRs
float_format: 		.asciz "%.2f"	# SSE FPRs
long_float_format: 	.asciz "%.2Lf"	# x87 "Long-float" FPRs

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
	
	movq %mm0, %rsi

	# prints value to MM0 = MMX
	leaq hex_format(%rip), %rdi
	movq $0, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	trap
	
	# prints contents of xmm0 = SSE
	leaq float_format(%rip), %rdi
	movq $1, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	trap

	subq $16, %rsp
	fstpt (%rsp)
	leaq long_float_format(%rip), %rdi
	movq $0, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	addq $16, %rsp
	trap

	popq %rbp
	movq $0, %rax

	ret
