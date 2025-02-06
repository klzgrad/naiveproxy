#!/usr/bin/env perl
# Copyright 2024 The BoringSSL Authors
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#------------------------------------------------------------------------------
#
# VAES and VPCLMULQDQ optimized AES-GCM for x86_64
#
# This file is based on aes-gcm-avx10-x86_64.S from the Linux kernel
# (https://git.kernel.org/linus/b06affb1cb580e13).  The following notable
# changes have been made:
#
# - Relicensed under BoringSSL's preferred license.
#
# - Converted from GNU assembler to "perlasm".  This was necessary for
#   compatibility with BoringSSL's Windows builds which use NASM instead of the
#   GNU assembler.  It was also necessary for compatibility with the 'delocate'
#   tool used in BoringSSL's FIPS builds.
#
# - Added support for the Windows ABI.
#
# - Changed function prototypes to be compatible with what BoringSSL wants.
#
# - Removed the optimized finalization function, as BoringSSL doesn't want it.
#
# - Added a single-block GHASH multiplication function, as BoringSSL needs this.
#
# - Added optimization for large amounts of AAD.
#
#------------------------------------------------------------------------------
#
# This file implements AES-GCM (Galois/Counter Mode) for x86_64 CPUs that
# support VAES (vector AES), VPCLMULQDQ (vector carryless multiplication), and
# either AVX512 or AVX10.  Some of the functions, notably the encryption and
# decryption update functions which are the most performance-critical, are
# provided in two variants generated from a macro: one using 256-bit vectors
# (suffix: vaes_avx10_256) and one using 512-bit vectors (vaes_avx10_512).  The
# other, "shared" functions (vaes_avx10) use at most 256-bit vectors.
#
# The functions that use 512-bit vectors are intended for CPUs that support
# 512-bit vectors *and* where using them doesn't cause significant
# downclocking.  They require the following CPU features:
#
#       VAES && VPCLMULQDQ && BMI2 && ((AVX512BW && AVX512VL) || AVX10/512)
#
# The other functions require the following CPU features:
#
#       VAES && VPCLMULQDQ && BMI2 && ((AVX512BW && AVX512VL) || AVX10/256)
#
# Note that we use "avx10" in the names of the functions as a shorthand to
# really mean "AVX10 or a certain set of AVX512 features".  Due to Intel's
# introduction of AVX512 and then its replacement by AVX10, there doesn't seem
# to be a simple way to name things that makes sense on all CPUs.
#
# Note that the macros that support both 256-bit and 512-bit vectors could
# fairly easily be changed to support 128-bit too.  However, this would *not*
# be sufficient to allow the code to run on CPUs without AVX512 or AVX10,
# because the code heavily uses several features of these extensions other than
# the vector length: the increase in the number of SIMD registers from 16 to
# 32, masking support, and new instructions such as vpternlogd (which can do a
# three-argument XOR).  These features are very useful for AES-GCM.

$flavour = shift;
$output  = shift;
if ( $flavour =~ /\./ ) { $output = $flavour; undef $flavour; }

if ( $flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/ ) {
    $win64   = 1;
    @argregs = ( "%rcx", "%rdx", "%r8", "%r9" );
}
else {
    $win64   = 0;
    @argregs = ( "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" );
}

$0 =~ m/(.*[\/\\])[^\/\\]+$/;
$dir = $1;
( $xlate = "${dir}x86_64-xlate.pl" and -f $xlate )
  or ( $xlate = "${dir}../../../perlasm/x86_64-xlate.pl" and -f $xlate )
  or die "can't locate x86_64-xlate.pl";

open OUT, "| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT = *OUT;

sub _begin_func {
    my ( $funcname, $uses_seh ) = @_;
    $g_cur_func_name          = $funcname;
    $g_cur_func_uses_seh      = $uses_seh;
    @g_cur_func_saved_gpregs  = ();
    @g_cur_func_saved_xmmregs = ();
    return <<___;
.globl $funcname
.type $funcname,\@abi-omnipotent
.align 32
$funcname:
    .cfi_startproc
    @{[ $uses_seh ? ".seh_startproc" : "" ]}
    _CET_ENDBR
___
}

# Push a list of general purpose registers onto the stack.
sub _save_gpregs {
    my @gpregs = @_;
    my $code   = "";
    die "_save_gpregs requires uses_seh" unless $g_cur_func_uses_seh;
    die "_save_gpregs can only be called once per function"
      if @g_cur_func_saved_gpregs;
    die "Order must be _save_gpregs, then _save_xmmregs"
      if @g_cur_func_saved_xmmregs;
    @g_cur_func_saved_gpregs = @gpregs;
    for my $reg (@gpregs) {
        $code .= "push $reg\n";
        if ($win64) {
            $code .= ".seh_pushreg $reg\n";
        }
        else {
            $code .= ".cfi_push $reg\n";
        }
    }
    return $code;
}

# Push a list of xmm registers onto the stack if the target is Windows.
sub _save_xmmregs {
    my @xmmregs     = @_;
    my $num_xmmregs = scalar @xmmregs;
    my $code        = "";
    die "_save_xmmregs requires uses_seh" unless $g_cur_func_uses_seh;
    die "_save_xmmregs can only be called once per function"
      if @g_cur_func_saved_xmmregs;
    if ( $win64 and $num_xmmregs > 0 ) {
        @g_cur_func_saved_xmmregs = @xmmregs;
        my $is_misaligned = ( scalar @g_cur_func_saved_gpregs ) % 2 == 0;
        my $alloc_size    = 16 * $num_xmmregs + ( $is_misaligned ? 8 : 0 );
        $code .= "sub \$$alloc_size, %rsp\n";
        $code .= ".seh_stackalloc $alloc_size\n";
        for my $i ( 0 .. $num_xmmregs - 1 ) {
            my $reg_num = $xmmregs[$i];
            my $pos     = 16 * $i;
            $code .= "movdqa %xmm$reg_num, $pos(%rsp)\n";
            $code .= ".seh_savexmm %xmm$reg_num, $pos\n";
        }
    }
    return $code;
}

sub _end_func {
    my $code = "";

    # Restore any xmm registers that were saved earlier.
    my $num_xmmregs = scalar @g_cur_func_saved_xmmregs;
    if ( $win64 and $num_xmmregs > 0 ) {
        my $need_alignment = ( scalar @g_cur_func_saved_gpregs ) % 2 == 0;
        my $alloc_size     = 16 * $num_xmmregs + ( $need_alignment ? 8 : 0 );
        for my $i ( 0 .. $num_xmmregs - 1 ) {
            my $reg_num = $g_cur_func_saved_xmmregs[$i];
            my $pos     = 16 * $i;
            $code .= "movdqa $pos(%rsp), %xmm$reg_num\n";
        }
        $code .= "add \$$alloc_size, %rsp\n";
    }

    # Restore any general purpose registers that were saved earlier.
    for my $reg ( reverse @g_cur_func_saved_gpregs ) {
        $code .= "pop $reg\n";
        if ( !$win64 ) {
            $code .= ".cfi_pop $reg\n";
        }
    }

    $code .= <<___;
    ret
    @{[ $g_cur_func_uses_seh ? ".seh_endproc" : "" ]}
    .cfi_endproc
    .size   $g_cur_func_name, . - $g_cur_func_name
___
    return $code;
}

$code = <<___;
.section .rodata
.align 64

    # A shuffle mask that reflects the bytes of 16-byte blocks
.Lbswap_mask:
    .quad   0x08090a0b0c0d0e0f, 0x0001020304050607

    # This is the GHASH reducing polynomial without its constant term, i.e.
    # x^128 + x^7 + x^2 + x, represented using the backwards mapping
    # between bits and polynomial coefficients.
    #
    # Alternatively, it can be interpreted as the naturally-ordered
    # representation of the polynomial x^127 + x^126 + x^121 + 1, i.e. the
    # "reversed" GHASH reducing polynomial without its x^128 term.
.Lgfpoly:
    .quad   1, 0xc200000000000000

    # Same as above, but with the (1 << 64) bit set.
.Lgfpoly_and_internal_carrybit:
    .quad   1, 0xc200000000000001

    # The below constants are used for incrementing the counter blocks.
    # ctr_pattern points to the four 128-bit values [0, 1, 2, 3].
    # inc_2blocks and inc_4blocks point to the single 128-bit values 2 and
    # 4.  Note that the same '2' is reused in ctr_pattern and inc_2blocks.
.Lctr_pattern:
    .quad   0, 0
    .quad   1, 0
.Linc_2blocks:
    .quad   2, 0
    .quad   3, 0
.Linc_4blocks:
    .quad   4, 0

.text
___

