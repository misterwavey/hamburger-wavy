/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_ARCH(mips)
ENTRY(standalone_start)
SECTIONS {
.exception 0x9d00a180 : { *(.exception) }
.vector0 0x9d00a200 : { *(.vector0) }
.vector1 0x9d00a220 : { *(.vector1) }
.vector2 0x9d00a240 : { *(.vector2) }
.vector3 0x9d00a260 : { *(.vector3) }
.vector4 0x9d00a280 : { *(.vector4) }
.vector5 0x9d00a2a0 : { *(.vector5) }
.vector6 0x9d00a2c0 : { *(.vector6) }
.vector7 0x9d00a2e0 : { *(.vector7) }
.vector8 0x9d00a300 : { *(.vector8) }
.vector9 0x9d00a320 : { *(.vector9) }
.vector10 0x9d00a340 : { *(.vector10) }
.vector11 0x9d00a360 : { *(.vector11) }
.vector12 0x9d00a380 : { *(.vector12) }
.vector13 0x9d00a3a0 : { *(.vector13) }
.vector14 0x9d00a3c0 : { *(.vector14) }
.vector15 0x9d00a3e0 : { *(.vector15) }
.vector16 0x9d00a400 : { *(.vector16) }
.vector17 0x9d00a420 : { *(.vector17) }
.vector18 0x9d00a440 : { *(.vector18) }
.vector19 0x9d00a460 : { *(.vector19) }
.vector20 0x9d00a480 : { *(.vector20) }
.vector21 0x9d00a4a0 : { *(.vector21) }
.vector22 0x9d00a4c0 : { *(.vector22) }
.vector23 0x9d00a4e0 : { *(.vector23) }
.vector24 0x9d00a500 : { *(.vector24) }
.vector25 0x9d00a520 : { *(.vector25) }
.vector26 0x9d00a540 : { *(.vector26) }
.vector27 0x9d00a560 : { *(.vector27) }
.vector28 0x9d00a580 : { *(.vector28) }
.vector29 0x9d00a5a0 : { *(.vector29) }
.vector30 0x9d00a5c0 : { *(.vector30) }
.vector31 0x9d00a5e0 : { *(.vector31) }
.vector32 0x9d00a600 : { *(.vector32) }
.vector33 0x9d00a620 : { *(.vector33) }
.vector34 0x9d00a640 : { *(.vector34) }
.vector35 0x9d00a660 : { *(.vector35) }
.vector36 0x9d00a680 : { *(.vector36) }
.vector37 0x9d00a6a0 : { *(.vector37) }
.vector38 0x9d00a6c0 : { *(.vector38) }
.vector39 0x9d00a6e0 : { *(.vector39) }
.vector40 0x9d00a700 : { *(.vector40) }
.vector41 0x9d00a720 : { *(.vector41) }
.vector42 0x9d00a740 : { *(.vector42) }
.vector43 0x9d00a760 : { *(.vector43) }
.startup 0x9d00b970 : { *(.startup) }
.debug_exception 0x9d00bcf0 : { *(.debug_exception) }
.text 0x9d00be00 : { *(.text .text.*) *(.rodata .rodata.*) }
.reset_handler 0x9d013a44 : { *(.reset_handler) }
.config2 0xbfc00bf4 : { *(.config2) }
.config1 0xbfc00bf8 : { *(.config1) }
.bss 0xa0000000 (NOLOAD) : { __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); __bss_end = .; }
.unexpected : { *(.data .data.* .sdata .sdata.*) }
ASSERT(SIZEOF(.unexpected)==0,"Initialized RAM requires an explicit initializer")
ASSERT(SIZEOF(.text)<=0x7c44,"Application overlaps fixed reset handler")
ASSERT(SIZEOF(.startup)<=0x1cc,"Startup overflow")
ASSERT(__bss_end<=0xa000e000,"Leave at least 8 KiB for stack")
/DISCARD/ : { *(.got .comment .note.* .MIPS.abiflags .reginfo .pdr .eh_frame) }
}
