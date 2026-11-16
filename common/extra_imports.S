@ Imports of libkernel exports that the vitasdk stubs can't give, in the format of the stubs that
@ vita-libs-gen-2 makes. Each stub holds its flags, the library NID and the export's NID. Two
@ functions are missing from the stubs. __stack_chk_guard, 0x93B8AA67, is in the stubs, but newlib
@ defines the same symbol, so it is imported here under another name.

#define SCE_LIB_KERNEL_NID 0xCAE9ACE6

.macro function name, nid
	.section .vitalink.fstubs.SceLibKernel,"ax",%progbits
	.align 4
	.global \name
	.type \name, %function
\name:
	.word 0
	.word SCE_LIB_KERNEL_NID
	.word \nid
	.align 4
.endm

.macro variable name, nid
	.section .vitalink.vstubs.SceLibKernel,"",%progbits
	.align 4
	.global \name
	.type \name, %object
\name:
	.word 0
	.word SCE_LIB_KERNEL_NID
	.word \nid
	.align 4
.endm

.arch armv7a

function SceLibKernel_35D20E49, 0x35D20E49
function SceLibKernel_6F9C4CC1, 0x6F9C4CC1
variable libkernel_stack_chk_guard, 0x93B8AA67