# Number of powers of the hash key stored in the key struct.  The powers are
# stored from highest (H^NUM_H_POWERS) to lowest (H^1).
$NUM_H_POWERS = 16;

$OFFSETOFEND_H_POWERS = $NUM_H_POWERS * 16;

# Offset to 'rounds' in AES_KEY struct
$OFFSETOF_AES_ROUNDS = 240;

# The current vector length in bytes
undef $VL;

# Set the vector length in bytes.  This sets the VL variable and defines
# register aliases V0-V31 that map to the ymm or zmm registers.
sub _set_veclen {
    ($VL) = @_;
    foreach my $i ( 0 .. 31 ) {
        if ( $VL == 32 ) {
            ${"V${i}"} = "%ymm${i}";
        }
        elsif ( $VL == 64 ) {
            ${"V${i}"} = "%zmm${i}";
        }
        else {
            die "Unsupported vector length";
        }
    }
}

# The _ghash_mul_step macro does one step of GHASH multiplication of the
# 128-bit lanes of \a by the corresponding 128-bit lanes of \b and storing the
# reduced products in \dst.  \t0, \t1, and \t2 are temporary registers of the
# same size as \a and \b.  To complete all steps, this must invoked with \i=0
# through \i=9.  The division into steps allows users of this macro to
# optionally interleave the computation with other instructions.  Users of this
# macro must preserve the parameter registers across steps.
#
# The multiplications are done in GHASH's representation of the finite field
# GF(2^128).  Elements of GF(2^128) are represented as binary polynomials
# (i.e. polynomials whose coefficients are bits) modulo a reducing polynomial
# G.  The GCM specification uses G = x^128 + x^7 + x^2 + x + 1.  Addition is
# just XOR, while multiplication is more complex and has two parts: (a) do
# carryless multiplication of two 128-bit input polynomials to get a 256-bit
# intermediate product polynomial, and (b) reduce the intermediate product to
# 128 bits by adding multiples of G that cancel out terms in it.  (Adding
# multiples of G doesn't change which field element the polynomial represents.)
#
# Unfortunately, the GCM specification maps bits to/from polynomial
# coefficients backwards from the natural order.  In each byte it specifies the
# highest bit to be the lowest order polynomial coefficient, *not* the highest!
# This makes it nontrivial to work with the GHASH polynomials.  We could
# reflect the bits, but x86 doesn't have an instruction that does that.
#
# Instead, we operate on the values without bit-reflecting them.  This *mostly*
# just works, since XOR and carryless multiplication are symmetric with respect
# to bit order, but it has some consequences.  First, due to GHASH's byte
# order, by skipping bit reflection, *byte* reflection becomes necessary to
# give the polynomial terms a consistent order.  E.g., considering an N-bit
# value interpreted using the G = x^128 + x^7 + x^2 + x + 1 convention, bits 0
# through N-1 of the byte-reflected value represent the coefficients of x^(N-1)
# through x^0, whereas bits 0 through N-1 of the non-byte-reflected value
# represent x^7...x^0, x^15...x^8, ..., x^(N-1)...x^(N-8) which can't be worked
# with.  Fortunately, x86's vpshufb instruction can do byte reflection.
#
# Second, forgoing the bit reflection causes an extra multiple of x (still
# using the G = x^128 + x^7 + x^2 + x + 1 convention) to be introduced by each
# multiplication.  This is because an M-bit by N-bit carryless multiplication
# really produces a (M+N-1)-bit product, but in practice it's zero-extended to
# M+N bits.  In the G = x^128 + x^7 + x^2 + x + 1 convention, which maps bits
# to polynomial coefficients backwards, this zero-extension actually changes
# the product by introducing an extra factor of x.  Therefore, users of this
# macro must ensure that one of the inputs has an extra factor of x^-1, i.e.
# the multiplicative inverse of x, to cancel out the extra x.
#
# Third, the backwards coefficients convention is just confusing to work with,
# since it makes "low" and "high" in the polynomial math mean the opposite of
# their normal meaning in computer programming.  This can be solved by using an
# alternative interpretation: the polynomial coefficients are understood to be
# in the natural order, and the multiplication is actually \a * \b * x^-128 mod
# x^128 + x^127 + x^126 + x^121 + 1.  This doesn't change the inputs, outputs,
# or the implementation at all; it just changes the mathematical interpretation
# of what each instruction is doing.  Starting from here, we'll use this
# alternative interpretation, as it's easier to understand the code that way.
#
# Moving onto the implementation, the vpclmulqdq instruction does 64 x 64 =>
# 128-bit carryless multiplication, so we break the 128 x 128 multiplication
# into parts as follows (the _L and _H suffixes denote low and high 64 bits):
#
#     LO = a_L * b_L
#     MI = (a_L * b_H) + (a_H * b_L)
#     HI = a_H * b_H
#
# The 256-bit product is x^128*HI + x^64*MI + LO.  LO, MI, and HI are 128-bit.
# Note that MI "overlaps" with LO and HI.  We don't consolidate MI into LO and
# HI right away, since the way the reduction works makes that unnecessary.
#
# For the reduction, we cancel out the low 128 bits by adding multiples of G =
# x^128 + x^127 + x^126 + x^121 + 1.  This is done by two iterations, each of
# which cancels out the next lowest 64 bits.  Consider a value x^64*A + B,
# where A and B are 128-bit.  Adding B_L*G to that value gives:
#
#       x^64*A + B + B_L*G
#     = x^64*A + x^64*B_H + B_L + B_L*(x^128 + x^127 + x^126 + x^121 + 1)
#     = x^64*A + x^64*B_H + B_L + x^128*B_L + x^64*B_L*(x^63 + x^62 + x^57) + B_L
#     = x^64*A + x^64*B_H + x^128*B_L + x^64*B_L*(x^63 + x^62 + x^57) + B_L + B_L
#     = x^64*(A + B_H + x^64*B_L + B_L*(x^63 + x^62 + x^57))
#
# So: if we sum A, B with its halves swapped, and the low half of B times x^63
# + x^62 + x^57, we get a 128-bit value C where x^64*C is congruent to the
# original value x^64*A + B.  I.e., the low 64 bits got canceled out.
#
# We just need to apply this twice: first to fold LO into MI, and second to
# fold the updated MI into HI.
#
# The needed three-argument XORs are done using the vpternlogd instruction with
# immediate 0x96, since this is faster than two vpxord instructions.
#
# A potential optimization, assuming that b is fixed per-key (if a is fixed
# per-key it would work the other way around), is to use one iteration of the
# reduction described above to precompute a value c such that x^64*c = b mod G,
# and then multiply a_L by c (and implicitly by x^64) instead of by b:
#
#     MI = (a_L * c_L) + (a_H * b_L)
#     HI = (a_L * c_H) + (a_H * b_H)
#
# This would eliminate the LO part of the intermediate product, which would
# eliminate the need to fold LO into MI.  This would save two instructions,
# including a vpclmulqdq.  However, we currently don't use this optimization
# because it would require twice as many per-key precomputed values.
#
# Using Karatsuba multiplication instead of "schoolbook" multiplication
# similarly would save a vpclmulqdq but does not seem to be worth it.
sub _ghash_mul_step {
    my ( $i, $a, $b, $dst, $gfpoly, $t0, $t1, $t2 ) = @_;
    if ( $i == 0 ) {
        return "vpclmulqdq \$0x00, $a, $b, $t0\n" .    # LO = a_L * b_L
          "vpclmulqdq \$0x01, $a, $b, $t1\n";          # MI_0 = a_L * b_H
    }
    elsif ( $i == 1 ) {
        return "vpclmulqdq \$0x10, $a, $b, $t2\n";     # MI_1 = a_H * b_L
    }
    elsif ( $i == 2 ) {
        return "vpxord $t2, $t1, $t1\n";               # MI = MI_0 + MI_1
    }
    elsif ( $i == 3 ) {
        return
          "vpclmulqdq \$0x01, $t0, $gfpoly, $t2\n";  # LO_L*(x^63 + x^62 + x^57)
    }
    elsif ( $i == 4 ) {
        return "vpshufd \$0x4e, $t0, $t0\n";         # Swap halves of LO
    }
    elsif ( $i == 5 ) {
        return "vpternlogd \$0x96, $t2, $t0, $t1\n";    # Fold LO into MI
    }
    elsif ( $i == 6 ) {
        return "vpclmulqdq \$0x11, $a, $b, $dst\n";     # HI = a_H * b_H
    }
    elsif ( $i == 7 ) {
        return
          "vpclmulqdq \$0x01, $t1, $gfpoly, $t0\n";  # MI_L*(x^63 + x^62 + x^57)
    }
    elsif ( $i == 8 ) {
        return "vpshufd \$0x4e, $t1, $t1\n";         # Swap halves of MI
    }
    elsif ( $i == 9 ) {
        return "vpternlogd \$0x96, $t0, $t1, $dst\n";    # Fold MI into HI
    }
}

