// This file is generated from a similarly-named Perl script in the BoringSSL
// source tree. Do not edit by hand.

#if defined(__has_feature)
#if __has_feature(memory_sanitizer) && !defined(OPENSSL_NO_ASM)
#define OPENSSL_NO_ASM
#endif
#endif

#if !defined(OPENSSL_NO_ASM)
#if defined(__aarch64__)
#if defined(BORINGSSL_PREFIX)
#include <boringssl_prefix_symbols_asm.h>
#endif
.section	.rodata

.Lp503p1_nz_s8:
.quad	0x085BDA2211E7A0AC, 0x9BF6C87B7E7DAF13
.quad	0x45C6BDDA77A4D01B, 0x4066F541811E1E60

.Lp503x2:
.quad	0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFF
.quad	0x57FFFFFFFFFFFFFF, 0x2610B7B44423CF41
.quad	0x3737ED90F6FCFB5E, 0xC08B8D7BB4EF49A0
.quad	0x0080CDEA83023C3C

.text
.globl	sike_mpmul
.hidden	sike_mpmul
.align	4
sike_mpmul:
	stp	x29, x30, [sp,#-96]!
	add	x29, sp, #0
	stp	x19, x20, [sp,#16]
	stp	x21, x22, [sp,#32]
	stp	x23, x24, [sp,#48]
	stp	x25, x26, [sp,#64]
	stp	x27, x28, [sp,#80]

	ldp	x3, x4, [x0]
	ldp	x5, x6, [x0,#16]
	ldp	x7, x8, [x0,#32]
	ldp	x9, x10, [x0,#48]
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]
	ldp	x15, x16, [x1,#32]
	ldp	x17, x19, [x1,#48]

        // x3-x7 <- AH + AL, x7 <- carry
	adds	x3, x3, x7
	adcs	x4, x4, x8
	adcs	x5, x5, x9
	adcs	x6, x6, x10
	adc	x7, xzr, xzr

        // x11-x14 <- BH + BL, x8 <- carry
	adds	x11, x11, x15
	adcs	x12, x12, x16
	adcs	x13, x13, x17
	adcs	x14, x14, x19
	adc	x8, xzr, xzr

        // x9 <- combined carry
	and	x9, x7, x8
        // x7-x8 <- mask
	sub	x7, xzr, x7
	sub	x8, xzr, x8


        // x15-x19 <- masked (BH + BL)
	and	x15, x11, x7
	and	x16, x12, x7
	and	x17, x13, x7
	and	x19, x14, x7

        // x20-x23 <- masked (AH + AL)
	and	x20, x3, x8
	and	x21, x4, x8
	and	x22, x5, x8
	and	x23, x6, x8

        // x15-x19, x7 <- masked (AH+AL) + masked (BH+BL), step 1
	adds	x15, x15, x20
	adcs	x16, x16, x21
	adcs	x17, x17, x22
	adcs	x19, x19, x23
	adc	x7, x9, xzr

        // x8-x10,x20-x24 <- (AH+AL) x (BH+BL), low part
	stp	x3, x4, [x2,#0]
                // A0-A1 <- AH + AL, T0 <- mask
	adds	x3, x3, x5
	adcs	x4, x4, x6
	adc	x25, xzr, xzr

        // C6, T1 <- BH + BL, C7 <- mask
	adds	x23, x11, x13
	adcs	x26, x12, x14
	adc	x24, xzr, xzr

        // C0-C1 <- masked (BH + BL)
	sub	x10, xzr, x25
	sub	x20, xzr, x24
	and	x8, x23, x10
	and	x9, x26, x10

        // C4-C5 <- masked (AH + AL), T0 <- combined carry
	and	x21, x3, x20
	and	x22, x4, x20
	mul	x10, x3, x23
	mul	x20, x3, x26
	and	x25, x25, x24

        // C0-C1, T0 <- (AH+AL) x (BH+BL), part 1
	adds	x8, x21, x8
	umulh	x21, x3, x26
	adcs	x9, x22, x9
	umulh	x22, x3, x23
	adc	x25, x25, xzr

        // C2-C5 <- (AH+AL) x (BH+BL), low part
	mul	x3, x4, x23
	umulh	x23, x4, x23
	adds	x20, x20, x22
	adc	x21, x21, xzr

	mul	x24, x4, x26
	umulh	x26, x4, x26
	adds	x20, x20, x3
	adcs	x21, x21, x23
	adc	x22, xzr, xzr

	adds	x21, x21, x24
	adc	x22, x22, x26

	ldp	x3, x4, [x2,#0]

        // C2-C5, T0 <- (AH+AL) x (BH+BL), final part
	adds	x21, x8, x21
	umulh	x24, x3, x11
	umulh	x26, x3, x12
	adcs	x22, x9, x22
	mul	x8, x3, x11
	mul	x9, x3, x12
	adc	x25, x25, xzr

        // C0-C1, T1, C7 <- AL x BL
	mul	x3, x4, x11
	umulh	x11, x4, x11
	adds	x9, x9, x24
	adc	x26, x26, xzr

	mul	x23, x4, x12
	umulh	x12, x4, x12
	adds	x9, x9, x3
	adcs	x26, x26, x11
	adc	x24, xzr, xzr

	adds	x26, x26, x23
	adc	x24, x24, x12


        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL
	mul	x3, x5, x13
	umulh	x11, x5, x13
	subs	x10, x10, x8
	sbcs	x20, x20, x9
	sbcs	x21, x21, x26
	mul	x4, x5, x14
	umulh	x23, x5, x14
	sbcs	x22, x22, x24
	sbc	x25, x25, xzr

        // A0, A1, C6, B0 <- AH x BH
	mul	x5, x6, x13
	umulh	x13, x6, x13
	adds	x4, x4, x11
	adc	x23, x23, xzr

	mul	x12, x6, x14
	umulh	x14, x6, x14
	adds	x4, x4, x5
	adcs	x23, x23, x13
	adc	x11, xzr, xzr

	adds	x23, x23, x12
	adc	x11, x11, x14


        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
	subs	x10, x10, x3
	sbcs	x20, x20, x4
	sbcs	x21, x21, x23
	sbcs	x22, x22, x11
	sbc	x25, x25, xzr

	adds	x10, x10, x26
	adcs	x20, x20, x24
	adcs	x21, x21, x3
	adcs	x22, x22, x4
	adcs	x23, x25, x23
	adc	x24, x11, xzr


        // x15-x19, x7 <- (AH+AL) x (BH+BL), final step
	adds	x15, x15, x21
	adcs	x16, x16, x22
	adcs	x17, x17, x23
	adcs	x19, x19, x24
	adc	x7, x7, xzr

        // Load AL
	ldp	x3, x4, [x0]
	ldp	x5, x6, [x0,#16]
        // Load BL
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]

        // Temporarily store x8,x9 in x2
	stp	x8,x9, [x2,#0]
        // x21-x28 <- AL x BL
                // A0-A1 <- AH + AL, T0 <- mask
	adds	x3, x3, x5
	adcs	x4, x4, x6
	adc	x8, xzr, xzr

        // C6, T1 <- BH + BL, C7 <- mask
	adds	x27, x11, x13
	adcs	x9, x12, x14
	adc	x28, xzr, xzr

        // C0-C1 <- masked (BH + BL)
	sub	x23, xzr, x8
	sub	x24, xzr, x28
	and	x21, x27, x23
	and	x22, x9, x23

        // C4-C5 <- masked (AH + AL), T0 <- combined carry
	and	x25, x3, x24
	and	x26, x4, x24
	mul	x23, x3, x27
	mul	x24, x3, x9
	and	x8, x8, x28

        // C0-C1, T0 <- (AH+AL) x (BH+BL), part 1
	adds	x21, x25, x21
	umulh	x25, x3, x9
	adcs	x22, x26, x22
	umulh	x26, x3, x27
	adc	x8, x8, xzr

        // C2-C5 <- (AH+AL) x (BH+BL), low part
	mul	x3, x4, x27
	umulh	x27, x4, x27
	adds	x24, x24, x26
	adc	x25, x25, xzr

	mul	x28, x4, x9
	umulh	x9, x4, x9
	adds	x24, x24, x3
	adcs	x25, x25, x27
	adc	x26, xzr, xzr

	adds	x25, x25, x28
	adc	x26, x26, x9

	ldp	x3, x4, [x0,#0]

        // C2-C5, T0 <- (AH+AL) x (BH+BL), final part
	adds	x25, x21, x25
	umulh	x28, x3, x11
	umulh	x9, x3, x12
	adcs	x26, x22, x26
	mul	x21, x3, x11
	mul	x22, x3, x12
	adc	x8, x8, xzr

        // C0-C1, T1, C7 <- AL x BL
	mul	x3, x4, x11
	umulh	x11, x4, x11
	adds	x22, x22, x28
	adc	x9, x9, xzr

	mul	x27, x4, x12
	umulh	x12, x4, x12
	adds	x22, x22, x3
	adcs	x9, x9, x11
	adc	x28, xzr, xzr

	adds	x9, x9, x27
	adc	x28, x28, x12


        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL
	mul	x3, x5, x13
	umulh	x11, x5, x13
	subs	x23, x23, x21
	sbcs	x24, x24, x22
	sbcs	x25, x25, x9
	mul	x4, x5, x14
	umulh	x27, x5, x14
	sbcs	x26, x26, x28
	sbc	x8, x8, xzr

        // A0, A1, C6, B0 <- AH x BH
	mul	x5, x6, x13
	umulh	x13, x6, x13
	adds	x4, x4, x11
	adc	x27, x27, xzr

	mul	x12, x6, x14
	umulh	x14, x6, x14
	adds	x4, x4, x5
	adcs	x27, x27, x13
	adc	x11, xzr, xzr

	adds	x27, x27, x12
	adc	x11, x11, x14


        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
	subs	x23, x23, x3
	sbcs	x24, x24, x4
	sbcs	x25, x25, x27
	sbcs	x26, x26, x11
	sbc	x8, x8, xzr

	adds	x23, x23, x9
	adcs	x24, x24, x28
	adcs	x25, x25, x3
	adcs	x26, x26, x4
	adcs	x27, x8, x27
	adc	x28, x11, xzr

        // Restore x8,x9
	ldp	x8,x9, [x2,#0]

        // x8-x10,x20,x15-x17,x19 <- maskd (AH+AL) x (BH+BL) - ALxBL
	subs	x8, x8, x21
	sbcs	x9, x9, x22
	sbcs	x10, x10, x23
	sbcs	x20, x20, x24
	sbcs	x15, x15, x25
	sbcs	x16, x16, x26
	sbcs	x17, x17, x27
	sbcs	x19, x19, x28
	sbc	x7, x7, xzr

        // Store ALxBL, low
	stp	x21, x22, [x2]
	stp	x23, x24, [x2,#16]

        // Load AH
	ldp	x3, x4, [x0,#32]
	ldp	x5, x6, [x0,#48]
        // Load BH
	ldp	x11, x12, [x1,#32]
	ldp	x13, x14, [x1,#48]

	adds	x8, x8, x25
	adcs	x9, x9, x26
	adcs	x10, x10, x27
	adcs	x20, x20, x28
	adc	x1, xzr, xzr

	add	x0, x0, #32
        // Temporarily store x8,x9 in x2
	stp	x8,x9, [x2,#32]
        // x21-x28 <- AH x BH
                // A0-A1 <- AH + AL, T0 <- mask
	adds	x3, x3, x5
	adcs	x4, x4, x6
	adc	x8, xzr, xzr

        // C6, T1 <- BH + BL, C7 <- mask
	adds	x27, x11, x13
	adcs	x9, x12, x14
	adc	x28, xzr, xzr

        // C0-C1 <- masked (BH + BL)
	sub	x23, xzr, x8
	sub	x24, xzr, x28
	and	x21, x27, x23
	and	x22, x9, x23

        // C4-C5 <- masked (AH + AL), T0 <- combined carry
	and	x25, x3, x24
	and	x26, x4, x24
	mul	x23, x3, x27
	mul	x24, x3, x9
	and	x8, x8, x28

        // C0-C1, T0 <- (AH+AL) x (BH+BL), part 1
	adds	x21, x25, x21
	umulh	x25, x3, x9
	adcs	x22, x26, x22
	umulh	x26, x3, x27
	adc	x8, x8, xzr

        // C2-C5 <- (AH+AL) x (BH+BL), low part
	mul	x3, x4, x27
	umulh	x27, x4, x27
	adds	x24, x24, x26
	adc	x25, x25, xzr

	mul	x28, x4, x9
	umulh	x9, x4, x9
	adds	x24, x24, x3
	adcs	x25, x25, x27
	adc	x26, xzr, xzr

	adds	x25, x25, x28
	adc	x26, x26, x9

	ldp	x3, x4, [x0,#0]

        // C2-C5, T0 <- (AH+AL) x (BH+BL), final part
	adds	x25, x21, x25
	umulh	x28, x3, x11
	umulh	x9, x3, x12
	adcs	x26, x22, x26
	mul	x21, x3, x11
	mul	x22, x3, x12
	adc	x8, x8, xzr

        // C0-C1, T1, C7 <- AL x BL
	mul	x3, x4, x11
	umulh	x11, x4, x11
	adds	x22, x22, x28
	adc	x9, x9, xzr

	mul	x27, x4, x12
	umulh	x12, x4, x12
	adds	x22, x22, x3
	adcs	x9, x9, x11
	adc	x28, xzr, xzr

	adds	x9, x9, x27
	adc	x28, x28, x12


        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL
	mul	x3, x5, x13
	umulh	x11, x5, x13
	subs	x23, x23, x21
	sbcs	x24, x24, x22
	sbcs	x25, x25, x9
	mul	x4, x5, x14
	umulh	x27, x5, x14
	sbcs	x26, x26, x28
	sbc	x8, x8, xzr

        // A0, A1, C6, B0 <- AH x BH
	mul	x5, x6, x13
	umulh	x13, x6, x13
	adds	x4, x4, x11
	adc	x27, x27, xzr

	mul	x12, x6, x14
	umulh	x14, x6, x14
	adds	x4, x4, x5
	adcs	x27, x27, x13
	adc	x11, xzr, xzr

	adds	x27, x27, x12
	adc	x11, x11, x14


        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
	subs	x23, x23, x3
	sbcs	x24, x24, x4
	sbcs	x25, x25, x27
	sbcs	x26, x26, x11
	sbc	x8, x8, xzr

	adds	x23, x23, x9
	adcs	x24, x24, x28
	adcs	x25, x25, x3
	adcs	x26, x26, x4
	adcs	x27, x8, x27
	adc	x28, x11, xzr

        // Restore x8,x9
	ldp	x8,x9, [x2,#32]

	neg	x1, x1

        // x8-x10,x20,x15-x17,x19 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
	subs	x8, x8, x21
	sbcs	x9, x9, x22
	sbcs	x10, x10, x23
	sbcs	x20, x20, x24
	sbcs	x15, x15, x25
	sbcs	x16, x16, x26
	sbcs	x17, x17, x27
	sbcs	x19, x19, x28
	sbc	x7, x7, xzr

        // Store (AH+AL) x (BH+BL) - ALxBL - AHxBH, low
	stp	x8, x9, [x2,#32]
	stp	x10, x20, [x2,#48]

	adds	x1, x1, #1
	adcs	x15, x15, x21
	adcs	x16, x16, x22
	adcs	x17, x17, x23
	adcs	x19, x19, x24
	adcs	x25, x7, x25
	adcs	x26, x26, xzr
	adcs	x27, x27, xzr
	adc	x28, x28, xzr

	stp	x15, x16, [x2,#64]
	stp	x17, x19, [x2,#80]
	stp	x25, x26, [x2,#96]
	stp	x27, x28, [x2,#112]

	ldp	x19, x20, [x29,#16]
	ldp	x21, x22, [x29,#32]
	ldp	x23, x24, [x29,#48]
	ldp	x25, x26, [x29,#64]
	ldp	x27, x28, [x29,#80]
	ldp	x29, x30, [sp],#96
	ret
.globl	sike_fprdc
.hidden	sike_fprdc
.align	4
sike_fprdc:
	stp	x29, x30, [sp, #-112]!
	add	x29, sp, #0
	stp	x19, x20, [sp,#16]
	stp	x21, x22, [sp,#32]
	stp	x23, x24, [sp,#48]
	stp	x25, x26, [sp,#64]
	stp	x27, x28, [sp,#80]
	str	x1, [sp,#96]

	ldp	x2, x3, [x0,#0]       // a[0-1]

        // Load the prime constant
	adrp	x23, .Lp503p1_nz_s8
	add	x23, x23, :lo12:.Lp503p1_nz_s8
	ldp	x24, x25, [x23, #0]
	ldp	x26, x27, [x23, #16]

        // a[0-1] x .Lp503p1_nz_s8 --> result: x4:x9
	mul	x4, x2, x24           // a[0] x .Lp503p1_nz_s8[0]
	umulh	x7, x2, x24
	mul	x5, x2, x25           // a[0] x .Lp503p1_nz_s8[1]
	umulh	x6, x2, x25

	mul	x1, x3, x24
	umulh	x10, x3, x24
	adds	x5, x5, x7
	adc	x6, x6, xzr

	mul	x11, x2, x26
	umulh	x19, x2, x26
	adds	x5, x5, x1
	adcs	x6, x6, x10
	adc	x7, xzr, xzr

	mul	x1, x3, x25
	umulh	x10, x3, x25
	adds	x6, x6, x11
	adcs	x7, x7, x19
	adc	x8, xzr, xzr

	mul	x11, x2, x27
	umulh	x19, x2, x27
	adds	x6, x6, x1
	adcs	x7, x7, x10
	adc	x8, x8, xzr

	mul	x1, x3, x26
	umulh	x10, x3, x26
	adds	x7, x7, x11
	adcs	x8, x8, x19
	adc	x9, xzr, xzr

	mul	x11, x3, x27
	umulh	x19, x3, x27
	adds	x7, x7, x1
	adcs	x8, x8, x10
	adc	x9, x9, xzr
	adds	x8, x8, x11
	adc	x9, x9, x19



	ldp	x2,  x3, [x0,#16]     // a[2]
	ldp	x12, x13, [x0,#32]
	ldp	x14, x15, [x0,#48]

	orr	x10, xzr, x9, lsr #8
	lsl	x9, x9, #56
	orr	x9, x9, x8, lsr #8
	lsl	x8, x8, #56
	orr	x8, x8, x7, lsr #8
	lsl	x7, x7, #56
	orr	x7, x7, x6, lsr #8
	lsl	x6, x6, #56
	orr	x6, x6, x5, lsr #8
	lsl	x5, x5, #56
	orr	x5, x5, x4, lsr #8
	lsl	x4, x4, #56

	adds	x3, x4,  x3          // a[3]
	adcs	x12, x5, x12          // a[4]
	adcs	x13, x6, x13
	adcs	x14, x7, x14
	adcs	x15, x8, x15
	ldp	x16, x17, [x0,#64]
	ldp	x28, x30, [x0,#80]
	mul	x4,  x2, x24          // a[2] x .Lp503p1_nz_s8[0]
	umulh	x7,  x2, x24
	adcs	x16, x9, x16
	adcs	x17, x10, x17
	adcs	x28, xzr, x28
	adcs	x30, xzr, x30
	ldp	x20, x21, [x0,#96]
	ldp	x22, x23, [x0,#112]
	mul	x5,  x2, x25          // a[2] x .Lp503p1_nz_s8[1]
	umulh	x6,  x2, x25
	adcs	x20, xzr, x20
	adcs	x21, xzr, x21
	adcs	x22, xzr, x22
	adc	x23, xzr, x23

        // a[2-3] x .Lp503p1_nz_s8 --> result: x4:x9
	mul	x1, x3, x24
	umulh	x10, x3, x24
	adds	x5, x5, x7
	adc	x6, x6, xzr

	mul	x11, x2, x26
	umulh	x19, x2, x26
	adds	x5, x5, x1
	adcs	x6, x6, x10
	adc	x7, xzr, xzr

	mul	x1, x3, x25
	umulh	x10, x3, x25
	adds	x6, x6, x11
	adcs	x7, x7, x19
	adc	x8, xzr, xzr

	mul	x11, x2, x27
	umulh	x19, x2, x27
	adds	x6, x6, x1
	adcs	x7, x7, x10
	adc	x8, x8, xzr

	mul	x1, x3, x26
	umulh	x10, x3, x26
	adds	x7, x7, x11
	adcs	x8, x8, x19
	adc	x9, xzr, xzr

	mul	x11, x3, x27
	umulh	x19, x3, x27
	adds	x7, x7, x1
	adcs	x8, x8, x10
	adc	x9, x9, xzr
	adds	x8, x8, x11
	adc	x9, x9, x19



	orr	x10, xzr, x9, lsr #8
	lsl	x9, x9, #56
	orr	x9, x9, x8, lsr #8
	lsl	x8, x8, #56
	orr	x8, x8, x7, lsr #8
	lsl	x7, x7, #56
	orr	x7, x7, x6, lsr #8
	lsl	x6, x6, #56
	orr	x6, x6, x5, lsr #8
	lsl	x5, x5, #56
	orr	x5, x5, x4, lsr #8
	lsl	x4, x4, #56

	adds	x13, x4, x13          // a[5]
	adcs	x14, x5, x14          // a[6]
	adcs	x15, x6, x15
	adcs	x16, x7, x16
	mul	x4, x12, x24          // a[4] x .Lp503p1_nz_s8[0]
	umulh	x7, x12, x24
	adcs	x17, x8, x17
	adcs	x28, x9, x28
	adcs	x30, x10, x30
	adcs	x20, xzr, x20
	mul	x5, x12, x25          // a[4] x .Lp503p1_nz_s8[1]
	umulh	x6, x12, x25
	adcs	x21, xzr, x21
	adcs	x22, xzr, x22
	adc	x23, xzr, x23

        // a[4-5] x .Lp503p1_nz_s8 --> result: x4:x9
	mul	x1, x13, x24
	umulh	x10, x13, x24
	adds	x5, x5, x7
	adc	x6, x6, xzr

	mul	x11, x12, x26
	umulh	x19, x12, x26
	adds	x5, x5, x1
	adcs	x6, x6, x10
	adc	x7, xzr, xzr

	mul	x1, x13, x25
	umulh	x10, x13, x25
	adds	x6, x6, x11
	adcs	x7, x7, x19
	adc	x8, xzr, xzr

	mul	x11, x12, x27
	umulh	x19, x12, x27
	adds	x6, x6, x1
	adcs	x7, x7, x10
	adc	x8, x8, xzr

	mul	x1, x13, x26
	umulh	x10, x13, x26
	adds	x7, x7, x11
	adcs	x8, x8, x19
	adc	x9, xzr, xzr

	mul	x11, x13, x27
	umulh	x19, x13, x27
	adds	x7, x7, x1
	adcs	x8, x8, x10
	adc	x9, x9, xzr
	adds	x8, x8, x11
	adc	x9, x9, x19



	orr	x10, xzr, x9, lsr #8
	lsl	x9, x9, #56
	orr	x9, x9, x8, lsr #8
	lsl	x8, x8, #56
	orr	x8, x8, x7, lsr #8
	lsl	x7, x7, #56
	orr	x7, x7, x6, lsr #8
	lsl	x6, x6, #56
	orr	x6, x6, x5, lsr #8
	lsl	x5, x5, #56
	orr	x5, x5, x4, lsr #8
	lsl	x4, x4, #56

	adds	x15, x4, x15          // a[7]
	adcs	x16, x5, x16          // a[8]
	adcs	x17, x6, x17
	adcs	x28, x7, x28
	mul	x4, x14, x24          // a[6] x .Lp503p1_nz_s8[0]
	umulh	x7, x14, x24
	adcs	x30, x8, x30
	adcs	x20, x9, x20
	adcs	x21, x10, x21
	mul	x5, x14, x25          // a[6] x .Lp503p1_nz_s8[1]
	umulh	x6, x14, x25
	adcs	x22, xzr, x22
	adc	x23, xzr, x23

        // a[6-7] x .Lp503p1_nz_s8 --> result: x4:x9
	mul	x1, x15, x24
	umulh	x10, x15, x24
	adds	x5, x5, x7
	adc	x6, x6, xzr

	mul	x11, x14, x26
	umulh	x19, x14, x26
	adds	x5, x5, x1
	adcs	x6, x6, x10
	adc	x7, xzr, xzr

	mul	x1, x15, x25
	umulh	x10, x15, x25
	adds	x6, x6, x11
	adcs	x7, x7, x19
	adc	x8, xzr, xzr

	mul	x11, x14, x27
	umulh	x19, x14, x27
	adds	x6, x6, x1
	adcs	x7, x7, x10
	adc	x8, x8, xzr

	mul	x1, x15, x26
	umulh	x10, x15, x26
	adds	x7, x7, x11
	adcs	x8, x8, x19
	adc	x9, xzr, xzr

	mul	x11, x15, x27
	umulh	x19, x15, x27
	adds	x7, x7, x1
	adcs	x8, x8, x10
	adc	x9, x9, xzr
	adds	x8, x8, x11
	adc	x9, x9, x19



	orr	x10, xzr, x9, lsr #8
	lsl	x9, x9, #56
	orr	x9, x9, x8, lsr #8
	lsl	x8, x8, #56
	orr	x8, x8, x7, lsr #8
	lsl	x7, x7, #56
	orr	x7, x7, x6, lsr #8
	lsl	x6, x6, #56
	orr	x6, x6, x5, lsr #8
	lsl	x5, x5, #56
	orr	x5, x5, x4, lsr #8
	lsl	x4, x4, #56

	adds	x17, x4, x17
	adcs	x28, x5, x28
	ldr	x1, [sp,#96]
	adcs	x30, x6, x30
	adcs	x20, x7, x20
	stp	x16, x17, [x1,#0]     // Final result
	stp	x28, x30, [x1,#16]
	adcs	x21, x8, x21
	adcs	x22, x9, x22
	adc	x23, x10, x23
	stp	x20, x21, [x1,#32]
	stp	x22, x23, [x1,#48]

	ldp	x19, x20, [x29,#16]
	ldp	x21, x22, [x29,#32]
	ldp	x23, x24, [x29,#48]
	ldp	x25, x26, [x29,#64]
	ldp	x27, x28, [x29,#80]
	ldp	x29, x30, [sp],#112
	ret

.globl	sike_fpadd
.hidden	sike_fpadd
.align	4
sike_fpadd:
	stp	x29,x30, [sp,#-16]!
	add	x29, sp, #0

	ldp	x3, x4,   [x0,#0]
	ldp	x5, x6,   [x0,#16]
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]

        // Add a + b
	adds	x3, x3, x11
	adcs	x4, x4, x12
	adcs	x5, x5, x13
	adcs	x6, x6, x14
	ldp	x7, x8,   [x0,#32]
	ldp	x9, x10,  [x0,#48]
	ldp	x11, x12, [x1,#32]
	ldp	x13, x14, [x1,#48]
	adcs	x7, x7, x11
	adcs	x8, x8, x12
	adcs	x9, x9, x13
	adc	x10, x10, x14

        //  Subtract 2xp503
	adrp	x17, .Lp503x2
	add	x17, x17, :lo12:.Lp503x2
	ldp	x11, x12, [x17, #0]
	ldp	x13, x14, [x17, #16]
	subs	x3, x3, x11
	sbcs	x4, x4, x12
	sbcs	x5, x5, x12
	sbcs	x6, x6, x13
	sbcs	x7, x7, x14

	ldp	x15, x16, [x17, #32]
	ldr	x17,      [x17, #48]
	sbcs	x8, x8, x15
	sbcs	x9, x9, x16
	sbcs	x10, x10, x17
	sbc	x0, xzr, xzr    // x0 can be reused now

        // Add 2xp503 anded with the mask in x0
	and	x11, x11, x0
	and	x12, x12, x0
	and	x13, x13, x0
	and	x14, x14, x0
	and	x15, x15, x0
	and	x16, x16, x0
	and	x17, x17, x0

	adds	x3, x3, x11
	adcs	x4, x4, x12
	adcs	x5, x5, x12
	adcs	x6, x6, x13
	adcs	x7, x7, x14
	adcs	x8, x8, x15
	adcs	x9, x9, x16
	adc	x10, x10, x17

	stp	x3, x4,  [x2,#0]
	stp	x5, x6,  [x2,#16]
	stp	x7, x8,  [x2,#32]
	stp	x9, x10, [x2,#48]

	ldp	x29, x30, [sp],#16
	ret

.globl	sike_fpsub
.hidden	sike_fpsub
.align	4
sike_fpsub:
	stp	x29, x30, [sp,#-16]!
	add	x29, sp, #0

	ldp	x3, x4,   [x0,#0]
	ldp	x5, x6,   [x0,#16]
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]

        // Subtract a - b
	subs	x3, x3, x11
	sbcs	x4, x4, x12
	sbcs	x5, x5, x13
	sbcs	x6, x6, x14
	ldp	x7, x8,   [x0,#32]
	ldp	x11, x12, [x1,#32]
	sbcs	x7, x7, x11
	sbcs	x8, x8, x12
	ldp	x9, x10,  [x0,#48]
	ldp	x11, x12, [x1,#48]
	sbcs	x9, x9, x11
	sbcs	x10, x10, x12
	sbc	x17, xzr, xzr

        // Add 2xp503 anded with the mask in x17
	adrp	x16, .Lp503x2
	add	x16, x16, :lo12:.Lp503x2

        // First half
	ldp	x11, x12, [x16, #0]
	ldp	x13, x14, [x16, #16]
	and	x11, x11, x17
	and	x12, x12, x17
	and	x13, x13, x17
	adds	x3, x3, x11
	adcs	x4, x4, x12
	adcs	x5, x5, x12
	adcs	x6, x6, x13
	stp	x3, x4,  [x2,#0]
	stp	x5, x6,  [x2,#16]

        // Second half
	ldp	x11, x12, [x16, #32]
	ldr	x13,      [x16, #48]
	and	x14, x14, x17
	and	x11, x11, x17
	and	x12, x12, x17
	and	x13, x13, x17
	adcs	x7, x7, x14
	adcs	x8, x8, x11
	adcs	x9, x9, x12
	adc	x10, x10, x13
	stp	x7, x8,  [x2,#32]
	stp	x9, x10, [x2,#48]

	ldp	x29, x30, [sp],#16
	ret
.globl	sike_mpadd_asm
.hidden	sike_mpadd_asm
.align	4
sike_mpadd_asm:
	stp	x29, x30, [sp,#-16]!
	add	x29, sp, #0

	ldp	x3, x4,   [x0,#0]
	ldp	x5, x6,   [x0,#16]
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]

	adds	x3, x3, x11
	adcs	x4, x4, x12
	adcs	x5, x5, x13
	adcs	x6, x6, x14
	ldp	x7, x8,   [x0,#32]
	ldp	x9, x10,  [x0,#48]
	ldp	x11, x12, [x1,#32]
	ldp	x13, x14, [x1,#48]
	adcs	x7, x7, x11
	adcs	x8, x8, x12
	adcs	x9, x9, x13
	adc	x10, x10, x14

	stp	x3, x4,   [x2,#0]
	stp	x5, x6,   [x2,#16]
	stp	x7, x8,   [x2,#32]
	stp	x9, x10,  [x2,#48]

	ldp	x29, x30, [sp],#16
	ret
.globl	sike_mpadd503x2_asm
.hidden	sike_mpadd503x2_asm
.align	4
sike_mpadd503x2_asm:
	stp	x29, x30, [sp,#-16]!
	add	x29, sp, #0

	ldp	x3, x4,   [x0,#0]
	ldp	x5, x6,   [x0,#16]
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]
	adds	x3, x3, x11
	adcs	x4, x4, x12
	adcs	x5, x5, x13
	adcs	x6, x6, x14
	ldp	x7, x8,   [x0,#32]
	ldp	x9, x10,  [x0,#48]
	ldp	x11, x12, [x1,#32]
	ldp	x13, x14, [x1,#48]
	adcs	x7, x7, x11
	adcs	x8, x8, x12
	adcs	x9, x9, x13
	adcs	x10, x10, x14

	stp	x3, x4,   [x2,#0]
	stp	x5, x6,   [x2,#16]
	stp	x7, x8,   [x2,#32]
	stp	x9, x10,  [x2,#48]

	ldp	x3, x4,   [x0,#64]
	ldp	x5, x6,   [x0,#80]
	ldp	x11, x12, [x1,#64]
	ldp	x13, x14, [x1,#80]
	adcs	x3, x3, x11
	adcs	x4, x4, x12
	adcs	x5, x5, x13
	adcs	x6, x6, x14
	ldp	x7, x8,   [x0,#96]
	ldp	x9, x10,  [x0,#112]
	ldp	x11, x12, [x1,#96]
	ldp	x13, x14, [x1,#112]
	adcs	x7, x7, x11
	adcs	x8, x8, x12
	adcs	x9, x9, x13
	adc	x10, x10, x14

	stp	x3, x4,   [x2,#64]
	stp	x5, x6,   [x2,#80]
	stp	x7, x8,   [x2,#96]
	stp	x9, x10,  [x2,#112]

	ldp	x29, x30, [sp],#16
	ret
.globl	sike_mpsubx2_asm
.hidden	sike_mpsubx2_asm
.align	4
sike_mpsubx2_asm:
	stp	x29, x30, [sp,#-16]!
	add	x29, sp, #0

	ldp	x3, x4,   [x0,#0]
	ldp	x5, x6,   [x0,#16]
	ldp	x11, x12, [x1,#0]
	ldp	x13, x14, [x1,#16]
	subs	x3, x3, x11
	sbcs	x4, x4, x12
	sbcs	x5, x5, x13
	sbcs	x6, x6, x14
	ldp	x7, x8,   [x0,#32]
	ldp	x9, x10,  [x0,#48]
	ldp	x11, x12, [x1,#32]
	ldp	x13, x14, [x1,#48]
	sbcs	x7, x7, x11
	sbcs	x8, x8, x12
	sbcs	x9, x9, x13
	sbcs	x10, x10, x14

	stp	x3, x4,   [x2,#0]
	stp	x5, x6,   [x2,#16]
	stp	x7, x8,   [x2,#32]
	stp	x9, x10,  [x2,#48]

	ldp	x3, x4,   [x0,#64]
	ldp	x5, x6,   [x0,#80]
	ldp	x11, x12, [x1,#64]
	ldp	x13, x14, [x1,#80]
	sbcs	x3, x3, x11
	sbcs	x4, x4, x12
	sbcs	x5, x5, x13
	sbcs	x6, x6, x14
	ldp	x7, x8,   [x0,#96]
	ldp	x9, x10,  [x0,#112]
	ldp	x11, x12, [x1,#96]
	ldp	x13, x14, [x1,#112]
	sbcs	x7, x7, x11
	sbcs	x8, x8, x12
	sbcs	x9, x9, x13
	sbcs	x10, x10, x14
	sbc	x0, xzr, xzr

	stp	x3, x4,   [x2,#64]
	stp	x5, x6,   [x2,#80]
	stp	x7, x8,   [x2,#96]
	stp	x9, x10,  [x2,#112]

	ldp	x29, x30, [sp],#16
	ret
.globl	sike_mpdblsubx2_asm
.hidden	sike_mpdblsubx2_asm
.align	4
sike_mpdblsubx2_asm:
	stp	x29, x30, [sp, #-64]!
	add	x29, sp, #0

	stp	x20, x21, [sp, #16]
	stp	x22, x23, [sp, #32]
	str	x24,      [sp, #48]

	ldp	x3, x4,   [x2,#0]
	ldp	x5, x6,   [x2,#16]
	ldp	x7, x8,   [x2,#32]
	ldp	x9, x10,  [x2,#48]
	ldp	x11, x12, [x2,#64]
	ldp	x13, x14, [x2,#80]
	ldp	x15, x16, [x2,#96]
	ldp	x17, x24, [x2,#112]

	ldp	x20, x21, [x0,#0]
	ldp	x22, x23, [x0,#16]
	subs	x3, x3, x20
	sbcs	x4, x4, x21
	sbcs	x5, x5, x22
	sbcs	x6, x6, x23
	ldp	x20, x21, [x0,#32]
	ldp	x22, x23, [x0,#48]
	sbcs	x7, x7, x20
	sbcs	x8, x8, x21
	sbcs	x9, x9, x22
	sbcs	x10, x10, x23
	ldp	x20, x21, [x0,#64]
	ldp	x22, x23, [x0,#80]
	sbcs	x11, x11, x20
	sbcs	x12, x12, x21
	sbcs	x13, x13, x22
	sbcs	x14, x14, x23
	ldp	x20, x21, [x0,#96]
	ldp	x22, x23, [x0,#112]
	sbcs	x15, x15, x20
	sbcs	x16, x16, x21
	sbcs	x17, x17, x22
	sbc	x24, x24, x23

	ldp	x20, x21, [x1,#0]
	ldp	x22, x23, [x1,#16]
	subs	x3, x3, x20
	sbcs	x4, x4, x21
	sbcs	x5, x5, x22
	sbcs	x6, x6, x23
	ldp	x20, x21, [x1,#32]
	ldp	x22, x23, [x1,#48]
	sbcs	x7, x7, x20
	sbcs	x8, x8, x21
	sbcs	x9, x9, x22
	sbcs	x10, x10, x23
	ldp	x20, x21, [x1,#64]
	ldp	x22, x23, [x1,#80]
	sbcs	x11, x11, x20
	sbcs	x12, x12, x21
	sbcs	x13, x13, x22
	sbcs	x14, x14, x23
	ldp	x20, x21, [x1,#96]
	ldp	x22, x23, [x1,#112]
	sbcs	x15, x15, x20
	sbcs	x16, x16, x21
	sbcs	x17, x17, x22
	sbc	x24, x24, x23

	stp	x3, x4,   [x2,#0]
	stp	x5, x6,   [x2,#16]
	stp	x7, x8,   [x2,#32]
	stp	x9, x10,  [x2,#48]
	stp	x11, x12, [x2,#64]
	stp	x13, x14, [x2,#80]
	stp	x15, x16, [x2,#96]
	stp	x17, x24, [x2,#112]

	ldp	x20, x21, [x29,#16]
	ldp	x22, x23, [x29,#32]
	ldr	x24,      [x29,#48]

	ldp	x29, x30, [sp],#64
	ret
#endif
#endif  // !OPENSSL_NO_ASM
