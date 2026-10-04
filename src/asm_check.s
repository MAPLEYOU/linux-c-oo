	.file	"asm_check.c"
	.intel_syntax noprefix
	.text
	.p2align 4
	.globl	upcast_tcp_to_sock
	.def	upcast_tcp_to_sock;	.scl	2;	.type	32;	.endef
	.seh_proc	upcast_tcp_to_sock
upcast_tcp_to_sock:
	.seh_endprologue
	mov	rax, rcx
	ret
	.seh_endproc
	.p2align 4
	.globl	upcast_sock_to_inet
	.def	upcast_sock_to_inet;	.scl	2;	.type	32;	.endef
	.seh_proc	upcast_sock_to_inet
upcast_sock_to_inet:
	.seh_endprologue
	mov	rax, rcx
	ret
	.seh_endproc
	.p2align 4
	.globl	downcast_dev_to_client
	.def	downcast_dev_to_client;	.scl	2;	.type	32;	.endef
	.seh_proc	downcast_dev_to_client
downcast_dev_to_client:
	.seh_endprologue
	lea	rax, -32[rcx]
	ret
	.seh_endproc
	.ident	"GCC: (x86_64-win32-seh-rev0, Built by MinGW-Builds project) 15.1.0"