# GHASH-multiply the 128-bit lanes of \a by the 128-bit lanes of \b and store
# the reduced products in \dst.  See _ghash_mul_step for full explanation.
sub _ghash_mul {
    my ( $a, $b, $dst, $gfpoly, $t0, $t1, $t2 ) = @_;
    my $code = "";
    for my $i ( 0 .. 9 ) {
        $code .= _ghash_mul_step $i, $a, $b, $dst, $gfpoly, $t0, $t1, $t2;
    }
    return $code;
}

# GHASH-multiply the 128-bit lanes of \a by the 128-bit lanes of \b and add the
# *unreduced* products to \lo, \mi, and \hi.
sub _ghash_mul_noreduce {
    my ( $a, $b, $lo, $mi, $hi, $t0, $t1, $t2, $t3 ) = @_;
    return <<___;
    vpclmulqdq      \$0x00, $a, $b, $t0      # a_L * b_L
    vpclmulqdq      \$0x01, $a, $b, $t1      # a_L * b_H
    vpclmulqdq      \$0x10, $a, $b, $t2      # a_H * b_L
    vpclmulqdq      \$0x11, $a, $b, $t3      # a_H * b_H
    vpxord          $t0, $lo, $lo
    vpternlogd      \$0x96, $t2, $t1, $mi
    vpxord          $t3, $hi, $hi
___
}

# Reduce the unreduced products from \lo, \mi, and \hi and store the 128-bit
# reduced products in \hi.  See _ghash_mul_step for explanation of reduction.
sub _ghash_reduce {
    my ( $lo, $mi, $hi, $gfpoly, $t0 ) = @_;
    return <<___;
    vpclmulqdq      \$0x01, $lo, $gfpoly, $t0
    vpshufd         \$0x4e, $lo, $lo
    vpternlogd      \$0x96, $t0, $lo, $mi
    vpclmulqdq      \$0x01, $mi, $gfpoly, $t0
    vpshufd         \$0x4e, $mi, $mi
    vpternlogd      \$0x96, $t0, $mi, $hi
___
}

$g_init_macro_expansion_count = 0;

# void gcm_init_##suffix(u128 Htable[16], const uint64_t H[2]);
#
# Initialize |Htable| with powers of the GHASH subkey |H|.
#
# The powers are stored in the order H^NUM_H_POWERS to H^1.
#
# This macro supports both VL=32 and VL=64.  _set_veclen must have been invoked
# with the desired length.  In the VL=32 case, the function computes twice as
# many key powers than are actually used by the VL=32 GCM update functions.
# This is done to keep the key format the same regardless of vector length.
sub _aes_gcm_init {
    my $local_label_suffix = "__func" . ++$g_init_macro_expansion_count;

    # Function arguments
    my ( $HTABLE, $H_PTR ) = @argregs[ 0 .. 1 ];

    # Additional local variables.  V0-V2 and %rax are used as temporaries.
    my $POWERS_PTR     = "%r8";
    my $RNDKEYLAST_PTR = "%r9";
    my ( $H_CUR, $H_CUR_YMM, $H_CUR_XMM )    = ( "$V3", "%ymm3", "%xmm3" );
    my ( $H_INC, $H_INC_YMM, $H_INC_XMM )    = ( "$V4", "%ymm4", "%xmm4" );
    my ( $GFPOLY, $GFPOLY_YMM, $GFPOLY_XMM ) = ( "$V5", "%ymm5", "%xmm5" );

    my $code = <<___;
    # Get pointer to lowest set of key powers (located at end of array).
    lea             $OFFSETOFEND_H_POWERS-$VL($HTABLE), $POWERS_PTR

    # Load the byte-reflected hash subkey.  BoringSSL provides it in
    # byte-reflected form except the two halves are in the wrong order.
    vpshufd         \$0x4e, ($H_PTR), $H_CUR_XMM

    # Finish preprocessing the first key power, H^1.  Since this GHASH
    # implementation operates directly on values with the backwards bit
    # order specified by the GCM standard, it's necessary to preprocess the
    # raw key as follows.  First, reflect its bytes.  Second, multiply it
    # by x^-1 mod x^128 + x^7 + x^2 + x + 1 (if using the backwards
    # interpretation of polynomial coefficients), which can also be
    # interpreted as multiplication by x mod x^128 + x^127 + x^126 + x^121
    # + 1 using the alternative, natural interpretation of polynomial
    # coefficients.  For details, see the comment above _ghash_mul_step.
    #
    # Either way, for the multiplication the concrete operation performed
    # is a left shift of the 128-bit value by 1 bit, then an XOR with (0xc2
    # << 120) | 1 if a 1 bit was carried out.  However, there's no 128-bit
    # wide shift instruction, so instead double each of the two 64-bit
    # halves and incorporate the internal carry bit into the value XOR'd.
    vpshufd         \$0xd3, $H_CUR_XMM, %xmm0
    vpsrad          \$31, %xmm0, %xmm0
    vpaddq          $H_CUR_XMM, $H_CUR_XMM, $H_CUR_XMM
    # H_CUR_XMM ^= xmm0 & gfpoly_and_internal_carrybit
    vpternlogd      \$0x78, .Lgfpoly_and_internal_carrybit(%rip), %xmm0, $H_CUR_XMM

    # Load the gfpoly constant.
    vbroadcasti32x4 .Lgfpoly(%rip), $GFPOLY

    # Square H^1 to get H^2.
    #
    # Note that as with H^1, all higher key powers also need an extra
    # factor of x^-1 (or x using the natural interpretation).  Nothing
    # special needs to be done to make this happen, though: H^1 * H^1 would
    # end up with two factors of x^-1, but the multiplication consumes one.
    # So the product H^2 ends up with the desired one factor of x^-1.
    @{[ _ghash_mul  $H_CUR_XMM, $H_CUR_XMM, $H_INC_XMM, $GFPOLY_XMM,
                    "%xmm0", "%xmm1", "%xmm2" ]}

    # Create H_CUR_YMM = [H^2, H^1] and H_INC_YMM = [H^2, H^2].
    vinserti128     \$1, $H_CUR_XMM, $H_INC_YMM, $H_CUR_YMM
    vinserti128     \$1, $H_INC_XMM, $H_INC_YMM, $H_INC_YMM
___

    if ( $VL == 64 ) {

        # Create H_CUR = [H^4, H^3, H^2, H^1] and H_INC = [H^4, H^4, H^4, H^4].
        $code .= <<___;
        @{[ _ghash_mul  $H_INC_YMM, $H_CUR_YMM, $H_INC_YMM, $GFPOLY_YMM,
                        "%ymm0", "%ymm1", "%ymm2" ]}
        vinserti64x4    \$1, $H_CUR_YMM, $H_INC, $H_CUR
        vshufi64x2      \$0, $H_INC, $H_INC, $H_INC
___
    }

    $code .= <<___;
    # Store the lowest set of key powers.
    vmovdqu8        $H_CUR, ($POWERS_PTR)

    # Compute and store the remaining key powers.  With VL=32, repeatedly
    # multiply [H^(i+1), H^i] by [H^2, H^2] to get [H^(i+3), H^(i+2)].
    # With VL=64, repeatedly multiply [H^(i+3), H^(i+2), H^(i+1), H^i] by
    # [H^4, H^4, H^4, H^4] to get [H^(i+7), H^(i+6), H^(i+5), H^(i+4)].
    mov             \$@{[ $NUM_H_POWERS*16/$VL - 1 ]}, %eax
.Lprecompute_next$local_label_suffix:
    sub             \$$VL, $POWERS_PTR
    @{[ _ghash_mul  $H_INC, $H_CUR, $H_CUR, $GFPOLY, $V0, $V1, $V2 ]}
    vmovdqu8        $H_CUR, ($POWERS_PTR)
    dec             %eax
    jnz             .Lprecompute_next$local_label_suffix

    vzeroupper      # This is needed after using ymm or zmm registers.
___
    return $code;
}

