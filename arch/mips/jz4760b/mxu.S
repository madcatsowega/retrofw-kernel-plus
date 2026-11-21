/*
 * Ingenic JZ4760B MXU SIMD register context-switch save/restore.
 *
 * xr[0..14] holds xr1..xr15; xr[15] (offset 60) holds xr16 (MXU control reg).
 * Bit 0 of xr16 is the MXU enable flag: tasks that have not enabled MXU skip
 * the 15-register save/restore in ~6 cycles.
 *
 * All MXU opcodes are emitted via verified .word encodings so any MIPS32
 * binutils can assemble this file.
 */
#include <asm/asm.h>
#include <asm/regdef.h>

	.text
	.set	noreorder

/* void jz_mxu_save(unsigned int xr[16]) */
LEAF(jz_mxu_save)
	.word	0x7002042e		/* S32M2I xr16, v0 */
	sw	v0, 60(a0)
	andi	v1, v0, 1
	beqz	v1, 1f
	nop
	.word	0x70800051		/* S32STD xr1,  a0, 0  */
	.word	0x70800491		/* S32STD xr2,  a0, 4  */
	.word	0x708008d1		/* S32STD xr3,  a0, 8  */
	.word	0x70800d11		/* S32STD xr4,  a0, 12 */
	.word	0x70801151		/* S32STD xr5,  a0, 16 */
	.word	0x70801591		/* S32STD xr6,  a0, 20 */
	.word	0x708019d1		/* S32STD xr7,  a0, 24 */
	.word	0x70801e11		/* S32STD xr8,  a0, 28 */
	.word	0x70802251		/* S32STD xr9,  a0, 32 */
	.word	0x70802691		/* S32STD xr10, a0, 36 */
	.word	0x70802ad1		/* S32STD xr11, a0, 40 */
	.word	0x70802f11		/* S32STD xr12, a0, 44 */
	.word	0x70803351		/* S32STD xr13, a0, 48 */
	.word	0x70803791		/* S32STD xr14, a0, 52 */
	.word	0x70803bd1		/* S32STD xr15, a0, 56 */
1:
	jr	ra
	nop
	END(jz_mxu_save)

/* void jz_mxu_restore(const unsigned int xr[16]) */
LEAF(jz_mxu_restore)
	lw	v0, 60(a0)
	andi	v1, v0, 1
	beqz	v1, 1f
	nop
	.word	0x7002042f		/* S32I2M xr16, v0 (enable MXU) */
	nop				/* JZ4760B XBurst requires >=3 cycles */
	nop				/* after enabling xr16 before S32LDD  */
	nop
	nop
	.word	0x70800050		/* S32LDD xr1,  a0, 0  */
	.word	0x70800490		/* S32LDD xr2,  a0, 4  */
	.word	0x708008d0		/* S32LDD xr3,  a0, 8  */
	.word	0x70800d10		/* S32LDD xr4,  a0, 12 */
	.word	0x70801150		/* S32LDD xr5,  a0, 16 */
	.word	0x70801590		/* S32LDD xr6,  a0, 20 */
	.word	0x708019d0		/* S32LDD xr7,  a0, 24 */
	.word	0x70801e10		/* S32LDD xr8,  a0, 28 */
	.word	0x70802250		/* S32LDD xr9,  a0, 32 */
	.word	0x70802690		/* S32LDD xr10, a0, 36 */
	.word	0x70802ad0		/* S32LDD xr11, a0, 40 */
	.word	0x70802f10		/* S32LDD xr12, a0, 44 */
	.word	0x70803350		/* S32LDD xr13, a0, 48 */
	.word	0x70803790		/* S32LDD xr14, a0, 52 */
	.word	0x70803bd0		/* S32LDD xr15, a0, 56 */
	jr	ra
	nop
1:
	.word	0x7002042f		/* S32I2M xr16, v0 (disable MXU) */
	jr	ra
	nop
	END(jz_mxu_restore)

/* void jz_mxu_init(unsigned int xr[16]) */
LEAF(jz_mxu_init)
	.word	0x7000042f		/* S32I2M xr16, $zero */
	sw	zero, 0(a0)
	sw	zero, 4(a0)
	sw	zero, 8(a0)
	sw	zero, 12(a0)
	sw	zero, 16(a0)
	sw	zero, 20(a0)
	sw	zero, 24(a0)
	sw	zero, 28(a0)
	sw	zero, 32(a0)
	sw	zero, 36(a0)
	sw	zero, 40(a0)
	sw	zero, 44(a0)
	sw	zero, 48(a0)
	sw	zero, 52(a0)
	sw	zero, 56(a0)
	sw	zero, 60(a0)
	jr	ra
	nop
	END(jz_mxu_init)
