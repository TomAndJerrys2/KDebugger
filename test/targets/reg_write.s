.global main

.section .data

.section .text

main:
	push %rbp
	movq %rsp, %rbp

	# Get PID
	movq $39, %rax
	syscall
	movq %rax, %r12

	# Trap
	movq $62, %rax
	movq %r12, %rdi
	movq $5, %rsi
	syscall	

	popq %rbp
	movq $0, %rax

	ret