# XOR together the 128-bit lanes of \src (whose low lane is \src_xmm) and store
# the result in \dst_xmm.  This implicitly zeroizes the other lanes of dst.
sub _horizontal_xor {
    my ( $src, $src_xmm, $dst_xmm, $t0_xmm, $t1_xmm, $t2_xmm ) = @_;
    if ( $VL == 32 ) {
        return <<___;
        vextracti32x4   \$1, $src, $t0_xmm
        vpxord          $t0_xmm, $src_xmm, $dst_xmm
___
    }
    elsif ( $VL == 64 ) {
        return <<___;
        vextracti32x4   \$1, $src, $t0_xmm
        vextracti32x4   \$2, $src, $t1_xmm
        vextracti32x4   \$3, $src, $t2_xmm
        vpxord          $t0_xmm, $src_xmm, $dst_xmm
        vpternlogd      \$0x96, $t1_xmm, $t2_xmm, $dst_xmm
___
    }
    else {
        die "Unsupported vector length";
    }
}

# Do one step of the GHASH update of the data blocks given in the vector
# registers GHASHDATA[0-3].  \i specifies the step to do, 0 through 9.  The
# division into steps allows users of this macro to optionally interleave the
# computation with other instructions.  This macro uses the vector register
# GHASH_ACC as input/output; GHASHDATA[0-3] as inputs that are clobbered;
# H_POW[4-1], GFPOLY, and BSWAP_MASK as inputs that aren't clobbered; and
# GHASHTMP[0-2] as temporaries.  This macro handles the byte-reflection of the
# data blocks.  The parameter registers must be preserved across steps.
#
# The GHASH update does: GHASH_ACC = H_POW4*(GHASHDATA0 + GHASH_ACC) +
# H_POW3*GHASHDATA1 + H_POW2*GHASHDATA2 + H_POW1*GHASHDATA3, where the
# operations are vectorized operations on vectors of 16-byte blocks.  E.g.,
# with VL=32 there are 2 blocks per vector and the vectorized terms correspond
# to the following non-vectorized terms:
#
#       H_POW4*(GHASHDATA0 + GHASH_ACC) => H^8*(blk0 + GHASH_ACC_XMM) and H^7*(blk1 + 0)
#       H_POW3*GHASHDATA1 => H^6*blk2 and H^5*blk3
#       H_POW2*GHASHDATA2 => H^4*blk4 and H^3*blk5
#       H_POW1*GHASHDATA3 => H^2*blk6 and H^1*blk7
#
# With VL=64, we use 4 blocks/vector, H^16 through H^1, and blk0 through blk15.
#
# More concretely, this code does:
#   - Do vectorized "schoolbook" multiplications to compute the intermediate
#     256-bit product of each block and its corresponding hash key power.
#     There are 4*VL/16 of these intermediate products.
#   - Sum (XOR) the intermediate 256-bit products across vectors.  This leaves
#     VL/16 256-bit intermediate values.
#   - Do a vectorized reduction of these 256-bit intermediate values to
#     128-bits each.  This leaves VL/16 128-bit intermediate values.
#   - Sum (XOR) these values and store the 128-bit result in GHASH_ACC_XMM.
#
# See _ghash_mul_step for the full explanation of the operations performed for
# each individual finite field multiplication and reduction.
sub _ghash_step_4x {
    my ($i) = @_;
    if ( $i == 0 ) {
        return <<___;
        vpshufb         $BSWAP_MASK, $GHASHDATA0, $GHASHDATA0
        vpxord          $GHASH_ACC, $GHASHDATA0, $GHASHDATA0
        vpshufb         $BSWAP_MASK, $GHASHDATA1, $GHASHDATA1
        vpshufb         $BSWAP_MASK, $GHASHDATA2, $GHASHDATA2
___
    }
    elsif ( $i == 1 ) {
        return <<___;
        vpshufb         $BSWAP_MASK, $GHASHDATA3, $GHASHDATA3
        vpclmulqdq      \$0x00, $H_POW4, $GHASHDATA0, $GHASH_ACC    # LO_0
        vpclmulqdq      \$0x00, $H_POW3, $GHASHDATA1, $GHASHTMP0    # LO_1
        vpclmulqdq      \$0x00, $H_POW2, $GHASHDATA2, $GHASHTMP1    # LO_2
___
    }
    elsif ( $i == 2 ) {
        return <<___;
        vpxord          $GHASHTMP0, $GHASH_ACC, $GHASH_ACC          # sum(LO_{1,0})
        vpclmulqdq      \$0x00, $H_POW1, $GHASHDATA3, $GHASHTMP2    # LO_3
        vpternlogd      \$0x96, $GHASHTMP2, $GHASHTMP1, $GHASH_ACC  # LO = sum(LO_{3,2,1,0})
        vpclmulqdq      \$0x01, $H_POW4, $GHASHDATA0, $GHASHTMP0    # MI_0
___
    }
    elsif ( $i == 3 ) {
        return <<___;
        vpclmulqdq      \$0x01, $H_POW3, $GHASHDATA1, $GHASHTMP1    # MI_1
        vpclmulqdq      \$0x01, $H_POW2, $GHASHDATA2, $GHASHTMP2    # MI_2
        vpternlogd      \$0x96, $GHASHTMP2, $GHASHTMP1, $GHASHTMP0  # sum(MI_{2,1,0})
        vpclmulqdq      \$0x01, $H_POW1, $GHASHDATA3, $GHASHTMP1    # MI_3
___
    }
    elsif ( $i == 4 ) {
        return <<___;
        vpclmulqdq      \$0x10, $H_POW4, $GHASHDATA0, $GHASHTMP2    # MI_4
        vpternlogd      \$0x96, $GHASHTMP2, $GHASHTMP1, $GHASHTMP0  # sum(MI_{4,3,2,1,0})
        vpclmulqdq      \$0x10, $H_POW3, $GHASHDATA1, $GHASHTMP1    # MI_5
        vpclmulqdq      \$0x10, $H_POW2, $GHASHDATA2, $GHASHTMP2    # MI_6
___
    }
    elsif ( $i == 5 ) {
        return <<___;
        vpternlogd      \$0x96, $GHASHTMP2, $GHASHTMP1, $GHASHTMP0  # sum(MI_{6,5,4,3,2,1,0})
        vpclmulqdq      \$0x01, $GHASH_ACC, $GFPOLY, $GHASHTMP2     # LO_L*(x^63 + x^62 + x^57)
        vpclmulqdq      \$0x10, $H_POW1, $GHASHDATA3, $GHASHTMP1    # MI_7
        vpxord          $GHASHTMP1, $GHASHTMP0, $GHASHTMP0          # MI = sum(MI_{7,6,5,4,3,2,1,0})
___
    }
    elsif ( $i == 6 ) {
        return <<___;
        vpshufd         \$0x4e, $GHASH_ACC, $GHASH_ACC              # Swap halves of LO
        vpclmulqdq      \$0x11, $H_POW4, $GHASHDATA0, $GHASHDATA0   # HI_0
        vpclmulqdq      \$0x11, $H_POW3, $GHASHDATA1, $GHASHDATA1   # HI_1
        vpclmulqdq      \$0x11, $H_POW2, $GHASHDATA2, $GHASHDATA2   # HI_2
___
    }
    elsif ( $i == 7 ) {
        return <<___;
        vpternlogd      \$0x96, $GHASHTMP2, $GHASH_ACC, $GHASHTMP0  # Fold LO into MI
        vpclmulqdq      \$0x11, $H_POW1, $GHASHDATA3, $GHASHDATA3   # HI_3
        vpternlogd      \$0x96, $GHASHDATA2, $GHASHDATA1, $GHASHDATA0 # sum(HI_{2,1,0})
        vpclmulqdq      \$0x01, $GHASHTMP0, $GFPOLY, $GHASHTMP1     # MI_L*(x^63 + x^62 + x^57)
___
    }
    elsif ( $i == 8 ) {
        return <<___;
        vpxord          $GHASHDATA3, $GHASHDATA0, $GHASH_ACC        # HI = sum(HI_{3,2,1,0})
        vpshufd         \$0x4e, $GHASHTMP0, $GHASHTMP0              # Swap halves of MI
        vpternlogd      \$0x96, $GHASHTMP1, $GHASHTMP0, $GHASH_ACC  # Fold MI into HI
___
    }
    elsif ( $i == 9 ) {
        return _horizontal_xor $GHASH_ACC, $GHASH_ACC_XMM, $GHASH_ACC_XMM,
          $GHASHDATA0_XMM, $GHASHDATA1_XMM, $GHASHDATA2_XMM;
    }
}

# Update GHASH with the blocks given in GHASHDATA[0-3].
# See _ghash_step_4x for full explanation.
sub _ghash_4x {
    my $code = "";
    for my $i ( 0 .. 9 ) {
        $code .= _ghash_step_4x $i;
    }
    return $code;
}

$g_ghash_macro_expansion_count = 0;

