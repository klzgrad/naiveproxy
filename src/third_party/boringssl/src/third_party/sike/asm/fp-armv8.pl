#! /usr/bin/env perl
#
# April 2019
#
# Abstract: field arithmetic in aarch64 assembly for SIDH/p503

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../../crypto/perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$PREFIX="sike";

$code.=<<___;
.section  .rodata

.Lp503p1_nz_s8:
    .quad  0x085BDA2211E7A0AC, 0x9BF6C87B7E7DAF13
    .quad  0x45C6BDDA77A4D01B, 0x4066F541811E1E60

.Lp503x2:
    .quad  0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFF
    .quad  0x57FFFFFFFFFFFFFF, 0x2610B7B44423CF41
    .quad  0x3737ED90F6FCFB5E, 0xC08B8D7BB4EF49A0
    .quad  0x0080CDEA83023C3C

.text
___

# C[0-2] = A[0] * B[0-1]
sub mul64x128_comba_cut {
    my ($A0,$B0,$B1,$C0,$C1,$C2,$T0,$T1)=@_;
    my $body=<<___;
        mul     $T1, $A0, $B0
        umulh   $B0, $A0, $B0
        adds    $C0, $C0, $C2
        adc     $C1, $C1, xzr

        mul     $T0, $A0, $B1
        umulh   $B1, $A0, $B1
        adds    $C0, $C0, $T1
        adcs    $C1, $C1, $B0
        adc     $C2, xzr, xzr

        adds    $C1, $C1, $T0
        adc     $C2, $C2, $B1
___
    return $body;
}

