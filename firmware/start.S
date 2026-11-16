/* SPDX-License-Identifier: GPL-3.0-only */
.set noreorder
.section .startup,"ax",@progbits
.globl standalone_start
standalone_start:
    di
    ehb
    lui $sp,0xa001
    addiu $sp,$sp,-8
    lui $gp,0xa000
    addiu $gp,$gp,0x7ff0
    lui $t0,0x9d01
    addiu $t0,$t0,-0x6000
    mtc0 $t0,$15,1
    mfc0 $t0,$12
    lui $t1,0xffbf
    ori $t1,$t1,0xfff8
    and $t0,$t0,$t1
    mtc0 $t0,$12
    ehb
    lui $t0,%hi(__bss_start)
    addiu $t0,$t0,%lo(__bss_start)
    lui $t1,%hi(__bss_end)
    addiu $t1,$t1,%lo(__bss_end)
1:  beq $t0,$t1,2f
    nop
    sw $zero,0($t0)
    b 1b
    addiu $t0,$t0,4
2:
#ifdef ONE_PROFILE
    lui $t2,0xa55a
    ori $t2,$t2,0x5aa5
    lui $t0,%hi(__bss_end)
    addiu $t0,$t0,%lo(__bss_end)
    move $t1,$sp
3:  beq $t0,$t1,4f
    nop
    sw $t2,0($t0)
    b 3b
    addiu $t0,$t0,4
4:
#endif
    jal standalone_main
    nop
    j standalone_fault
    nop
.section .exception,"ax",@progbits
    j standalone_fault
    nop
.section .debug_exception,"ax",@progbits
    j standalone_fault
    nop
/* Emit only the original addressed 8-byte vector stubs, without new holes. */
.macro VECTOR n
.section .vector\n,"ax",@progbits
    j standalone_fault
    nop
.endm
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43
VECTOR \n
.endr
.section .reset_handler,"ax",@progbits
.globl standalone_reset
standalone_reset:
    di
    ehb
    lui $t0,0xbf81
    lui $t1,0xaa99
    ori $t1,$t1,0x6655
    sw $zero,-0xdd0($t0)
    sw $t1,-0xdd0($t0)
    lui $t1,0x5566
    ori $t1,$t1,0x99aa
    sw $t1,-0xdd0($t0)
    li $t1,1
    sw $t1,-0x9e8($t0)
    lw $t1,-0x9f0($t0)
1:  b 1b
    nop
/* Boot-flash debug vector is owned by the resident firmware; omit it. */
.section .config2,"a",@progbits
    .word 0xfff9fffc
.section .config1,"a",@progbits
    .word 0xff6bcedb