# void gcm_ghash_##suffix(uint8_t Xi[16], const u128 Htable[16],
#                         const uint8_t *in, size_t len);
#
# This macro generates the body of a GHASH update function with the above
# prototype.  This macro supports both VL=32 and VL=64.  _set_veclen must have
# been invoked with the desired length.
#
# The generated function processes the AAD (Additional Authenticated Data) in
# GCM.  Using the key |Htable|, it updates the GHASH accumulator |Xi| with the
# data given by |in| and |len|.  On the first call, |Xi| must be all zeroes.
# |len| must be a multiple of 16.
#
# This function handles large amounts of AAD efficiently, while also keeping the
# overhead low for small amounts of AAD which is the common case.  TLS uses less
# than one block of AAD, but (uncommonly) other use cases may use much more.
sub _ghash_update {
    my $local_label_suffix = "__func" . ++$g_ghash_macro_expansion_count;
    my $code               = "";

    # Function arguments
    my ( $GHASH_ACC_PTR, $H_POWERS, $AAD, $AADLEN ) = @argregs[ 0 .. 3 ];

    # Additional local variables
    ( $GHASHDATA0, $GHASHDATA0_XMM ) = ( $V0, "%xmm0" );
    ( $GHASHDATA1, $GHASHDATA1_XMM ) = ( $V1, "%xmm1" );
    ( $GHASHDATA2, $GHASHDATA2_XMM ) = ( $V2, "%xmm2" );
    ( $GHASHDATA3, $GHASHDATA3_XMM ) = ( $V3, "%xmm3" );
    ( $BSWAP_MASK, $BSWAP_MASK_XMM ) = ( $V4, "%xmm4" );
    ( $GHASH_ACC,  $GHASH_ACC_XMM )  = ( $V5, "%xmm5" );
    ( $H_POW4, $H_POW3, $H_POW2 )          = ( $V6, $V7, $V8 );
    ( $H_POW1, $H_POW1_XMM )               = ( $V9, "%xmm9" );
    ( $GFPOLY, $GFPOLY_XMM )               = ( $V10, "%xmm10" );
    ( $GHASHTMP0, $GHASHTMP1, $GHASHTMP2 ) = ( $V11, $V12, $V13 );

    $code .= <<___;
    @{[ _save_xmmregs (6 .. 13) ]}
    .seh_endprologue

    # Load the bswap_mask and gfpoly constants.  Since AADLEN is usually small,
    # usually only 128-bit vectors will be used.  So as an optimization, don't
    # broadcast these constants to all 128-bit lanes quite yet.
    vmovdqu         .Lbswap_mask(%rip), $BSWAP_MASK_XMM
    vmovdqu         .Lgfpoly(%rip), $GFPOLY_XMM

    # Load the GHASH accumulator.
    vmovdqu         ($GHASH_ACC_PTR), $GHASH_ACC_XMM
    vpshufb         $BSWAP_MASK_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM

    # Optimize for AADLEN < VL by checking for AADLEN < VL before AADLEN < 4*VL.
    cmp             \$$VL, $AADLEN
    jb              .Laad_blockbyblock$local_label_suffix

    # AADLEN >= VL, so we'll operate on full vectors.  Broadcast bswap_mask and
    # gfpoly to all 128-bit lanes.
    vshufi64x2      \$0, $BSWAP_MASK, $BSWAP_MASK, $BSWAP_MASK
    vshufi64x2      \$0, $GFPOLY, $GFPOLY, $GFPOLY

    # Load the lowest set of key powers.
    vmovdqu8        $OFFSETOFEND_H_POWERS-1*$VL($H_POWERS), $H_POW1

    cmp             \$4*$VL-1, $AADLEN
    jbe             .Laad_loop_1x$local_label_suffix

    # AADLEN >= 4*VL.  Load the higher key powers.
    vmovdqu8        $OFFSETOFEND_H_POWERS-4*$VL($H_POWERS), $H_POW4
    vmovdqu8        $OFFSETOFEND_H_POWERS-3*$VL($H_POWERS), $H_POW3
    vmovdqu8        $OFFSETOFEND_H_POWERS-2*$VL($H_POWERS), $H_POW2

    # Update GHASH with 4*VL bytes of AAD at a time.
.Laad_loop_4x$local_label_suffix:
    vmovdqu8        0*$VL($AAD), $GHASHDATA0
    vmovdqu8        1*$VL($AAD), $GHASHDATA1
    vmovdqu8        2*$VL($AAD), $GHASHDATA2
    vmovdqu8        3*$VL($AAD), $GHASHDATA3
    @{[ _ghash_4x ]}
    sub             \$-4*$VL, $AAD  # shorter than 'add 4*VL' when VL=32
    add             \$-4*$VL, $AADLEN
    cmp             \$4*$VL-1, $AADLEN
    ja              .Laad_loop_4x$local_label_suffix

    # Update GHASH with VL bytes of AAD at a time.
    cmp             \$$VL, $AADLEN
    jb              .Laad_large_done$local_label_suffix
.Laad_loop_1x$local_label_suffix:
    vmovdqu8        ($AAD), $GHASHDATA0
    vpshufb         $BSWAP_MASK, $GHASHDATA0, $GHASHDATA0
    vpxord          $GHASHDATA0, $GHASH_ACC, $GHASH_ACC
    @{[ _ghash_mul  $H_POW1, $GHASH_ACC, $GHASH_ACC, $GFPOLY,
                    $GHASHDATA0, $GHASHDATA1, $GHASHDATA2 ]}
    @{[ _horizontal_xor $GHASH_ACC, $GHASH_ACC_XMM, $GHASH_ACC_XMM,
                        $GHASHDATA0_XMM, $GHASHDATA1_XMM, $GHASHDATA2_XMM ]}
    add             \$$VL, $AAD
    sub             \$$VL, $AADLEN
    cmp             \$$VL, $AADLEN
    jae             .Laad_loop_1x$local_label_suffix

.Laad_large_done$local_label_suffix:
    # Issue the vzeroupper that is needed after using ymm or zmm registers.
    # Do it here instead of at the end, to minimize overhead for small AADLEN.
    vzeroupper

    # GHASH the remaining data 16 bytes at a time, using xmm registers only.
.Laad_blockbyblock$local_label_suffix:
    test            $AADLEN, $AADLEN
    jz              .Laad_done$local_label_suffix
    vmovdqu         $OFFSETOFEND_H_POWERS-16($H_POWERS), $H_POW1_XMM
.Laad_loop_blockbyblock$local_label_suffix:
    vmovdqu         ($AAD), $GHASHDATA0_XMM
    vpshufb         $BSWAP_MASK_XMM, $GHASHDATA0_XMM, $GHASHDATA0_XMM
    vpxor           $GHASHDATA0_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM
    @{[ _ghash_mul  $H_POW1_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM, $GFPOLY_XMM,
                    $GHASHDATA0_XMM, $GHASHDATA1_XMM, $GHASHDATA2_XMM ]}
    add             \$16, $AAD
    sub             \$16, $AADLEN
    jnz             .Laad_loop_blockbyblock$local_label_suffix

.Laad_done$local_label_suffix:
    # Store the updated GHASH accumulator back to memory.
    vpshufb         $BSWAP_MASK_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM
    vmovdqu         $GHASH_ACC_XMM, ($GHASH_ACC_PTR)
___
    return $code;
}

# Do one non-last round of AES encryption on the counter blocks in V0-V3 using
# the round key that has been broadcast to all 128-bit lanes of \round_key.
sub _vaesenc_4x {
    my ($round_key) = @_;
    return <<___;
    vaesenc         $round_key, $V0, $V0
    vaesenc         $round_key, $V1, $V1
    vaesenc         $round_key, $V2, $V2
    vaesenc         $round_key, $V3, $V3
___
}

# Start the AES encryption of four vectors of counter blocks.
sub _ctr_begin_4x {
    return <<___;
    # Increment LE_CTR four times to generate four vectors of little-endian
    # counter blocks, swap each to big-endian, and store them in V0-V3.
    vpshufb         $BSWAP_MASK, $LE_CTR, $V0
    vpaddd          $LE_CTR_INC, $LE_CTR, $LE_CTR
    vpshufb         $BSWAP_MASK, $LE_CTR, $V1
    vpaddd          $LE_CTR_INC, $LE_CTR, $LE_CTR
    vpshufb         $BSWAP_MASK, $LE_CTR, $V2
    vpaddd          $LE_CTR_INC, $LE_CTR, $LE_CTR
    vpshufb         $BSWAP_MASK, $LE_CTR, $V3
    vpaddd          $LE_CTR_INC, $LE_CTR, $LE_CTR

    # AES "round zero": XOR in the zero-th round key.
    vpxord          $RNDKEY0, $V0, $V0
    vpxord          $RNDKEY0, $V1, $V1
    vpxord          $RNDKEY0, $V2, $V2
    vpxord          $RNDKEY0, $V3, $V3
___
}