sub mul256_karatsuba_comba {
    my ($M,$A0,$A1,$A2,$A3,$B0,$B1,$B2,$B3,$C0,$C1,$C2,$C3,$C4,$C5,$C6,$C7,$T0,$T1)=@_;
    # (AH+AL) x (BH+BL), low part
    my $mul_low=&mul64x128_comba_cut($A1, $C6, $T1, $C3, $C4, $C5, $C7, $A0);
    # AL x BL
    my $mul_albl=&mul64x128_comba_cut($A1, $B0, $B1, $C1, $T1, $C7, $C6, $A0);
    # AH x BH
    my $mul_ahbh=&mul64x128_comba_cut($A3, $B2, $B3, $A1, $C6, $B0, $B1, $A2);
    my $body=<<___;
        // A0-A1 <- AH + AL, T0 <- mask
        adds    $A0, $A0, $A2
        adcs    $A1, $A1, $A3
        adc     $T0, xzr, xzr

        // C6, T1 <- BH + BL, C7 <- mask
        adds    $C6, $B0, $B2
        adcs    $T1, $B1, $B3
        adc     $C7, xzr, xzr

        // C0-C1 <- masked (BH + BL)
        sub     $C2, xzr, $T0
        sub     $C3, xzr, $C7
        and     $C0, $C6, $C2
        and     $C1, $T1, $C2

        // C4-C5 <- masked (AH + AL), T0 <- combined carry
        and     $C4, $A0, $C3
        and     $C5, $A1, $C3
        mul     $C2, $A0, $C6
        mul     $C3, $A0, $T1
        and     $T0, $T0, $C7

        // C0-C1, T0 <- (AH+AL) x (BH+BL), part 1
        adds    $C0, $C4, $C0
        umulh   $C4, $A0, $T1
        adcs    $C1, $C5, $C1
        umulh   $C5, $A0, $C6
        adc     $T0, $T0, xzr

        // C2-C5 <- (AH+AL) x (BH+BL), low part
        $mul_low
        ldp     $A0, $A1, [$M,#0]

        // C2-C5, T0 <- (AH+AL) x (BH+BL), final part
        adds    $C4, $C0, $C4
        umulh   $C7, $A0, $B0
        umulh   $T1, $A0, $B1
        adcs    $C5, $C1, $C5
        mul     $C0, $A0, $B0
        mul     $C1, $A0, $B1
        adc     $T0, $T0, xzr

        // C0-C1, T1, C7 <- AL x BL
        $mul_albl

        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL
        mul     $A0, $A2, $B2
        umulh   $B0, $A2, $B2
        subs    $C2, $C2, $C0
        sbcs    $C3, $C3, $C1
        sbcs    $C4, $C4, $T1
        mul     $A1, $A2, $B3
        umulh   $C6, $A2, $B3
        sbcs    $C5, $C5, $C7
        sbc     $T0, $T0, xzr

        // A0, A1, C6, B0 <- AH x BH
        $mul_ahbh

        // C2-C5, T0 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
        subs    $C2, $C2, $A0
        sbcs    $C3, $C3, $A1
        sbcs    $C4, $C4, $C6
        sbcs    $C5, $C5, $B0
        sbc     $T0, $T0, xzr

        adds    $C2, $C2, $T1
        adcs    $C3, $C3, $C7
        adcs    $C4, $C4, $A0
        adcs    $C5, $C5, $A1
        adcs    $C6, $T0, $C6
        adc     $C7, $B0, xzr
___
    return $body;
}

# 512-bit integer multiplication using Karatsuba (two levels),
# Comba (lower level).
# Operation: c [x2] = a [x0] * b [x1]
sub mul {
    # (AH+AL) x (BH+BL), low part
    my $mul_kc_low=&mul256_karatsuba_comba(
        "x2",                                           # M0
        "x3","x4","x5","x6",                            # A0-A3
        "x11","x12","x13","x14",                        # B0-B3
        "x8","x9","x10","x20","x21","x22","x23","x24",  # C0-C7
        "x25","x26");                                   # TMP
    # AL x BL
    my $mul_albl=&mul256_karatsuba_comba(
        "x0",                                           # M0
        "x3","x4","x5","x6",                            # A0-A3
        "x11","x12","x13","x14",                        # B0-B3
        "x21","x22","x23","x24","x25","x26","x27","x28",# C0-C7
        "x8","x9");                                     # TMP
    # AH x BH
    my $mul_ahbh=&mul256_karatsuba_comba(
        "x0",                                           # M0
        "x3","x4","x5","x6",                            # A0-A3
        "x11","x12","x13","x14",                        # B0-B3
        "x21","x22","x23","x24","x25","x26","x27","x28",# C0-C7
        "x8","x9");                                     # TMP

    my $body=<<___;
        .global ${PREFIX}_mpmul
        .align 4
        ${PREFIX}_mpmul:
        stp     x29, x30, [sp,#-96]!
        add     x29, sp, #0
        stp     x19, x20, [sp,#16]
        stp     x21, x22, [sp,#32]
        stp     x23, x24, [sp,#48]
        stp     x25, x26, [sp,#64]
        stp     x27, x28, [sp,#80]

        ldp     x3, x4, [x0]
        ldp     x5, x6, [x0,#16]
        ldp     x7, x8, [x0,#32]
        ldp     x9, x10, [x0,#48]
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]
        ldp     x15, x16, [x1,#32]
        ldp     x17, x19, [x1,#48]

        // x3-x7 <- AH + AL, x7 <- carry
        adds    x3, x3, x7
        adcs    x4, x4, x8
        adcs    x5, x5, x9
        adcs    x6, x6, x10
        adc     x7, xzr, xzr

        // x11-x14 <- BH + BL, x8 <- carry
        adds    x11, x11, x15
        adcs    x12, x12, x16
        adcs    x13, x13, x17
        adcs    x14, x14, x19
        adc     x8, xzr, xzr

        // x9 <- combined carry
        and      x9, x7, x8
        // x7-x8 <- mask
        sub      x7, xzr, x7
        sub      x8, xzr, x8


        // x15-x19 <- masked (BH + BL)
        and     x15, x11, x7
        and     x16, x12, x7
        and     x17, x13, x7
        and     x19, x14, x7

        // x20-x23 <- masked (AH + AL)
        and     x20, x3, x8
        and     x21, x4, x8
        and     x22, x5, x8
        and     x23, x6, x8

        // x15-x19, x7 <- masked (AH+AL) + masked (BH+BL), step 1
        adds    x15, x15, x20
        adcs    x16, x16, x21
        adcs    x17, x17, x22
        adcs    x19, x19, x23
        adc     x7, x9, xzr

        // x8-x10,x20-x24 <- (AH+AL) x (BH+BL), low part
        stp     x3, x4, [x2,#0]
        $mul_kc_low

        // x15-x19, x7 <- (AH+AL) x (BH+BL), final step
        adds    x15, x15, x21
        adcs    x16, x16, x22
        adcs    x17, x17, x23
        adcs    x19, x19, x24
        adc     x7, x7, xzr

        // Load AL
        ldp     x3, x4, [x0]
        ldp     x5, x6, [x0,#16]
        // Load BL
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]

        // Temporarily store x8,x9 in x2
        stp     x8,x9, [x2,#0]
        // x21-x28 <- AL x BL
        $mul_albl
        // Restore x8,x9
        ldp     x8,x9, [x2,#0]

        // x8-x10,x20,x15-x17,x19 <- maskd (AH+AL) x (BH+BL) - ALxBL
        subs    x8, x8, x21
        sbcs    x9, x9, x22
        sbcs    x10, x10, x23
        sbcs    x20, x20, x24
        sbcs    x15, x15, x25
        sbcs    x16, x16, x26
        sbcs    x17, x17, x27
        sbcs    x19, x19, x28
        sbc     x7, x7, xzr

        // Store ALxBL, low
        stp     x21, x22, [x2]
        stp     x23, x24, [x2,#16]

        // Load AH
        ldp     x3, x4, [x0,#32]
        ldp     x5, x6, [x0,#48]
        // Load BH
        ldp     x11, x12, [x1,#32]
        ldp     x13, x14, [x1,#48]

        adds    x8, x8, x25
        adcs    x9, x9, x26
        adcs    x10, x10, x27
        adcs    x20, x20, x28
        adc     x1, xzr, xzr

        add     x0, x0, #32
        // Temporarily store x8,x9 in x2
        stp     x8,x9, [x2,#32]
        // x21-x28 <- AH x BH
        $mul_ahbh
        // Restore x8,x9
        ldp     x8,x9, [x2,#32]

        neg     x1, x1

        // x8-x10,x20,x15-x17,x19 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
        subs    x8, x8, x21
        sbcs    x9, x9, x22
        sbcs    x10, x10, x23
        sbcs    x20, x20, x24
        sbcs    x15, x15, x25
        sbcs    x16, x16, x26
        sbcs    x17, x17, x27
        sbcs    x19, x19, x28
        sbc     x7, x7, xzr

        // Store (AH+AL) x (BH+BL) - ALxBL - AHxBH, low
        stp     x8, x9, [x2,#32]
        stp     x10, x20, [x2,#48]

        adds    x1, x1, #1
        adcs    x15, x15, x21
        adcs    x16, x16, x22
        adcs    x17, x17, x23
        adcs    x19, x19, x24
        adcs    x25, x7, x25
        adcs    x26, x26, xzr
        adcs    x27, x27, xzr
        adc     x28, x28, xzr

        stp     x15, x16, [x2,#64]
        stp     x17, x19, [x2,#80]
        stp     x25, x26, [x2,#96]
        stp     x27, x28, [x2,#112]

        ldp     x19, x20, [x29,#16]
        ldp     x21, x22, [x29,#32]
        ldp     x23, x24, [x29,#48]
        ldp     x25, x26, [x29,#64]
        ldp     x27, x28, [x29,#80]
        ldp     x29, x30, [sp],#96
        ret
___
    return $body;
}
$code.=&mul();

# Computes C0-C4 = (A0-A1) * (B0-B3)
# Inputs remain intact
sub mul128x256_comba {
    my ($A0,$A1,$B0,$B1,$B2,$B3,$C0,$C1,$C2,$C3,$C4,$T0,$T1,$T2,$T3)=@_;
    my $body=<<___;
        mul     $T0, $A1, $B0
        umulh   $T1, $A1, $B0
        adds    $C0, $C0, $C2
        adc     $C1, $C1, xzr

        mul     $T2, $A0, $B2
        umulh   $T3, $A0, $B2
        adds    $C0, $C0, $T0
        adcs    $C1, $C1, $T1
        adc     $C2, xzr, xzr

        mul     $T0, $A1, $B1
        umulh   $T1, $A1, $B1
        adds    $C1, $C1, $T2
        adcs    $C2, $C2, $T3
        adc     $C3, xzr, xzr

        mul     $T2, $A0, $B3
        umulh   $T3, $A0, $B3
        adds    $C1, $C1, $T0
        adcs    $C2, $C2, $T1
        adc     $C3, $C3, xzr

        mul     $T0, $A1, $B2
        umulh   $T1, $A1, $B2
        adds    $C2, $C2, $T2
        adcs    $C3, $C3, $T3
        adc     $C4, xzr, xzr

        mul     $T2, $A1, $B3
        umulh   $T3, $A1, $B3
        adds    $C2, $C2, $T0
        adcs    $C3, $C3, $T1
        adc     $C4, $C4, xzr
        adds    $C3, $C3, $T2
        adc     $C4, $C4, $T3

___
    return $body;
}

#  Montgomery reduction
#  Based on method described in Faz-Hernandez et al. https://eprint.iacr.org/2017/1015
#  Operation: mc [x1] = ma [x0]
#  NOTE: ma=mc is not allowed
sub rdc {
    my $mul01=&mul128x256_comba(
        "x2","x3",                  # A0-A1
        "x24","x25","x26","x27",    # B0-B3
        "x5","x6","x7","x8","x9",   # C0-B4
        "x1","x10","x11","x19");    # TMP
    my $mul23=&mul128x256_comba(
        "x2","x3",                  # A0-A1
        "x24","x25","x26","x27",    # B0-B3
        "x5","x6","x7","x8","x9",   # C0-C4
        "x1","x10","x11","x19");    # TMP
    my $mul45=&mul128x256_comba(
        "x12","x13",                # A0-A1
        "x24","x25","x26","x27",    # B0-B3
        "x5","x6","x7","x8","x9",   # C0-C4
        "x1","x10","x11","x19");    # TMP
    my $mul67=&mul128x256_comba(
        "x14","x15",                # A0-A1
        "x24","x25","x26","x27",    # B0-B3
        "x5","x6","x7","x8","x9",   # C0-C4
        "x1","x10","x11","x19");    # TMP
    my $body=<<___;
    .global ${PREFIX}_fprdc
    .align 4
    ${PREFIX}_fprdc:
        stp     x29, x30, [sp, #-112]!
        add     x29, sp, #0
        stp     x19, x20, [sp,#16]
        stp     x21, x22, [sp,#32]
        stp     x23, x24, [sp,#48]
        stp     x25, x26, [sp,#64]
        stp     x27, x28, [sp,#80]
        str     x1, [sp,#96]

        ldp     x2, x3, [x0,#0]       // a[0-1]

        // Load the prime constant
        adrp    x23, :pg_hi21:.Lp503p1_nz_s8
        add     x23, x23, :lo12:.Lp503p1_nz_s8
        ldp     x24, x25, [x23, #0]
        ldp     x26, x27, [x23, #16]

        // a[0-1] x .Lp503p1_nz_s8 --> result: x4:x9
        mul     x4, x2, x24           // a[0] x .Lp503p1_nz_s8[0]
        umulh   x7, x2, x24
        mul     x5, x2, x25           // a[0] x .Lp503p1_nz_s8[1]
        umulh   x6, x2, x25

        $mul01

        ldp      x2,  x3, [x0,#16]     // a[2]
        ldp     x12, x13, [x0,#32]
        ldp     x14, x15, [x0,#48]

        orr     x10, xzr, x9, lsr #8
        lsl     x9, x9, #56
        orr     x9, x9, x8, lsr #8
        lsl     x8, x8, #56
        orr     x8, x8, x7, lsr #8
        lsl     x7, x7, #56
        orr     x7, x7, x6, lsr #8
        lsl     x6, x6, #56
        orr     x6, x6, x5, lsr #8
        lsl     x5, x5, #56
        orr     x5, x5, x4, lsr #8
        lsl     x4, x4, #56

        adds     x3, x4,  x3          // a[3]
        adcs    x12, x5, x12          // a[4]
        adcs    x13, x6, x13
        adcs    x14, x7, x14
        adcs    x15, x8, x15
        ldp     x16, x17, [x0,#64]
        ldp     x28, x30, [x0,#80]
        mul     x4,  x2, x24          // a[2] x .Lp503p1_nz_s8[0]
        umulh   x7,  x2, x24
        adcs    x16, x9, x16
        adcs    x17, x10, x17
        adcs    x28, xzr, x28
        adcs    x30, xzr, x30
        ldp     x20, x21, [x0,#96]
        ldp     x22, x23, [x0,#112]
        mul     x5,  x2, x25          // a[2] x .Lp503p1_nz_s8[1]
        umulh   x6,  x2, x25
        adcs    x20, xzr, x20
        adcs    x21, xzr, x21
        adcs    x22, xzr, x22
        adc     x23, xzr, x23

        // a[2-3] x .Lp503p1_nz_s8 --> result: x4:x9
        $mul23

        orr     x10, xzr, x9, lsr #8
        lsl     x9, x9, #56
        orr     x9, x9, x8, lsr #8
        lsl     x8, x8, #56
        orr     x8, x8, x7, lsr #8
        lsl     x7, x7, #56
        orr     x7, x7, x6, lsr #8
        lsl     x6, x6, #56
        orr     x6, x6, x5, lsr #8
        lsl     x5, x5, #56
        orr     x5, x5, x4, lsr #8
        lsl     x4, x4, #56

        adds    x13, x4, x13          // a[5]
        adcs    x14, x5, x14          // a[6]
        adcs    x15, x6, x15
        adcs    x16, x7, x16
        mul     x4, x12, x24          // a[4] x .Lp503p1_nz_s8[0]
        umulh   x7, x12, x24
        adcs    x17, x8, x17
        adcs    x28, x9, x28
        adcs    x30, x10, x30
        adcs    x20, xzr, x20
        mul     x5, x12, x25          // a[4] x .Lp503p1_nz_s8[1]
        umulh   x6, x12, x25
        adcs    x21, xzr, x21
        adcs    x22, xzr, x22
        adc     x23, xzr, x23

        // a[4-5] x .Lp503p1_nz_s8 --> result: x4:x9
        $mul45

        orr     x10, xzr, x9, lsr #8
        lsl     x9, x9, #56
        orr     x9, x9, x8, lsr #8
        lsl     x8, x8, #56
        orr     x8, x8, x7, lsr #8
        lsl     x7, x7, #56
        orr     x7, x7, x6, lsr #8
        lsl     x6, x6, #56
        orr     x6, x6, x5, lsr #8
        lsl     x5, x5, #56
        orr     x5, x5, x4, lsr #8
        lsl     x4, x4, #56

        adds    x15, x4, x15          // a[7]
        adcs    x16, x5, x16          // a[8]
        adcs    x17, x6, x17
        adcs    x28, x7, x28
        mul     x4, x14, x24          // a[6] x .Lp503p1_nz_s8[0]
        umulh   x7, x14, x24
        adcs    x30, x8, x30
        adcs    x20, x9, x20
        adcs    x21, x10, x21
        mul     x5, x14, x25          // a[6] x .Lp503p1_nz_s8[1]
        umulh   x6, x14, x25
        adcs    x22, xzr, x22
        adc     x23, xzr, x23

        // a[6-7] x .Lp503p1_nz_s8 --> result: x4:x9
        $mul67

        orr     x10, xzr, x9, lsr #8
        lsl     x9, x9, #56
        orr     x9, x9, x8, lsr #8
        lsl     x8, x8, #56
        orr     x8, x8, x7, lsr #8
        lsl     x7, x7, #56
        orr     x7, x7, x6, lsr #8
        lsl     x6, x6, #56
        orr     x6, x6, x5, lsr #8
        lsl     x5, x5, #56
        orr     x5, x5, x4, lsr #8
        lsl     x4, x4, #56

        adds    x17, x4, x17
        adcs    x28, x5, x28
        ldr     x1, [sp,#96]
        adcs    x30, x6, x30
        adcs    x20, x7, x20
        stp     x16, x17, [x1,#0]     // Final result
        stp     x28, x30, [x1,#16]
        adcs    x21, x8, x21
        adcs    x22, x9, x22
        adc     x23, x10, x23
        stp     x20, x21, [x1,#32]
        stp     x22, x23, [x1,#48]

        ldp     x19, x20, [x29,#16]
        ldp     x21, x22, [x29,#32]
        ldp     x23, x24, [x29,#48]
        ldp     x25, x26, [x29,#64]
        ldp     x27, x28, [x29,#80]
        ldp     x29, x30, [sp],#112
        ret

___
}

$code.=&rdc();


#  Field addition
#  Operation: c [x2] = a [x0] + b [x1]
$code.=<<___;
    .global ${PREFIX}_fpadd
    .align 4
    ${PREFIX}_fpadd:
        stp     x29,x30, [sp,#-16]!
        add     x29, sp, #0

        ldp     x3, x4,   [x0,#0]
        ldp     x5, x6,   [x0,#16]
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]

        // Add a + b
        adds    x3, x3, x11
        adcs    x4, x4, x12
        adcs    x5, x5, x13
        adcs    x6, x6, x14
        ldp     x7, x8,   [x0,#32]
        ldp     x9, x10,  [x0,#48]
        ldp     x11, x12, [x1,#32]
        ldp     x13, x14, [x1,#48]
        adcs    x7, x7, x11
        adcs    x8, x8, x12
        adcs    x9, x9, x13
        adc     x10, x10, x14

        //  Subtract 2xp503
        adrp    x17, :pg_hi21:.Lp503x2
        add     x17, x17, :lo12:.Lp503x2
        ldp     x11, x12, [x17, #0]
        ldp     x13, x14, [x17, #16]
        subs    x3, x3, x11
        sbcs    x4, x4, x12
        sbcs    x5, x5, x12
        sbcs    x6, x6, x13
        sbcs    x7, x7, x14

        ldp     x15, x16, [x17, #32]
        ldr     x17,      [x17, #48]
        sbcs    x8, x8, x15
        sbcs    x9, x9, x16
        sbcs    x10, x10, x17
        sbc     x0, xzr, xzr    // x0 can be reused now

        // Add 2xp503 anded with the mask in x0
        and     x11, x11, x0
        and     x12, x12, x0
        and     x13, x13, x0
        and     x14, x14, x0
        and     x15, x15, x0
        and     x16, x16, x0
        and     x17, x17, x0

        adds    x3, x3, x11
        adcs    x4, x4, x12
        adcs    x5, x5, x12
        adcs    x6, x6, x13
        adcs    x7, x7, x14
        adcs    x8, x8, x15
        adcs    x9, x9, x16
        adc     x10, x10, x17

        stp     x3, x4,  [x2,#0]
        stp     x5, x6,  [x2,#16]
        stp     x7, x8,  [x2,#32]
        stp     x9, x10, [x2,#48]

        ldp     x29, x30, [sp],#16
        ret

___

#  Field subtraction
#  Operation: c [x2] = a [x0] - b [x1]
$code.=<<___;
    .global ${PREFIX}_fpsub
    .align 4
    ${PREFIX}_fpsub:
        stp     x29, x30, [sp,#-16]!
        add     x29, sp, #0

        ldp     x3, x4,   [x0,#0]
        ldp     x5, x6,   [x0,#16]
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]

        // Subtract a - b
        subs    x3, x3, x11
        sbcs    x4, x4, x12
        sbcs    x5, x5, x13
        sbcs    x6, x6, x14
        ldp     x7, x8,   [x0,#32]
        ldp     x11, x12, [x1,#32]
        sbcs    x7, x7, x11
        sbcs    x8, x8, x12
        ldp     x9, x10,  [x0,#48]
        ldp     x11, x12, [x1,#48]
        sbcs    x9, x9, x11
        sbcs    x10, x10, x12
        sbc     x17, xzr, xzr

        // Add 2xp503 anded with the mask in x17
        adrp    x16, :pg_hi21:.Lp503x2
        add     x16, x16, :lo12:.Lp503x2

        // First half
        ldp     x11, x12, [x16, #0]
        ldp     x13, x14, [x16, #16]
        and     x11, x11, x17
        and     x12, x12, x17
        and     x13, x13, x17
        adds    x3, x3, x11
        adcs    x4, x4, x12
        adcs    x5, x5, x12
        adcs    x6, x6, x13
        stp     x3, x4,  [x2,#0]
        stp     x5, x6,  [x2,#16]

        // Second half
        ldp     x11, x12, [x16, #32]
        ldr     x13,      [x16, #48]
        and     x14, x14, x17
        and     x11, x11, x17
        and     x12, x12, x17
        and     x13, x13, x17
        adcs    x7, x7, x14
        adcs    x8, x8, x11
        adcs    x9, x9, x12
        adc     x10, x10, x13
        stp     x7, x8,  [x2,#32]
        stp     x9, x10, [x2,#48]

        ldp     x29, x30, [sp],#16
        ret
___

# 503-bit multiprecision addition
# Operation: c [x2] = a [x0] + b [x1]
$code.=<<___;
    .global ${PREFIX}_mpadd_asm
    .align 4
    ${PREFIX}_mpadd_asm:
        stp     x29, x30, [sp,#-16]!
        add     x29, sp, #0

        ldp     x3, x4,   [x0,#0]
        ldp     x5, x6,   [x0,#16]
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]

        adds    x3, x3, x11
        adcs    x4, x4, x12
        adcs    x5, x5, x13
        adcs    x6, x6, x14
        ldp     x7, x8,   [x0,#32]
        ldp     x9, x10,  [x0,#48]
        ldp     x11, x12, [x1,#32]
        ldp     x13, x14, [x1,#48]
        adcs    x7, x7, x11
        adcs    x8, x8, x12
        adcs    x9, x9, x13
        adc     x10, x10, x14

        stp     x3, x4,   [x2,#0]
        stp     x5, x6,   [x2,#16]
        stp     x7, x8,   [x2,#32]
        stp     x9, x10,  [x2,#48]

        ldp     x29, x30, [sp],#16
        ret
___


# 2x503-bit multiprecision addition
# Operation: c [x2] = a [x0] + b [x1]
$code.=<<___;
    .global ${PREFIX}_mpadd503x2_asm
    .align 4
    ${PREFIX}_mpadd503x2_asm:
        stp     x29, x30, [sp,#-16]!
        add     x29, sp, #0

        ldp     x3, x4,   [x0,#0]
        ldp     x5, x6,   [x0,#16]
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]
        adds    x3, x3, x11
        adcs    x4, x4, x12
        adcs    x5, x5, x13
        adcs    x6, x6, x14
        ldp     x7, x8,   [x0,#32]
        ldp     x9, x10,  [x0,#48]
        ldp     x11, x12, [x1,#32]
        ldp     x13, x14, [x1,#48]
        adcs    x7, x7, x11
        adcs    x8, x8, x12
        adcs    x9, x9, x13
        adcs    x10, x10, x14

        stp     x3, x4,   [x2,#0]
        stp     x5, x6,   [x2,#16]
        stp     x7, x8,   [x2,#32]
        stp     x9, x10,  [x2,#48]

        ldp     x3, x4,   [x0,#64]
        ldp     x5, x6,   [x0,#80]
        ldp     x11, x12, [x1,#64]
        ldp     x13, x14, [x1,#80]
        adcs    x3, x3, x11
        adcs    x4, x4, x12
        adcs    x5, x5, x13
        adcs    x6, x6, x14
        ldp     x7, x8,   [x0,#96]
        ldp     x9, x10,  [x0,#112]
        ldp     x11, x12, [x1,#96]
        ldp     x13, x14, [x1,#112]
        adcs    x7, x7, x11
        adcs    x8, x8, x12
        adcs    x9, x9, x13
        adc     x10, x10, x14

        stp     x3, x4,   [x2,#64]
        stp     x5, x6,   [x2,#80]
        stp     x7, x8,   [x2,#96]
        stp     x9, x10,  [x2,#112]

        ldp     x29, x30, [sp],#16
        ret
___



# 2x503-bit multiprecision subtraction
# Operation: c [x2] = a [x0] - b [x1].
# Returns borrow mask
$code.=<<___;
    .global ${PREFIX}_mpsubx2_asm
    .align 4
    ${PREFIX}_mpsubx2_asm:
        stp     x29, x30, [sp,#-16]!
        add     x29, sp, #0

        ldp     x3, x4,   [x0,#0]
        ldp     x5, x6,   [x0,#16]
        ldp     x11, x12, [x1,#0]
        ldp     x13, x14, [x1,#16]
        subs    x3, x3, x11
        sbcs    x4, x4, x12
        sbcs    x5, x5, x13
        sbcs    x6, x6, x14
        ldp     x7, x8,   [x0,#32]
        ldp     x9, x10,  [x0,#48]
        ldp     x11, x12, [x1,#32]
        ldp     x13, x14, [x1,#48]
        sbcs    x7, x7, x11
        sbcs    x8, x8, x12
        sbcs    x9, x9, x13
        sbcs    x10, x10, x14

        stp     x3, x4,   [x2,#0]
        stp     x5, x6,   [x2,#16]
        stp     x7, x8,   [x2,#32]
        stp     x9, x10,  [x2,#48]

        ldp     x3, x4,   [x0,#64]
        ldp     x5, x6,   [x0,#80]
        ldp     x11, x12, [x1,#64]
        ldp     x13, x14, [x1,#80]
        sbcs    x3, x3, x11
        sbcs    x4, x4, x12
        sbcs    x5, x5, x13
        sbcs    x6, x6, x14
        ldp     x7, x8,   [x0,#96]
        ldp     x9, x10,  [x0,#112]
        ldp     x11, x12, [x1,#96]
        ldp     x13, x14, [x1,#112]
        sbcs    x7, x7, x11
        sbcs    x8, x8, x12
        sbcs    x9, x9, x13
        sbcs    x10, x10, x14
        sbc     x0, xzr, xzr

        stp     x3, x4,   [x2,#64]
        stp     x5, x6,   [x2,#80]
        stp     x7, x8,   [x2,#96]
        stp     x9, x10,  [x2,#112]

        ldp     x29, x30, [sp],#16
        ret
___


# Double 2x503-bit multiprecision subtraction
# Operation: c [x2] = c [x2] - a [x0] - b [x1]
$code.=<<___;
    .global ${PREFIX}_mpdblsubx2_asm
    .align 4
    ${PREFIX}_mpdblsubx2_asm:
        stp     x29, x30, [sp, #-64]!
        add     x29, sp, #0

        stp     x20, x21, [sp, #16]
        stp     x22, x23, [sp, #32]
        str     x24,      [sp, #48]

        ldp     x3, x4,   [x2,#0]
        ldp     x5, x6,   [x2,#16]
        ldp     x7, x8,   [x2,#32]
        ldp     x9, x10,  [x2,#48]
        ldp     x11, x12, [x2,#64]
        ldp     x13, x14, [x2,#80]
        ldp     x15, x16, [x2,#96]
        ldp     x17, x24, [x2,#112]

        ldp     x20, x21, [x0,#0]
        ldp     x22, x23, [x0,#16]
        subs    x3, x3, x20
        sbcs    x4, x4, x21
        sbcs    x5, x5, x22
        sbcs    x6, x6, x23
        ldp     x20, x21, [x0,#32]
        ldp     x22, x23, [x0,#48]
        sbcs    x7, x7, x20
        sbcs    x8, x8, x21
        sbcs    x9, x9, x22
        sbcs    x10, x10, x23
        ldp     x20, x21, [x0,#64]
        ldp     x22, x23, [x0,#80]
        sbcs    x11, x11, x20
        sbcs    x12, x12, x21
        sbcs    x13, x13, x22
        sbcs    x14, x14, x23
        ldp     x20, x21, [x0,#96]
        ldp     x22, x23, [x0,#112]
        sbcs    x15, x15, x20
        sbcs    x16, x16, x21
        sbcs    x17, x17, x22
        sbc     x24, x24, x23

        ldp     x20, x21, [x1,#0]
        ldp     x22, x23, [x1,#16]
        subs    x3, x3, x20
        sbcs    x4, x4, x21
        sbcs    x5, x5, x22
        sbcs    x6, x6, x23
        ldp     x20, x21, [x1,#32]
        ldp     x22, x23, [x1,#48]
        sbcs    x7, x7, x20
        sbcs    x8, x8, x21
        sbcs    x9, x9, x22
        sbcs    x10, x10, x23
        ldp     x20, x21, [x1,#64]
        ldp     x22, x23, [x1,#80]
        sbcs    x11, x11, x20
        sbcs    x12, x12, x21
        sbcs    x13, x13, x22
        sbcs    x14, x14, x23
        ldp     x20, x21, [x1,#96]
        ldp     x22, x23, [x1,#112]
        sbcs    x15, x15, x20
        sbcs    x16, x16, x21
        sbcs    x17, x17, x22
        sbc     x24, x24, x23

        stp     x3, x4,   [x2,#0]
        stp     x5, x6,   [x2,#16]
        stp     x7, x8,   [x2,#32]
        stp     x9, x10,  [x2,#48]
        stp     x11, x12, [x2,#64]
        stp     x13, x14, [x2,#80]
        stp     x15, x16, [x2,#96]
        stp     x17, x24, [x2,#112]

        ldp     x20, x21, [x29,#16]
        ldp     x22, x23, [x29,#32]
        ldr     x24,      [x29,#48]

        ldp     x29, x30, [sp],#64
        ret
___

foreach (split("\n",$code)) {
  s/\`([^\`]*)\`/eval($1)/ge;
  print $_,"\n";
}

close STDOUT;