# Do the last AES round for four vectors of counter blocks V0-V3, XOR source
# data with the resulting keystream, and write the result to DST and
# GHASHDATA[0-3].  (Implementation differs slightly, but has the same effect.)
sub _aesenclast_and_xor_4x {
    return <<___;
    # XOR the source data with the last round key, saving the result in
    # GHASHDATA[0-3].  This reduces latency by taking advantage of the
    # property vaesenclast(key, a) ^ b == vaesenclast(key ^ b, a).
    vpxord          0*$VL($SRC), $RNDKEYLAST, $GHASHDATA0
    vpxord          1*$VL($SRC), $RNDKEYLAST, $GHASHDATA1
    vpxord          2*$VL($SRC), $RNDKEYLAST, $GHASHDATA2
    vpxord          3*$VL($SRC), $RNDKEYLAST, $GHASHDATA3

    # Do the last AES round.  This handles the XOR with the source data
    # too, as per the optimization described above.
    vaesenclast     $GHASHDATA0, $V0, $GHASHDATA0
    vaesenclast     $GHASHDATA1, $V1, $GHASHDATA1
    vaesenclast     $GHASHDATA2, $V2, $GHASHDATA2
    vaesenclast     $GHASHDATA3, $V3, $GHASHDATA3

    # Store the en/decrypted data to DST.
    vmovdqu8        $GHASHDATA0, 0*$VL($DST)
    vmovdqu8        $GHASHDATA1, 1*$VL($DST)
    vmovdqu8        $GHASHDATA2, 2*$VL($DST)
    vmovdqu8        $GHASHDATA3, 3*$VL($DST)
___
}

$g_update_macro_expansion_count = 0;

# void aes_gcm_{enc,dec}_update_##suffix(const uint8_t *in, uint8_t *out,
#                                        size_t len, const AES_KEY *key,
#                                        const uint8_t ivec[16],
#                                        const u128 Htable[16],
#                                        uint8_t Xi[16]);
#
# This macro generates a GCM encryption or decryption update function with the
# above prototype (with \enc selecting which one).  This macro supports both
# VL=32 and VL=64.  _set_veclen must have been invoked with the desired length.
#
# This function computes the next portion of the CTR keystream, XOR's it with
# |len| bytes from |in|, and writes the resulting encrypted or decrypted data
# to |out|.  It also updates the GHASH accumulator |Xi| using the next |len|
# ciphertext bytes.
#
# |len| must be a multiple of 16, except on the last call where it can be any
# length.  The caller must do any buffering needed to ensure this.  Both
# in-place and out-of-place en/decryption are supported.
#
# |ivec| must give the current counter in big-endian format.  This function
# loads the counter from |ivec| and increments the loaded counter as needed, but
# it does *not* store the updated counter back to |ivec|.  The caller must
# update |ivec| if any more data segments follow.  Internally, only the low
# 32-bit word of the counter is incremented, following the GCM standard.
sub _aes_gcm_update {
    my $local_label_suffix = "__func" . ++$g_update_macro_expansion_count;

    my ($enc) = @_;

    my $code = "";

    # Function arguments
    ( $SRC, $DST, $DATALEN, $AESKEY, $BE_CTR_PTR, $H_POWERS, $GHASH_ACC_PTR ) =
      $win64
      ? ( @argregs[ 0 .. 3 ], "%rsi", "%rdi", "%r12" )
      : ( @argregs[ 0 .. 5 ], "%r12" );

    # Additional local variables

    # %rax, %k1, and %k2 are used as temporary registers.  BE_CTR_PTR is
    # also available as a temporary register after the counter is loaded.

    # AES key length in bytes
    ( $AESKEYLEN, $AESKEYLEN64 ) = ( "%r10d", "%r10" );

    # Pointer to the last AES round key for the chosen AES variant
    $RNDKEYLAST_PTR = "%r11";

    # In the main loop, V0-V3 are used as AES input and output.  Elsewhere
    # they are used as temporary registers.

    # GHASHDATA[0-3] hold the ciphertext blocks and GHASH input data.
    ( $GHASHDATA0, $GHASHDATA0_XMM ) = ( $V4, "%xmm4" );
    ( $GHASHDATA1, $GHASHDATA1_XMM ) = ( $V5, "%xmm5" );
    ( $GHASHDATA2, $GHASHDATA2_XMM ) = ( $V6, "%xmm6" );
    ( $GHASHDATA3, $GHASHDATA3_XMM ) = ( $V7, "%xmm7" );

    # BSWAP_MASK is the shuffle mask for byte-reflecting 128-bit values
    # using vpshufb, copied to all 128-bit lanes.
    ( $BSWAP_MASK, $BSWAP_MASK_XMM ) = ( $V8, "%xmm8" );

    # RNDKEY temporarily holds the next AES round key.
    $RNDKEY = $V9;

    # GHASH_ACC is the accumulator variable for GHASH.  When fully reduced,
    # only the lowest 128-bit lane can be nonzero.  When not fully reduced,
    # more than one lane may be used, and they need to be XOR'd together.
    ( $GHASH_ACC, $GHASH_ACC_XMM ) = ( $V10, "%xmm10" );

    # LE_CTR_INC is the vector of 32-bit words that need to be added to a
    # vector of little-endian counter blocks to advance it forwards.
    $LE_CTR_INC = $V11;

    # LE_CTR contains the next set of little-endian counter blocks.
    $LE_CTR = $V12;

    # RNDKEY0, RNDKEYLAST, and RNDKEY_M[9-1] contain cached AES round keys,
    # copied to all 128-bit lanes.  RNDKEY0 is the zero-th round key,
    # RNDKEYLAST the last, and RNDKEY_M\i the one \i-th from the last.
    (
        $RNDKEY0,   $RNDKEYLAST, $RNDKEY_M9, $RNDKEY_M8,
        $RNDKEY_M7, $RNDKEY_M6,  $RNDKEY_M5, $RNDKEY_M4,
        $RNDKEY_M3, $RNDKEY_M2,  $RNDKEY_M1
    ) = ( $V13, $V14, $V15, $V16, $V17, $V18, $V19, $V20, $V21, $V22, $V23 );

    # GHASHTMP[0-2] are temporary variables used by _ghash_step_4x.  These
    # cannot coincide with anything used for AES encryption, since for
    # performance reasons GHASH and AES encryption are interleaved.
    ( $GHASHTMP0, $GHASHTMP1, $GHASHTMP2 ) = ( $V24, $V25, $V26 );

    # H_POW[4-1] contain the powers of the hash key H^(4*VL/16)...H^1.  The
    # descending numbering reflects the order of the key powers.
    ( $H_POW4, $H_POW3, $H_POW2, $H_POW1 ) = ( $V27, $V28, $V29, $V30 );

    # GFPOLY contains the .Lgfpoly constant, copied to all 128-bit lanes.
    $GFPOLY = $V31;

    if ($win64) {
        $code .= <<___;
        @{[ _save_gpregs $BE_CTR_PTR, $H_POWERS, $GHASH_ACC_PTR ]}
        mov             64(%rsp), $BE_CTR_PTR     # arg5
        mov             72(%rsp), $H_POWERS       # arg6
        mov             80(%rsp), $GHASH_ACC_PTR  # arg7
        @{[ _save_xmmregs (6 .. 15) ]}
        .seh_endprologue
___
    }
    else {
        $code .= <<___;
        @{[ _save_gpregs $GHASH_ACC_PTR ]}
        mov             16(%rsp), $GHASH_ACC_PTR  # arg7
___
    }

    if ($enc) {
        $code .= <<___;
#ifdef BORINGSSL_DISPATCH_TEST
        .extern BORINGSSL_function_hit
        movb \$1,BORINGSSL_function_hit+@{[ $VL < 64 ? 6 : 7 ]}(%rip)
#endif
___
    }
    $code .= <<___;
    # Load some constants.
    vbroadcasti32x4 .Lbswap_mask(%rip), $BSWAP_MASK
    vbroadcasti32x4 .Lgfpoly(%rip), $GFPOLY

    # Load the GHASH accumulator and the starting counter.
    # BoringSSL passes these values in big endian format.
    vmovdqu         ($GHASH_ACC_PTR), $GHASH_ACC_XMM
    vpshufb         $BSWAP_MASK_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM
    vbroadcasti32x4 ($BE_CTR_PTR), $LE_CTR
    vpshufb         $BSWAP_MASK, $LE_CTR, $LE_CTR

    # Load the AES key length in bytes.  BoringSSL stores number of rounds
    # minus 1, so convert using: AESKEYLEN = 4 * aeskey->rounds - 20.
    movl            $OFFSETOF_AES_ROUNDS($AESKEY), $AESKEYLEN
    lea             -20(,$AESKEYLEN,4), $AESKEYLEN

    # Make RNDKEYLAST_PTR point to the last AES round key.  This is the
    # round key with index 10, 12, or 14 for AES-128, AES-192, or AES-256
    # respectively.  Then load the zero-th and last round keys.
    lea             6*16($AESKEY,$AESKEYLEN64,4), $RNDKEYLAST_PTR
    vbroadcasti32x4 ($AESKEY), $RNDKEY0
    vbroadcasti32x4 ($RNDKEYLAST_PTR), $RNDKEYLAST

    # Finish initializing LE_CTR by adding [0, 1, ...] to its low words.
    vpaddd          .Lctr_pattern(%rip), $LE_CTR, $LE_CTR

    # Initialize LE_CTR_INC to contain VL/16 in all 128-bit lanes.
    vbroadcasti32x4 .Linc_@{[ $VL / 16 ]}blocks(%rip), $LE_CTR_INC

    # If there are at least 4*VL bytes of data, then continue into the loop
    # that processes 4*VL bytes of data at a time.  Otherwise skip it.
    cmp             \$4*$VL-1, $DATALEN
    jbe             .Lcrypt_loop_4x_done$local_label_suffix

    # Load powers of the hash key.
    vmovdqu8        $OFFSETOFEND_H_POWERS-4*$VL($H_POWERS), $H_POW4
    vmovdqu8        $OFFSETOFEND_H_POWERS-3*$VL($H_POWERS), $H_POW3
    vmovdqu8        $OFFSETOFEND_H_POWERS-2*$VL($H_POWERS), $H_POW2
    vmovdqu8        $OFFSETOFEND_H_POWERS-1*$VL($H_POWERS), $H_POW1
___

    # Main loop: en/decrypt and hash 4 vectors at a time.
    #
    # When possible, interleave the AES encryption of the counter blocks
    # with the GHASH update of the ciphertext blocks.  This improves
    # performance on many CPUs because the execution ports used by the VAES
    # instructions often differ from those used by vpclmulqdq and other
    # instructions used in GHASH.  For example, many Intel CPUs dispatch
    # vaesenc to ports 0 and 1 and vpclmulqdq to port 5.
    #
    # The interleaving is easiest to do during decryption, since during
    # decryption the ciphertext blocks are immediately available.  For
    # encryption, instead encrypt the first set of blocks, then hash those
    # blocks while encrypting the next set of blocks, repeat that as
    # needed, and finally hash the last set of blocks.

    if ($enc) {
        $code .= <<___;
        # Encrypt the first 4 vectors of plaintext blocks.  Leave the resulting
        # ciphertext in GHASHDATA[0-3] for GHASH.
        @{[ _ctr_begin_4x ]}
        lea             16($AESKEY), %rax
.Lvaesenc_loop_first_4_vecs$local_label_suffix:
        vbroadcasti32x4 (%rax), $RNDKEY
        @{[ _vaesenc_4x $RNDKEY ]}
        add             \$16, %rax
        cmp             %rax, $RNDKEYLAST_PTR
        jne             .Lvaesenc_loop_first_4_vecs$local_label_suffix
        @{[ _aesenclast_and_xor_4x ]}
        sub             \$-4*$VL, $SRC  # shorter than 'add 4*VL' when VL=32
        sub             \$-4*$VL, $DST
        add             \$-4*$VL, $DATALEN
        cmp             \$4*$VL-1, $DATALEN
        jbe             .Lghash_last_ciphertext_4x$local_label_suffix
___
    }

    # Cache as many additional AES round keys as possible.
    for my $i ( reverse 1 .. 9 ) {
        $code .= <<___;
        vbroadcasti32x4 -$i*16($RNDKEYLAST_PTR), ${"RNDKEY_M$i"}
___
    }

    $code .= <<___;
.Lcrypt_loop_4x$local_label_suffix:
___

    # If decrypting, load more ciphertext blocks into GHASHDATA[0-3].  If
    # encrypting, GHASHDATA[0-3] already contain the previous ciphertext.
    if ( !$enc ) {
        $code .= <<___;
        vmovdqu8        0*$VL($SRC), $GHASHDATA0
        vmovdqu8        1*$VL($SRC), $GHASHDATA1
        vmovdqu8        2*$VL($SRC), $GHASHDATA2
        vmovdqu8        3*$VL($SRC), $GHASHDATA3
___
    }

    $code .= <<___;
    # Start the AES encryption of the counter blocks.
    @{[ _ctr_begin_4x ]}
    cmp             \$24, $AESKEYLEN
    jl              .Laes128$local_label_suffix
    je              .Laes192$local_label_suffix
    # AES-256
    vbroadcasti32x4 -13*16($RNDKEYLAST_PTR), $RNDKEY
    @{[ _vaesenc_4x $RNDKEY ]}
    vbroadcasti32x4 -12*16($RNDKEYLAST_PTR), $RNDKEY
    @{[ _vaesenc_4x $RNDKEY ]}
.Laes192$local_label_suffix:
    vbroadcasti32x4 -11*16($RNDKEYLAST_PTR), $RNDKEY
    @{[ _vaesenc_4x $RNDKEY ]}
    vbroadcasti32x4 -10*16($RNDKEYLAST_PTR), $RNDKEY
    @{[ _vaesenc_4x $RNDKEY ]}
.Laes128$local_label_suffix:
___

    # Finish the AES encryption of the counter blocks in V0-V3, interleaved
    # with the GHASH update of the ciphertext blocks in GHASHDATA[0-3].
    for my $i ( reverse 1 .. 9 ) {
        $code .= <<___;
        @{[ _ghash_step_4x  (9 - $i) ]}
        @{[ _vaesenc_4x     ${"RNDKEY_M$i"} ]}
___
    }
    $code .= <<___;
    @{[ _ghash_step_4x  9 ]}
    @{[ _aesenclast_and_xor_4x ]}
    sub             \$-4*$VL, $SRC  # shorter than 'add 4*VL' when VL=32
    sub             \$-4*$VL, $DST
    add             \$-4*$VL, $DATALEN
    cmp             \$4*$VL-1, $DATALEN
    ja              .Lcrypt_loop_4x$local_label_suffix
___

    if ($enc) {

        # Update GHASH with the last set of ciphertext blocks.
        $code .= <<___;
.Lghash_last_ciphertext_4x$local_label_suffix:
        @{[ _ghash_4x ]}
___
    }

    my $POWERS_PTR = $BE_CTR_PTR;    # BE_CTR_PTR is free to be reused.

    $code .= <<___;
.Lcrypt_loop_4x_done$local_label_suffix:
    # Check whether any data remains.
    test            $DATALEN, $DATALEN
    jz              .Ldone$local_label_suffix

    # The data length isn't a multiple of 4*VL.  Process the remaining data
    # of length 1 <= DATALEN < 4*VL, up to one vector (VL bytes) at a time.
    # Going one vector at a time may seem inefficient compared to having
    # separate code paths for each possible number of vectors remaining.
    # However, using a loop keeps the code size down, and it performs
    # surprising well; modern CPUs will start executing the next iteration
    # before the previous one finishes and also predict the number of loop
    # iterations.  For a similar reason, we roll up the AES rounds.
    #
    # On the last iteration, the remaining length may be less than VL.
    # Handle this using masking.
    #
    # Since there are enough key powers available for all remaining data,
    # there is no need to do a GHASH reduction after each iteration.
    # Instead, multiply each remaining block by its own key power, and only
    # do a GHASH reduction at the very end.

    # Make POWERS_PTR point to the key powers [H^N, H^(N-1), ...] where N
    # is the number of blocks that remain.
    mov             $DATALEN, %rax
    neg             %rax
    and             \$-16, %rax  # -round_up(DATALEN, 16)
    lea             $OFFSETOFEND_H_POWERS($H_POWERS,%rax), $POWERS_PTR
___

    # Start collecting the unreduced GHASH intermediate value LO, MI, HI.
    my ( $LO, $LO_XMM ) = ( $GHASHDATA0, $GHASHDATA0_XMM );
    my ( $MI, $MI_XMM ) = ( $GHASHDATA1, $GHASHDATA1_XMM );
    my ( $HI, $HI_XMM ) = ( $GHASHDATA2, $GHASHDATA2_XMM );
    $code .= <<___;
    vpxor           $LO_XMM, $LO_XMM, $LO_XMM
    vpxor           $MI_XMM, $MI_XMM, $MI_XMM
    vpxor           $HI_XMM, $HI_XMM, $HI_XMM

    cmp             \$$VL, $DATALEN
    jb              .Lpartial_vec$local_label_suffix

.Lcrypt_loop_1x$local_label_suffix:
    # Process a full vector of length VL.

    # Encrypt a vector of counter blocks.
    vpshufb         $BSWAP_MASK, $LE_CTR, $V0
    vpaddd          $LE_CTR_INC, $LE_CTR, $LE_CTR
    vpxord          $RNDKEY0, $V0, $V0
    lea             16($AESKEY), %rax
.Lvaesenc_loop_tail_full_vec$local_label_suffix:
    vbroadcasti32x4 (%rax), $RNDKEY
    vaesenc         $RNDKEY, $V0, $V0
    add             \$16, %rax
    cmp             %rax, $RNDKEYLAST_PTR
    jne             .Lvaesenc_loop_tail_full_vec$local_label_suffix
    vaesenclast     $RNDKEYLAST, $V0, $V0

    # XOR the data with the vector of keystream blocks.
    vmovdqu8        ($SRC), $V1
    vpxord          $V1, $V0, $V0
    vmovdqu8        $V0, ($DST)

    # Update GHASH with the ciphertext blocks, without reducing.
    vmovdqu8        ($POWERS_PTR), $H_POW1
    vpshufb         $BSWAP_MASK, @{[ $enc ? $V0 : $V1 ]}, $V0
    vpxord          $GHASH_ACC, $V0, $V0
    @{[ _ghash_mul_noreduce $H_POW1, $V0, $LO, $MI, $HI, $GHASHDATA3,
                            $V1, $V2, $V3 ]}
    vpxor           $GHASH_ACC_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM

    add             \$$VL, $POWERS_PTR
    add             \$$VL, $SRC
    add             \$$VL, $DST
    sub             \$$VL, $DATALEN
    cmp             \$$VL, $DATALEN
    jae             .Lcrypt_loop_1x$local_label_suffix

    test            $DATALEN, $DATALEN
    jz              .Lreduce$local_label_suffix

.Lpartial_vec$local_label_suffix:
    # Process a partial vector of length 1 <= DATALEN < VL.

    # Set the data mask %k1 to DATALEN 1's.
    # Set the key powers mask %k2 to round_up(DATALEN, 16) 1's.
    mov             \$-1, %rax
    bzhi            $DATALEN, %rax, %rax
    @{[ $VL < 64 ? "kmovd %eax, %k1" : "kmovq %rax, %k1" ]}
    add             \$15, $DATALEN
    and             \$-16, $DATALEN
    mov             \$-1, %rax
    bzhi            $DATALEN, %rax, %rax
    @{[ $VL < 64 ? "kmovd %eax, %k2" : "kmovq %rax, %k2" ]}

    # Encrypt one last vector of counter blocks.  This does not need to be
    # masked.  The counter does not need to be incremented here.
    vpshufb         $BSWAP_MASK, $LE_CTR, $V0
    vpxord          $RNDKEY0, $V0, $V0
    lea             16($AESKEY), %rax
.Lvaesenc_loop_tail_partialvec$local_label_suffix:
    vbroadcasti32x4 (%rax), $RNDKEY
    vaesenc         $RNDKEY, $V0, $V0
    add             \$16, %rax
    cmp             %rax, $RNDKEYLAST_PTR
    jne             .Lvaesenc_loop_tail_partialvec$local_label_suffix
    vaesenclast     $RNDKEYLAST, $V0, $V0

    # XOR the data with the appropriate number of keystream bytes.
    vmovdqu8        ($SRC), $V1\{%k1}{z}
    vpxord          $V1, $V0, $V0
    vmovdqu8        $V0, ($DST){%k1}

    # Update GHASH with the ciphertext block(s), without reducing.
    #
    # In the case of DATALEN < VL, the ciphertext is zero-padded to VL.
    # (If decrypting, it's done by the above masked load.  If encrypting,
    # it's done by the below masked register-to-register move.)  Note that
    # if DATALEN <= VL - 16, there will be additional padding beyond the
    # padding of the last block specified by GHASH itself; i.e., there may
    # be whole block(s) that get processed by the GHASH multiplication and
    # reduction instructions but should not actually be included in the
    # GHASH.  However, any such blocks are all-zeroes, and the values that
    # they're multiplied with are also all-zeroes.  Therefore they just add
    # 0 * 0 = 0 to the final GHASH result, which makes no difference.
    vmovdqu8        ($POWERS_PTR), $H_POW1\{%k2}{z}
    @{[ $enc ? "vmovdqu8 $V0, $V1\{%k1}{z}" : "" ]}
    vpshufb         $BSWAP_MASK, $V1, $V0
    vpxord          $GHASH_ACC, $V0, $V0
    @{[ _ghash_mul_noreduce $H_POW1, $V0, $LO, $MI, $HI, $GHASHDATA3,
                            $V1, $V2, $V3 ]}

.Lreduce$local_label_suffix:
    # Finally, do the GHASH reduction.
    @{[ _ghash_reduce   $LO, $MI, $HI, $GFPOLY, $V0 ]}
    @{[ _horizontal_xor $HI, $HI_XMM, $GHASH_ACC_XMM,
                        "%xmm0", "%xmm1", "%xmm2" ]}

.Ldone$local_label_suffix:
    # Store the updated GHASH accumulator back to memory.
    vpshufb         $BSWAP_MASK_XMM, $GHASH_ACC_XMM, $GHASH_ACC_XMM
    vmovdqu         $GHASH_ACC_XMM, ($GHASH_ACC_PTR)

    vzeroupper      # This is needed after using ymm or zmm registers.
___
    return $code;
}

# void gcm_gmult_vpclmulqdq_avx10(uint8_t Xi[16], const u128 Htable[16]);
$code .= _begin_func "gcm_gmult_vpclmulqdq_avx10", 1;
{
    my ( $GHASH_ACC_PTR, $H_POWERS ) = @argregs[ 0 .. 1 ];
    my ( $GHASH_ACC, $BSWAP_MASK, $H_POW1, $GFPOLY, $T0, $T1, $T2 ) =
      map( "%xmm$_", ( 0 .. 6 ) );

    $code .= <<___;
    @{[ _save_xmmregs (6) ]}
    .seh_endprologue

    vmovdqu         ($GHASH_ACC_PTR), $GHASH_ACC
    vmovdqu         .Lbswap_mask(%rip), $BSWAP_MASK
    vmovdqu         $OFFSETOFEND_H_POWERS-16($H_POWERS), $H_POW1
    vmovdqu         .Lgfpoly(%rip), $GFPOLY
    vpshufb         $BSWAP_MASK, $GHASH_ACC, $GHASH_ACC

    @{[ _ghash_mul  $H_POW1, $GHASH_ACC, $GHASH_ACC, $GFPOLY, $T0, $T1, $T2 ]}

    vpshufb         $BSWAP_MASK, $GHASH_ACC, $GHASH_ACC
    vmovdqu         $GHASH_ACC, ($GHASH_ACC_PTR)
___
}
$code .= _end_func;

# Disabled until significant deployment of AVX10/256 is seen.  The separate
# *_vaes_avx2 implementation provides the only 256-bit support for now.
#
# $code .= _begin_func "gcm_init_vpclmulqdq_avx10_256", 0;
# $code .= _aes_gcm_init;
# $code .= _end_func;
#
# $code .= _begin_func "gcm_ghash_vpclmulqdq_avx10_256", 1;
# $code .= _ghash_update;
# $code .= _end_func;
#
# $code .= _begin_func "aes_gcm_enc_update_vaes_avx10_256", 1;
# $code .= _aes_gcm_update 1;
# $code .= _end_func;
#
# $code .= _begin_func "aes_gcm_dec_update_vaes_avx10_256", 1;
# $code .= _aes_gcm_update 0;
# $code .= _end_func;

_set_veclen 64;

$code .= _begin_func "gcm_init_vpclmulqdq_avx10_512", 0;
$code .= _aes_gcm_init;
$code .= _end_func;

$code .= _begin_func "gcm_ghash_vpclmulqdq_avx10_512", 1;
$code .= _ghash_update;
$code .= _end_func;

$code .= _begin_func "aes_gcm_enc_update_vaes_avx10_512", 1;
$code .= _aes_gcm_update 1;
$code .= _end_func;

$code .= _begin_func "aes_gcm_dec_update_vaes_avx10_512", 1;
$code .= _aes_gcm_update 0;
$code .= _end_func;

print $code;
close STDOUT or die "error closing STDOUT: $!";
exit 0;
