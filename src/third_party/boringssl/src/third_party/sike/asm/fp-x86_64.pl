#! /usr/bin/env perl
#
# April 2019
#
# Abstract: field arithmetic in x64 assembly for SIDH/p503

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../../crypto/perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$PREFIX="sike";
$bmi2_adx = 1;

$code.=<<___;
.text

# p503 x 2
.Lp503x2:
.quad   0xFFFFFFFFFFFFFFFE
.quad   0xFFFFFFFFFFFFFFFF
.quad   0x57FFFFFFFFFFFFFF
.quad   0x2610B7B44423CF41
.quad   0x3737ED90F6FCFB5E
.quad   0xC08B8D7BB4EF49A0
.quad   0x0080CDEA83023C3C

# p503 + 1
.Lp503p1:
.quad   0xAC00000000000000
.quad   0x13085BDA2211E7A0
.quad   0x1B9BF6C87B7E7DAF
.quad   0x6045C6BDDA77A4D0
.quad   0x004066F541811E1E

.Lp503p1_nz:
.quad    0xAC00000000000000
.quad    0x13085BDA2211E7A0
.quad    0x1B9BF6C87B7E7DAF
.quad    0x6045C6BDDA77A4D0
.quad    0x004066F541811E1E

.extern OPENSSL_ia32cap_P
.hidden OPENSSL_ia32cap_P

___

# Performs schoolbook multiplication of 128-bit with 320-bit
# number. Uses MULX, ADOX, ADCX instruction.
sub mul128x320_school {
  my ($idxM0,$M0,$M1,$T0,$T1,$T2,$T3,$T4,$T5,$T6,$T7,$T8,$T9)=@_;
  my ($MUL0,$MUL8)=map("$idxM0+$_(%$M0)", (0,8));
  my $body.=<<___;
    mov    $MUL0, %rdx
    mulx   0+$M1, %$T0, %$T1       # T0 <- C0_final
    mulx   8+$M1, %$T4, %$T2

    xor    %rax, %rax
    mulx   16+$M1, %$T5, %$T3
    adox   %$T4, %$T1
    adox   %$T5, %$T2
    mulx   24+$M1, %$T7, %$T4
    adox   %$T7, %$T3
    mulx   32+$M1, %$T6, %$T5
    adox   %$T6, %$T4
    adox   %rax, %$T5

    mov    $MUL8, %rdx
    mulx   0+$M1, %$T6, %$T7
    adcx   %$T6, %$T1               # T1 <- C1_final
    adcx   %$T7, %$T2
    mulx   8+$M1, %$T8, %$T6
    adcx   %$T6, %$T3
    mulx   16+$M1, %$T7, %$T9
    adcx   %$T9, %$T4
    mulx   24+$M1, %$T9, %$T6
    adcx   %$T6, %$T5
    mulx   32+$M1, %rdx, %$T6
    adcx   %rax, %$T6

    xor    %rax, %rax
    adox   %$T8, %$T2
    adox   %$T7, %$T3
    adox   %$T9, %$T4
    adox   %rdx, %$T5
    adox   %rax, %$T6

___
  return $body;
}

# Compute z = x + y (mod p).
# Operation: c [rdx] = a [rdi] + b [rsi]
$code.=<<___;
.globl  ${PREFIX}_fpadd
.type   ${PREFIX}_fpadd,\@function,3
${PREFIX}_fpadd:
.cfi_startproc
  push %r12
.cfi_adjust_cfa_offset  8
.cfi_offset r12, -16
  push %r13
.cfi_adjust_cfa_offset  8
.cfi_offset r13, -24
  push %r14
.cfi_adjust_cfa_offset  8
.cfi_offset r14, -32
  push %r15
.cfi_adjust_cfa_offset  8
.cfi_offset r15, -40

  xor  %rax, %rax

  mov  0x0(%rdi),  %r8
  mov  0x8(%rdi),  %r9
  mov 0x10(%rdi), %r10
  mov 0x18(%rdi), %r11
  mov 0x20(%rdi), %r12
  mov 0x28(%rdi), %r13
  mov 0x30(%rdi), %r14
  mov 0x38(%rdi), %r15

  add  0x0(%rsi),  %r8
  adc  0x8(%rsi),  %r9
  adc 0x10(%rsi), %r10
  adc 0x18(%rsi), %r11
  adc 0x20(%rsi), %r12
  adc 0x28(%rsi), %r13
  adc 0x30(%rsi), %r14
  adc 0x38(%rsi), %r15

  mov .Lp503x2(%rip), %rcx;
  sub %rcx, %r8
  mov 8+.Lp503x2(%rip), %rcx;
  sbb %rcx, %r9
  sbb %rcx, %r10
  mov 16+.Lp503x2(%rip), %rcx;
  sbb %rcx, %r11
  mov 24+.Lp503x2(%rip), %rcx;
  sbb %rcx, %r12
  mov 32+.Lp503x2(%rip), %rcx;
  sbb %rcx, %r13
  mov 40+.Lp503x2(%rip), %rcx;
  sbb %rcx, %r14
  mov 48+.Lp503x2(%rip), %rcx;
  sbb %rcx, %r15
  sbb \$0, %rax

  mov .Lp503x2(%rip), %rdi
  and %rax, %rdi
  mov 8+.Lp503x2(%rip), %rsi
  and %rax, %rsi
  mov 16+.Lp503x2(%rip), %rcx
  and %rax, %rcx

  add %rdi, %r8
  mov %r8, 0x0(%rdx)
  adc %rsi, %r9
  mov %r9, 0x8(%rdx)
  adc %rsi, %r10
  mov %r10, 0x10(%rdx)
  adc %rcx, %r11
  mov %r11, 0x18(%rdx)

  setc   %cl

  mov 24+.Lp503x2(%rip), %r8
  and %rax, %r8
  mov 32+.Lp503x2(%rip), %r9
  and %rax, %r9
  mov 40+.Lp503x2(%rip), %r10
  and %rax, %r10
  mov 48+.Lp503x2(%rip), %r11
  and %rax, %r11

  bt    \$0, %rcx

  adc  %r8, %r12
  mov %r12, 0x20(%rdx)
  adc  %r9, %r13
  mov %r13, 0x28(%rdx)
  adc %r10, %r14
  mov %r14, 0x30(%rdx)
  adc %r11, %r15
  mov %r15, 0x38(%rdx)

  pop %r15
.cfi_adjust_cfa_offset  -8
  pop %r14
.cfi_adjust_cfa_offset  -8
  pop %r13
.cfi_adjust_cfa_offset  -8
  pop %r12
.cfi_adjust_cfa_offset  -8
  ret
.cfi_endproc
___



# Loads data to XMM0 and XMM1 and
# conditionaly swaps depending on XMM3
sub cswap_block16() {
  my $idx = shift;
  $idx *= 16;
  ("
    movdqu   $idx(%rdi), %xmm0
    movdqu   $idx(%rsi), %xmm1
    movdqa   %xmm1, %xmm2
    pxor     %xmm0, %xmm2
    pand     %xmm3, %xmm2
    pxor     %xmm2, %xmm0
    pxor     %xmm2, %xmm1
    movdqu   %xmm0, $idx(%rdi)
    movdqu   %xmm1, $idx(%rsi)
  ");
}

# Conditionally swaps bits in x and y in constant time.
# mask indicates bits to be swapped (set bits are swapped)
# Operation: [rdi] <-> [rsi] if rdx==1
sub cswap {
  # P[0].X with Q[0].X
  foreach ( 0.. 3){$BLOCKS.=eval "&cswap_block16($_)";}
  # P[0].Z with Q[0].Z
  foreach ( 4.. 7){$BLOCKS.=eval "&cswap_block16($_)";}
  # P[1].X with Q[1].X
  foreach ( 8..11){$BLOCKS.=eval "&cswap_block16($_)";}
  # P[1].Z with Q[1].Z
  foreach (12..15){$BLOCKS.=eval "&cswap_block16($_)";}

  my $body =<<___;
.globl  ${PREFIX}_cswap_asm
.type   ${PREFIX}_cswap_asm,\@function,3
${PREFIX}_cswap_asm:
  # Fill XMM3. After this step first half of XMM3 is
  # just zeros and second half is whatever in RDX
  mov   %rdx, %xmm3

  # Copy lower double word everywhere else. So that
  # XMM3=RDX|RDX. As RDX has either all bits set
  # or non result will be that XMM3 has also either
  # all bits set or non of them. 68 = 01000100b
  pshufd  \$68, %xmm3, %xmm3
  $BLOCKS
  ret
___
  ($body)
}
$code.=&cswap();

# Field subtraction
# Operation: c [rdx] = a [rdi] - b [rsi]
$code.=<<___;
.globl  ${PREFIX}_fpsub
.type   ${PREFIX}_fpsub,\@function,3
${PREFIX}_fpsub:
.cfi_startproc
  push   %r12
.cfi_adjust_cfa_offset  8
.cfi_offset r12, -16
  push   %r13
.cfi_adjust_cfa_offset  8
.cfi_offset r13, -24
  push   %r14
.cfi_adjust_cfa_offset  8
.cfi_offset r14, -32
  push   %r15
.cfi_adjust_cfa_offset  8
.cfi_offset r15, -40

  xor %rax, %rax

  mov  0x0(%rdi), %r8
  mov  0x8(%rdi), %r9
  mov 0x10(%rdi), %r10
  mov 0x18(%rdi), %r11
  mov 0x20(%rdi), %r12
  mov 0x28(%rdi), %r13
  mov 0x30(%rdi), %r14
  mov 0x38(%rdi), %r15

  sub  0x0(%rsi), %r8
  sbb  0x8(%rsi), %r9
  sbb 0x10(%rsi), %r10
  sbb 0x18(%rsi), %r11
  sbb 0x20(%rsi), %r12
  sbb 0x28(%rsi), %r13
  sbb 0x30(%rsi), %r14
  sbb 0x38(%rsi), %r15
  sbb \$0x0, %rax

  mov .Lp503x2(%rip), %rdi
  and %rax, %rdi
  mov 0x8+.Lp503x2(%rip), %rsi
  and %rax, %rsi
  mov 0x10+.Lp503x2(%rip), %rcx
  and %rax, %rcx

  add %rdi,        %r8
  adc %rsi,        %r9
  adc %rsi,       %r10
  adc %rcx,       %r11
  mov %r8,   0x0(%rdx)
  mov %r9,   0x8(%rdx)
  mov %r10, 0x10(%rdx)
  mov %r11, 0x18(%rdx)

  setc %cl

  mov 0x18+.Lp503x2(%rip),  %r8
  and %rax,  %r8
  mov 0x20+.Lp503x2(%rip),  %r9
  and %rax,  %r9
  mov 0x28+.Lp503x2(%rip), %r10
  and %rax, %r10
  mov 0x30+.Lp503x2(%rip), %r11
  and %rax, %r11

  bt \$0x0, %rcx

  adc %r8, %r12
  adc %r9, %r13
  adc %r10, %r14
  adc %r11, %r15
  mov %r12, 0x20(%rdx)
  mov %r13, 0x28(%rdx)
  mov %r14, 0x30(%rdx)
  mov %r15, 0x38(%rdx)

  pop %r15
.cfi_adjust_cfa_offset  -8
  pop %r14
.cfi_adjust_cfa_offset  -8
  pop %r13
.cfi_adjust_cfa_offset  -8
  pop %r12
.cfi_adjust_cfa_offset  -8
  ret
.cfi_endproc
___

#  503-bit multiprecision addition
#  Operation: c [rdx] = a [rdi] + b [rsi]
$code.=<<___;
.globl  ${PREFIX}_mpadd_asm
.type   ${PREFIX}_mpadd_asm,\@function,3
${PREFIX}_mpadd_asm:
.cfi_startproc
  mov  0x0(%rdi), %r8
  mov  0x8(%rdi), %r9
  mov 0x10(%rdi), %r10
  mov 0x18(%rdi), %r11
  add  0x0(%rsi), %r8
  adc  0x8(%rsi), %r9
  adc 0x10(%rsi), %r10
  adc 0x18(%rsi), %r11
  mov %r8,   0x0(%rdx)
  mov %r9,   0x8(%rdx)
  mov %r10, 0x10(%rdx)
  mov %r11, 0x18(%rdx)

  mov 0x20(%rdi), %r8
  mov 0x28(%rdi), %r9
  mov 0x30(%rdi), %r10
  mov 0x38(%rdi), %r11
  adc 0x20(%rsi), %r8
  adc 0x28(%rsi), %r9
  adc 0x30(%rsi), %r10
  adc 0x38(%rsi), %r11
  mov %r8,  0x20(%rdx)
  mov %r9,  0x28(%rdx)
  mov %r10, 0x30(%rdx)
  mov %r11, 0x38(%rdx)
  ret
.cfi_endproc
___

#  2x503-bit multiprecision subtraction
#  Operation: c [rdx] = a [rdi] - b [rsi].
#  Returns borrow mask
$code.=<<___;
.globl  ${PREFIX}_mpsubx2_asm
.type   ${PREFIX}_mpsubx2_asm,\@function,3
${PREFIX}_mpsubx2_asm:
.cfi_startproc
  xor %rax, %rax

  mov  0x0(%rdi), %r8
  mov  0x8(%rdi), %r9
  mov 0x10(%rdi), %r10
  mov 0x18(%rdi), %r11
  mov 0x20(%rdi), %rcx
  sub  0x0(%rsi), %r8
  sbb  0x8(%rsi), %r9
  sbb 0x10(%rsi), %r10
  sbb 0x18(%rsi), %r11
  sbb 0x20(%rsi), %rcx
  mov %r8,   0x0(%rdx)
  mov %r9,   0x8(%rdx)
  mov %r10, 0x10(%rdx)
  mov %r11, 0x18(%rdx)
  mov %rcx, 0x20(%rdx)

  mov 0x28(%rdi), %r8
  mov 0x30(%rdi), %r9
  mov 0x38(%rdi), %r10
  mov 0x40(%rdi), %r11
  mov 0x48(%rdi), %rcx
  sbb 0x28(%rsi), %r8
  sbb 0x30(%rsi), %r9
  sbb 0x38(%rsi), %r10
  sbb 0x40(%rsi), %r11
  sbb 0x48(%rsi), %rcx
  mov %r8,  0x28(%rdx)
  mov %r9,  0x30(%rdx)
  mov %r10, 0x38(%rdx)
  mov %r11, 0x40(%rdx)
  mov %rcx, 0x48(%rdx)

  mov 0x50(%rdi), %r8
  mov 0x58(%rdi), %r9
  mov 0x60(%rdi), %r10
  mov 0x68(%rdi), %r11
  mov 0x70(%rdi), %rcx
  sbb 0x50(%rsi), %r8
  sbb 0x58(%rsi), %r9
  sbb 0x60(%rsi), %r10
  sbb 0x68(%rsi), %r11
  sbb 0x70(%rsi), %rcx
  mov %r8,  0x50(%rdx)
  mov %r9,  0x58(%rdx)
  mov %r10, 0x60(%rdx)
  mov %r11, 0x68(%rdx)
  mov %rcx, 0x70(%rdx)

  mov 0x78(%rdi), %r8
  sbb 0x78(%rsi), %r8
  sbb \$0x0, %rax
  mov %r8, 0x78(%rdx)
  ret
.cfi_endproc
___

#  Double 2x503-bit multiprecision subtraction
#  Operation: c [rdx] = c [rdx] - a [rdi] - b [rsi]
$code.=<<___;
.globl  ${PREFIX}_mpdblsubx2_asm
.type   ${PREFIX}_mpdblsubx2_asm,\@function,3
${PREFIX}_mpdblsubx2_asm:
.cfi_startproc
  push   %r12
.cfi_adjust_cfa_offset 8
.cfi_offset r12, -16
  push   %r13
.cfi_adjust_cfa_offset 8
.cfi_offset r13, -24
  push   %r14
.cfi_adjust_cfa_offset 8
.cfi_offset r14, -32

  xor %rax, %rax

  mov  0x0(%rdx), %r8
  mov  0x8(%rdx), %r9
  mov 0x10(%rdx), %r10
  mov 0x18(%rdx), %r11
  mov 0x20(%rdx), %r12
  mov 0x28(%rdx), %r13
  mov 0x30(%rdx), %r14
  mov 0x38(%rdx), %rcx
  sub  0x0(%rdi), %r8
  sbb  0x8(%rdi), %r9
  sbb 0x10(%rdi), %r10
  sbb 0x18(%rdi), %r11
  sbb 0x20(%rdi), %r12
  sbb 0x28(%rdi), %r13
  sbb 0x30(%rdi), %r14
  sbb 0x38(%rdi), %rcx
  adc \$0x0, %rax

  sub  0x0(%rsi), %r8
  sbb  0x8(%rsi), %r9
  sbb 0x10(%rsi), %r10
  sbb 0x18(%rsi), %r11
  sbb 0x20(%rsi), %r12
  sbb 0x28(%rsi), %r13
  sbb 0x30(%rsi), %r14
  sbb 0x38(%rsi), %rcx
  adc \$0x0, %rax

  mov %r8,   0x0(%rdx)
  mov %r9,   0x8(%rdx)
  mov %r10, 0x10(%rdx)
  mov %r11, 0x18(%rdx)
  mov %r12, 0x20(%rdx)
  mov %r13, 0x28(%rdx)
  mov %r14, 0x30(%rdx)
  mov %rcx, 0x38(%rdx)

  mov 0x40(%rdx), %r8
  mov 0x48(%rdx), %r9
  mov 0x50(%rdx), %r10
  mov 0x58(%rdx), %r11
  mov 0x60(%rdx), %r12
  mov 0x68(%rdx), %r13
  mov 0x70(%rdx), %r14
  mov 0x78(%rdx), %rcx

  sub %rax, %r8
  sbb 0x40(%rdi), %r8
  sbb 0x48(%rdi), %r9
  sbb 0x50(%rdi), %r10
  sbb 0x58(%rdi), %r11
  sbb 0x60(%rdi), %r12
  sbb 0x68(%rdi), %r13
  sbb 0x70(%rdi), %r14
  sbb 0x78(%rdi), %rcx
  sub 0x40(%rsi), %r8
  sbb 0x48(%rsi), %r9
  sbb 0x50(%rsi), %r10
  sbb 0x58(%rsi), %r11
  sbb 0x60(%rsi), %r12
  sbb 0x68(%rsi), %r13
  sbb 0x70(%rsi), %r14
  sbb 0x78(%rsi), %rcx

  mov %r8,  0x40(%rdx)
  mov %r9,  0x48(%rdx)
  mov %r10, 0x50(%rdx)
  mov %r11, 0x58(%rdx)
  mov %r12, 0x60(%rdx)
  mov %r13, 0x68(%rdx)
  mov %r14, 0x70(%rdx)
  mov %rcx, 0x78(%rdx)

  pop %r14
.cfi_adjust_cfa_offset -8
  pop %r13
.cfi_adjust_cfa_offset -8
  pop %r12
.cfi_adjust_cfa_offset -8
  ret
.cfi_endproc

___

# Performs schoolbook multiplication of 2 256-bit numbers. Uses
# MULX instruction. Result is stored in 256 bits pointed by $DST.
sub mul256_school {
  my ($idxM0,$M0,$idxM1,$M1,$idxDST,$DST,$T0,$T1,$T2,$T3,$T4,$T5,$T6,$T7,$T8,$T9)=@_;
  my ($ML0,$ML8,$ML16,$ML24)=map("$idxM0+$_(%$M0)",(0,8,16,24));
  my ($MR0,$MR8,$MR16,$MR24)=map("$idxM1+$_(%$M1)",(0,8,16,24));
  my ($D0,$D1,$D2,$D3,$D4,$D5,$D6,$D7)=map("$idxDST+$_(%$DST)",(0,8,16,24,32,40,48,56));

  $body=<<___;
  mov    $ML0, %rdx
  mulx   $MR0, %$T1, %$T0   # T0:T1 = A0*B0
  mov    %$T1, $D0          # DST0_final
  mulx   $MR8, %$T2, %$T1   # T1:T2 = A0*B1
  xor    %rax, %rax
  adox   %$T2, %$T0
  mulx   $MR16,%$T3, %$T2   # T2:T3 = A0*B2
  adox   %$T3, %$T1
  mulx   $MR24,%$T4, %$T3   # T3:T4 = A0*B3
  adox   %$T4, %$T2

  mov    $ML8, %rdx
  mulx   $MR0, %$T4, %$T5   # T5:T4 = A1*B0
  adox   %rax, %$T3
  xor    %rax, %rax
  mulx   $MR8, %$T7, %$T6   # T6:T7 = A1*B1
  adox   %$T0, %$T4
  mov    %$T4, $D1          # DST1_final
  adcx   %$T7, %$T5
  mulx   $MR16,%$T8, %$T7   # T7:T8 = A1*B2
  adcx   %$T8, %$T6
  adox   %$T1, %$T5
  mulx   $MR24,%$T9, %$T8   # T8:T9 = A1*B3
  adcx   %$T9, %$T7
  adcx   %rax, %$T8
  adox   %$T2, %$T6

  mov    $ML16,%rdx
  mulx   $MR0, %$T0, %$T1   # T1:T0 = A2*B0
  adox   %$T3, %$T7
  adox   %rax, %$T8
  xor    %rax, %rax
  mulx   $MR8, %$T3, %$T2   # T2:T3 = A2*B1
  adox   %$T5, %$T0
  mov    %$T0, $D2          # DST2_final
  adcx   %$T3, %$T1
  mulx   $MR16,%$T4, %$T3   # T3:T4 = A2*B2
  adcx   %$T4, %$T2
  adox   %$T6, %$T1
  mulx   $MR24,%$T9, %$T4   # T3:T4 = A2*B3
  adcx   %$T9, %$T3

  adcx   %rax, %$T4
  adox   %$T7, %$T2
  adox   %$T8, %$T3
  adox   %rax, %$T4

  mov    $ML24, %rdx
  mulx   $MR0,  %$T0, %$T5   # T5:T0 = A3*B0
  xor    %rax,  %rax
  mulx   $MR8,  %$T7, %$T6   # T6:T7 = A3*B1
  adcx   %$T7,  %$T5
  adox   %$T0,  %$T1
  mulx   $MR16, %$T8, %$T7   # T7:T8 = A3*B2
  adcx   %$T8,  %$T6
  adox   %$T5,  %$T2
  mulx   $MR24, %$T9, %$T8   # T8:T9 = A3*B3
  adcx   %$T9,  %$T7
  adcx   %rax,  %$T8
  adox   %$T6,  %$T3
  adox   %$T7,  %$T4
  adox   %rax,  %$T8
  mov    %$T1,  $D3          # DST3_final
  mov    %$T2,  $D4          # DST4_final
  mov    %$T3,  $D5          # DST5_final
  mov    %$T4,  $D6          # DST6_final
  mov    %$T8,  $D7          # DST7_final

___
  return $body;
}

# 503-bit multiplication using Karatsuba (one level),
# schoolbook (one level).
sub mul_mulx {
  # [rcx+64] <- (AH+AL) x (BH+BL)
  my $mul256_low=&mul256_school(0,"rsp",32,"rsp",64,"rcx",map("r$_",(8..15)),"rbx","rbp");
  # [rcx] <- AL x BL
  my $mul256_albl=&mul256_school(0,"rdi",0,"rsi",0,"rcx",map("r$_",(8..15)),"rbx","rbp");
  # [rsp] <- AH x BH
  my $mul256_ahbh=&mul256_school(32,"rdi",32,"rsi",0,"rsp",map("r$_",(8..15)),"rbx","rbp");

  $body=<<___;
  .Lmul_mulx:
  .cfi_startproc
    # sike_mpmul has already pushed r12--15 by this point.
  .cfi_adjust_cfa_offset 32
  .cfi_offset r12, -16
  .cfi_offset r13, -24
  .cfi_offset r14, -32
  .cfi_offset r15, -40

    mov %rdx, %rcx

    # r8-r11 <- AH + AL, rax <- mask
    xor %rax, %rax
    mov (%rdi), %r8
    mov 0x8(%rdi), %r9
    mov 0x10(%rdi), %r10
    mov 0x18(%rdi), %r11
    push %rbx

  .cfi_adjust_cfa_offset 8
  .cfi_offset rbx, -48
    push %rbp
  .cfi_offset rbp, -56
  .cfi_adjust_cfa_offset 8
    sub \$96, %rsp
  .cfi_adjust_cfa_offset 96
    add 0x20(%rdi), %r8
    adc 0x28(%rdi), %r9
    adc 0x30(%rdi), %r10
    adc 0x38(%rdi), %r11
    sbb \$0x0, %rax
    mov %r8, (%rsp)
    mov %r9, 0x8(%rsp)
    mov %r10, 0x10(%rsp)
    mov %r11, 0x18(%rsp)

    # r12-r15 <- BH + BL, rbx <- mask
    xor %rbx, %rbx
    mov (%rsi), %r12
    mov 0x8(%rsi), %r13
    mov 0x10(%rsi), %r14
    mov 0x18(%rsi), %r15
    add 0x20(%rsi), %r12
    adc 0x28(%rsi), %r13
    adc 0x30(%rsi), %r14
    adc 0x38(%rsi), %r15
    sbb \$0x0, %rbx
    mov %r12, 0x20(%rsp)
    mov %r13, 0x28(%rsp)
    mov %r14, 0x30(%rsp)
    mov %r15, 0x38(%rsp)

    # r12-r15 <- masked (BH + BL)
    and %rax, %r12
    and %rax, %r13
    and %rax, %r14
    and %rax, %r15

    # r8-r11 <- masked (AH + AL)
    and %rbx, %r8
    and %rbx, %r9
    and %rbx, %r10
    and %rbx, %r11

    # r8-r11 <- masked (AH + AL) + masked (AH + AL)
    add %r12, %r8
    adc %r13, %r9
    adc %r14, %r10
    adc %r15, %r11
    mov %r8, 0x40(%rsp)
    mov %r9, 0x48(%rsp)
    mov %r10, 0x50(%rsp)
    mov %r11, 0x58(%rsp)

    # [rcx+64] <- (AH+AL) x (BH+BL)
    $mul256_low
    # [rcx] <- AL x BL (Result c0-c3)
    $mul256_albl
    # [rsp] <- AH x BH
    $mul256_ahbh

    # r8-r11 <- (AH+AL) x (BH+BL), final step
    mov 0x40(%rsp), %r8
    mov 0x48(%rsp), %r9
    mov 0x50(%rsp), %r10
    mov 0x58(%rsp), %r11
    mov 0x60(%rcx), %rax
    add %rax, %r8
    mov 0x68(%rcx), %rax
    adc %rax, %r9
    mov 0x70(%rcx), %rax
    adc %rax, %r10
    mov 0x78(%rcx), %rax
    adc %rax, %r11

    # [rcx+64], x3-x5 <- (AH+AL) x (BH+BL) - ALxBL
    mov 0x40(%rcx), %r12
    mov 0x48(%rcx), %r13
    mov 0x50(%rcx), %r14
    mov 0x58(%rcx), %r15
    sub (%rcx), %r12
    sbb 0x8(%rcx), %r13
    sbb 0x10(%rcx), %r14
    sbb 0x18(%rcx), %r15
    sbb 0x20(%rcx), %r8
    sbb 0x28(%rcx), %r9
    sbb 0x30(%rcx), %r10
    sbb 0x38(%rcx), %r11

    # r8-r15 <- (AH+AL) x (BH+BL) - ALxBL - AHxBH
    sub (%rsp), %r12
    sbb 0x8(%rsp), %r13
    sbb 0x10(%rsp), %r14
    sbb 0x18(%rsp), %r15
    sbb 0x20(%rsp), %r8
    sbb 0x28(%rsp), %r9
    sbb 0x30(%rsp), %r10
    sbb 0x38(%rsp), %r11

    add 0x20(%rcx), %r12
    mov %r12, 0x20(%rcx)    # Result C4-C7
    adc 0x28(%rcx), %r13
    mov %r13, 0x28(%rcx)
    adc 0x30(%rcx), %r14
    mov %r14, 0x30(%rcx)
    adc 0x38(%rcx), %r15
    mov %r15, 0x38(%rcx)
    mov (%rsp), %rax
    adc %rax, %r8           # Result C8-C15
    mov %r8, 0x40(%rcx)
    mov 0x8(%rsp), %rax
    adc %rax, %r9
    mov %r9, 0x48(%rcx)
    mov 0x10(%rsp), %rax
    adc %rax, %r10
    mov %r10, 0x50(%rcx)
    mov 0x18(%rsp), %rax
    adc %rax, %r11
    mov %r11, 0x58(%rcx)
    mov 0x20(%rsp), %r12
    adc \$0x0, %r12
    mov %r12, 0x60(%rcx)
    mov 0x28(%rsp), %r13
    adc \$0x0, %r13
    mov %r13, 0x68(%rcx)
    mov 0x30(%rsp), %r14
    adc \$0x0, %r14
    mov %r14, 0x70(%rcx)
    mov 0x38(%rsp), %r15
    adc \$0x0, %r15
    mov %r15, 0x78(%rcx)

    add \$96, %rsp
  .cfi_adjust_cfa_offset -96
    pop %rbp
  .cfi_adjust_cfa_offset -8
  .cfi_same_value rbp
    pop %rbx
  .cfi_adjust_cfa_offset -8
  .cfi_same_value rbx
    pop %r15
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r15
    pop %r14
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r14
    pop %r13
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r13
    pop %r12
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r12
      ret
  .cfi_endproc

___
  return $body;
}

# Jump to alternative implemenatation provided as an
# argument in case CPU supports ADOX/ADCX and MULX instructions.
sub alt_impl {
  $jmp_func = shift;

  $body=<<___;
  lea OPENSSL_ia32cap_P(%rip), %rcx
  mov 8(%rcx), %rcx
  and \$0x80100, %ecx
  cmp \$0x80100, %ecx
  je  $jmp_func

___
  return $body
}

#  Integer multiplication based on Karatsuba method
#  Operation: c [rdx] = a [rdi] * b [rsi]
#  NOTE: a=c or b=c are not allowed
sub mul {
  my $jump_optim.=&alt_impl(".Lmul_mulx") if ($bmi2_adx);
  my $body.=&mul_mulx() if ($bmi2_adx);

  $body.=<<___;
  .globl  ${PREFIX}_mpmul
  .type   ${PREFIX}_mpmul,\@function,3
  ${PREFIX}_mpmul:
  .cfi_startproc
    push %r12
  .cfi_adjust_cfa_offset 8
  .cfi_offset r12, -16
    push %r13
  .cfi_adjust_cfa_offset 8
  .cfi_offset r13, -24
    push %r14
  .cfi_adjust_cfa_offset 8
  .cfi_offset r14, -32
    push %r15
  .cfi_adjust_cfa_offset 8
  .cfi_offset r15, -40

    $jump_optim

    mov %rdx, %rcx

    # rcx[0-3] <- AH+AL
    xor %rax, %rax
    mov 0x20(%rdi), %r8
    mov 0x28(%rdi), %r9
    mov 0x30(%rdi), %r10
    mov 0x38(%rdi), %r11
    add  0x0(%rdi), %r8
    adc  0x8(%rdi), %r9
    adc 0x10(%rdi), %r10
    adc 0x18(%rdi), %r11
    mov %r8,   0x0(%rcx)
    mov %r9,   0x8(%rcx)
    mov %r10, 0x10(%rcx)
    mov %r11, 0x18(%rcx)
    sbb  \$0,  %rax
    sub \$80,  %rsp           # Allocating space in stack
  .cfi_adjust_cfa_offset 80

    # r12-r15 <- BH+BL
    xor %rdx, %rdx
    mov 0x20(%rsi), %r12
    mov 0x28(%rsi), %r13
    mov 0x30(%rsi), %r14
    mov 0x38(%rsi), %r15
    add  0x0(%rsi), %r12
    adc  0x8(%rsi), %r13
    adc 0x10(%rsi), %r14
    adc 0x18(%rsi), %r15
    sbb \$0x0, %rdx
    mov %rax, 0x40(%rsp)
    mov %rdx, 0x48(%rsp)

    # (rsp[0-3],r8,r9,r10,r11) <- (AH+AL)*(BH+BL)
    mov (%rcx), %rax
    mul %r12
    mov %rax, (%rsp)            # c0
    mov %rdx, %r8

    xor %r9, %r9
    mov (%rcx), %rax
    mul %r13
    add %rax, %r8
    adc %rdx, %r9

    xor %r10, %r10
    mov 0x8(%rcx), %rax
    mul %r12
    add %rax, %r8
    mov %r8, 0x8(%rsp)          # c1
    adc %rdx, %r9
    adc \$0x0, %r10

    xor %r8, %r8
    mov (%rcx), %rax
    mul %r14
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x10(%rcx), %rax
    mul %r12
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x8(%rcx), %rax
    mul %r13
    add %rax, %r9
    mov %r9, 0x10(%rsp)         # c2
    adc %rdx, %r10
    adc \$0x0, %r8

    xor %r9, %r9
    mov (%rcx), %rax
    mul %r15
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x18(%rcx), %rax
    mul %r12
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x8(%rcx), %rax
    mul %r14
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x10(%rcx), %rax
    mul %r13
    add %rax, %r10
    mov %r10, 0x18(%rsp)        # c3
    adc %rdx, %r8
    adc \$0x0, %r9

    xor %r10, %r10
    mov 0x8(%rcx), %rax
    mul %r15
    add %rax, %r8
    adc %rdx, %r9
    adc \$0x0, %r10

    mov 0x18(%rcx), %rax
    mul %r13
    add %rax, %r8
    adc %rdx, %r9
    adc \$0x0, %r10

    mov 0x10(%rcx), %rax
    mul %r14
    add %rax, %r8
    mov %r8, 0x20(%rsp)          # c4
    adc %rdx, %r9
    adc \$0x0, %r10

    xor %r11, %r11
    mov 0x10(%rcx), %rax
    mul %r15
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r11

    mov 0x18(%rcx), %rax
    mul %r14
    add %rax, %r9               # c5
    adc %rdx, %r10
    adc \$0x0, %r11

    mov 0x18(%rcx), %rax
    mul %r15
    add %rax, %r10              # c6
    adc %rdx, %r11              # c7

    mov 0x40(%rsp), %rax
    and %rax, %r12
    and %rax, %r13
    and %rax, %r14
    and %rax, %r15
    add %r8, %r12
    adc %r9, %r13
    adc %r10, %r14
    adc %r11, %r15

    mov 0x48(%rsp), %rax
    mov (%rcx), %r8
    mov 0x8(%rcx), %r9
    mov 0x10(%rcx), %r10
    mov 0x18(%rcx), %r11
    and %rax, %r8
    and %rax, %r9
    and %rax, %r10
    and %rax, %r11
    add %r12, %r8
    adc %r13, %r9
    adc %r14, %r10
    adc %r15, %r11
    mov %r8, 0x20(%rsp)
    mov %r9, 0x28(%rsp)
    mov %r10, 0x30(%rsp)
    mov %r11, 0x38(%rsp)

    mov (%rdi), %r11
    mov (%rsi), %rax
    mul %r11
    xor %r9, %r9
    mov %rax, (%rcx)              # c0
    mov %rdx, %r8

    mov 0x10(%rdi), %r14
    mov 0x8(%rsi), %rax
    mul %r11
    xor %r10, %r10
    add %rax, %r8
    adc %rdx, %r9

    mov 0x8(%rdi), %r12
    mov (%rsi), %rax
    mul %r12
    add %rax, %r8
    mov %r8, 0x8(%rcx)            # c1
    adc %rdx, %r9
    adc \$0x0, %r10

    xor %r8, %r8
    mov 0x10(%rsi), %rax
    mul %r11
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov (%rsi), %r13
    mov %r14, %rax
    mul %r13
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x8(%rsi), %rax
    mul %r12
    add %rax, %r9
    mov %r9, 0x10(%rcx)           # c2
    adc %rdx, %r10
    adc \$0x0, %r8

    xor %r9, %r9
    mov 0x18(%rsi), %rax
    mul %r11
    mov 0x18(%rdi), %r15
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov %r15, %rax
    mul %r13
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x10(%rsi), %rax
    mul %r12
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x8(%rsi), %rax
    mul %r14
    add %rax, %r10
    mov %r10, 0x18(%rcx)           # c3
    adc %rdx, %r8
    adc \$0x0, %r9

    xor %r10, %r10
    mov 0x18(%rsi), %rax
    mul %r12
    add %rax, %r8
    adc %rdx, %r9
    adc \$0x0, %r10

    mov 0x8(%rsi), %rax
    mul %r15
    add %rax, %r8
    adc %rdx, %r9
    adc \$0x0, %r10

    mov 0x10(%rsi), %rax
    mul %r14
    add %rax, %r8
    mov %r8, 0x20(%rcx)           # c4
    adc %rdx, %r9
    adc \$0x0, %r10

    xor %r8, %r8
    mov 0x18(%rsi), %rax
    mul %r14
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x10(%rsi), %rax
    mul %r15
    add %rax, %r9
    mov %r9, 0x28(%rcx)           # c5
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x18(%rsi), %rax
    mul %r15
    add %rax, %r10
    mov %r10, 0x30(%rcx)          # c6
    adc %rdx, %r8
    mov %r8, 0x38(%rcx)           # c7

    # rcx[8-15] <- AH*BH
    mov 0x20(%rdi), %r11
    mov 0x20(%rsi), %rax
    mul %r11
    xor %r9, %r9
    mov %rax, 0x40(%rcx)          # c0
    mov %rdx, %r8

    mov 0x30(%rdi), %r14
    mov 0x28(%rsi), %rax
    mul %r11
    xor %r10, %r10
    add %rax, %r8
    adc %rdx, %r9

    mov 0x28(%rdi), %r12
    mov 0x20(%rsi), %rax
    mul %r12
    add %rax, %r8
    mov %r8, 0x48(%rcx)           # c1
    adc %rdx, %r9
    adc \$0x0, %r10

    xor %r8, %r8
    mov 0x30(%rsi), %rax
    mul %r11
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x20(%rsi), %r13
    mov %r14, %rax
    mul %r13
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x28(%rsi), %rax
    mul %r12
    add %rax, %r9
    mov %r9, 0x50(%rcx)             # c2
    adc %rdx, %r10
    adc \$0x0, %r8

    xor %r9, %r9
    mov 0x38(%rsi), %rax
    mul %r11
    mov 0x38(%rdi), %r15
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov %r15, %rax
    mul %r13
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x30(%rsi), %rax
    mul %r12
    add %rax, %r10
    adc %rdx, %r8
    adc \$0x0, %r9

    mov 0x28(%rsi), %rax
    mul %r14
    add %rax, %r10
    mov %r10, 0x58(%rcx)            # c3
    adc %rdx, %r8
    adc \$0x0, %r9

    xor %r10, %r10
    mov 0x38(%rsi), %rax
    mul %r12
    add %rax, %r8
    adc %rdx, %r9
    adc \$0x0, %r10

    mov 0x28(%rsi), %rax
    mul %r15
    add %rax, %r8
    adc %rdx, %r9
    adc \$0x0, %r10

    mov 0x30(%rsi), %rax
    mul %r14
    add %rax, %r8
    mov %r8, 0x60(%rcx)             # c4
    adc %rdx, %r9
    adc \$0x0, %r10

    xor %r8, %r8
    mov 0x38(%rsi), %rax
    mul %r14
    add %rax, %r9
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x30(%rsi), %rax
    mul %r15
    add %rax, %r9
    mov %r9, 0x68(%rcx)             # c5
    adc %rdx, %r10
    adc \$0x0, %r8

    mov 0x38(%rsi), %rax
    mul %r15
    add %rax, %r10
    mov %r10, 0x70(%rcx)            # c6
    adc %rdx, %r8
    mov %r8, 0x78(%rcx)             # c7

    # [r8-r15] <- (AH+AL)*(BH+BL) - AL*BL
    mov  0x0(%rsp), %r8
    sub  0x0(%rcx), %r8
    mov  0x8(%rsp), %r9
    sbb  0x8(%rcx), %r9
    mov 0x10(%rsp), %r10
    sbb 0x10(%rcx), %r10
    mov 0x18(%rsp), %r11
    sbb 0x18(%rcx), %r11
    mov 0x20(%rsp), %r12
    sbb 0x20(%rcx), %r12
    mov 0x28(%rsp), %r13
    sbb 0x28(%rcx), %r13
    mov 0x30(%rsp), %r14
    sbb 0x30(%rcx), %r14
    mov 0x38(%rsp), %r15
    sbb 0x38(%rcx), %r15

    # [r8-r15] <- (AH+AL)*(BH+BL) - AL*BL - AH*BH
    mov 0x40(%rcx), %rax
    sub %rax, %r8
    mov 0x48(%rcx), %rax
    sbb %rax, %r9
    mov 0x50(%rcx), %rax
    sbb %rax, %r10
    mov 0x58(%rcx), %rax
    sbb %rax, %r11
    mov 0x60(%rcx), %rax
    sbb %rax, %r12
    mov 0x68(%rcx), %rdx
    sbb %rdx, %r13
    mov 0x70(%rcx), %rdi
    sbb %rdi, %r14
    mov 0x78(%rcx), %rsi
    sbb %rsi, %r15

    # Final result
    add 0x20(%rcx),  %r8
    mov %r8,  0x20(%rcx)
    adc 0x28(%rcx),  %r9
    mov %r9,  0x28(%rcx)
    adc 0x30(%rcx), %r10
    mov %r10, 0x30(%rcx)
    adc 0x38(%rcx), %r11
    mov %r11, 0x38(%rcx)
    adc 0x40(%rcx), %r12
    mov %r12, 0x40(%rcx)
    adc 0x48(%rcx), %r13
    mov %r13, 0x48(%rcx)
    adc 0x50(%rcx), %r14
    mov %r14, 0x50(%rcx)
    adc 0x58(%rcx), %r15
    mov %r15, 0x58(%rcx)
    adc \$0x0, %rax
    mov %rax, 0x60(%rcx)
    adc \$0x0, %rdx
    mov %rdx, 0x68(%rcx)
    adc \$0x0, %rdi
    mov %rdi, 0x70(%rcx)
    adc \$0x0, %rsi
    mov %rsi, 0x78(%rcx)

    add \$80, %rsp           # Restoring space in stack
  .cfi_adjust_cfa_offset -80
    pop %r15
  .cfi_adjust_cfa_offset -8
    pop %r14
  .cfi_adjust_cfa_offset -8
    pop %r13
  .cfi_adjust_cfa_offset -8
    pop %r12
  .cfi_adjust_cfa_offset -8
    ret
  .cfi_endproc

___
  return $body;
}

$code.=&mul();

#  Optimized Montgomery reduction for CPUs with ADOX/ADCX and MULX
#  Based on method described in Faz-Hernandez et al. https://eprint.iacr.org/2017/1015
#  Operation: c [rsi] = a [rdi]
#  NOTE: a=c is not allowed
sub rdc_mulx {
  # a[0-1] x .Lp503p1_nz --> result: r8:r14
  my $mul01=&mul128x320_school(0,"rdi",".Lp503p1_nz(%rip)",map("r$_",(8..14)),"rbx","rcx","r15");
  # a[2-3] x .Lp503p1_nz --> result: r8:r14
  my $mul23=&mul128x320_school(16,"rdi",".Lp503p1_nz(%rip)",map("r$_",(8..14)),"rbx","rcx","r15");
  # a[4-5] x .Lp503p1_nz --> result: r8:r14
  my $mul45=&mul128x320_school(32,"rdi",".Lp503p1_nz(%rip)",map("r$_",(8..14)),"rbx","rcx","r15");
  # a[6-7] x .Lp503p1_nz --> result: r8:r14
  my $mul67=&mul128x320_school(48,"rdi",".Lp503p1_nz(%rip)",map("r$_", (8..14)),"rbx","rcx","r15");

  my $body=<<___;
    .Lrdc_mulx_asm:
    .cfi_startproc
      # sike_fprdc has already pushed r12--15 and rbx by this point.
    .cfi_adjust_cfa_offset 32
    .cfi_offset r12, -16
    .cfi_offset r13, -24
    .cfi_offset r14, -32
    .cfi_offset r15, -40
    .cfi_offset rbx, -48
    .cfi_adjust_cfa_offset 8

    $mul01

    xor %r15, %r15
    add 0x18(%rdi), %r8
    adc 0x20(%rdi), %r9
    adc 0x28(%rdi), %r10
    adc 0x30(%rdi), %r11
    adc 0x38(%rdi), %r12
    adc 0x40(%rdi), %r13
    adc 0x48(%rdi), %r14
    adc 0x50(%rdi), %r15
    mov %r8, 0x18(%rdi)
    mov %r9, 0x20(%rdi)
    mov %r10, 0x28(%rdi)
    mov %r11, 0x30(%rdi)
    mov %r12, 0x38(%rdi)
    mov %r13, 0x40(%rdi)
    mov %r14, 0x48(%rdi)
    mov %r15, 0x50(%rdi)
    mov 0x58(%rdi), %r8
    mov 0x60(%rdi), %r9
    mov 0x68(%rdi), %r10
    mov 0x70(%rdi), %r11
    mov 0x78(%rdi), %r12
    adc \$0x0, %r8
    adc \$0x0, %r9
    adc \$0x0, %r10
    adc \$0x0, %r11
    adc \$0x0, %r12
    mov %r8, 0x58(%rdi)
    mov %r9, 0x60(%rdi)
    mov %r10, 0x68(%rdi)
    mov %r11, 0x70(%rdi)
    mov %r12, 0x78(%rdi)

    $mul23

    xor %r15, %r15
    add 0x28(%rdi), %r8
    adc 0x30(%rdi), %r9
    adc 0x38(%rdi), %r10
    adc 0x40(%rdi), %r11
    adc 0x48(%rdi), %r12
    adc 0x50(%rdi), %r13
    adc 0x58(%rdi), %r14
    adc 0x60(%rdi), %r15
    mov %r8, 0x28(%rdi)
    mov %r9, 0x30(%rdi)
    mov %r10, 0x38(%rdi)
    mov %r11, 0x40(%rdi)
    mov %r12, 0x48(%rdi)
    mov %r13, 0x50(%rdi)
    mov %r14, 0x58(%rdi)
    mov %r15, 0x60(%rdi)
    mov 0x68(%rdi), %r8
    mov 0x70(%rdi), %r9
    mov 0x78(%rdi), %r10
    adc \$0x0, %r8
    adc \$0x0, %r9
    adc \$0x0, %r10
    mov %r8, 0x68(%rdi)
    mov %r9, 0x70(%rdi)
    mov %r10, 0x78(%rdi)

    $mul45

    xor %r15, %r15
    xor %rbx, %rbx
    add 0x38(%rdi), %r8
    adc 0x40(%rdi), %r9
    adc 0x48(%rdi), %r10
    adc 0x50(%rdi), %r11
    adc 0x58(%rdi), %r12
    adc 0x60(%rdi), %r13
    adc 0x68(%rdi), %r14
    adc 0x70(%rdi), %r15
    adc 0x78(%rdi), %rbx
    mov %r8, 0x38(%rdi)
    mov %r9, (%rsi)         # Final result c0
    mov %r10, 0x48(%rdi)
    mov %r11, 0x50(%rdi)
    mov %r12, 0x58(%rdi)
    mov %r13, 0x60(%rdi)
    mov %r14, 0x68(%rdi)
    mov %r15, 0x70(%rdi)
    mov %rbx, 0x78(%rdi)

    $mul67

    add 0x48(%rdi), %r8
    adc 0x50(%rdi), %r9
    adc 0x58(%rdi), %r10
    adc 0x60(%rdi), %r11
    adc 0x68(%rdi), %r12
    adc 0x70(%rdi), %r13
    adc 0x78(%rdi), %r14
    mov %r8, 0x8(%rsi)
    mov %r9, 0x10(%rsi)
    mov %r10, 0x18(%rsi)
    mov %r11, 0x20(%rsi)
    mov %r12, 0x28(%rsi)
    mov %r13, 0x30(%rsi)
    mov %r14, 0x38(%rsi)

    pop %rbx
  .cfi_adjust_cfa_offset -8
  .cfi_same_value rbx
    pop %r15
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r15
    pop %r14
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r14
    pop %r13
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r13
    pop %r12
  .cfi_adjust_cfa_offset -8
  .cfi_same_value r12
    ret
  .cfi_endproc
___
  return $body;
}

#  Montgomery reduction
#  Based on comba method
#  Operation: c [rsi] = a [rdi]
#  NOTE: a=c is not allowed
sub rdc {
  my $jump_optim=&alt_impl(".Lrdc_mulx_asm") if ($bmi2_adx);
  my $body=&rdc_mulx() if ($bmi2_adx);

  $body.=<<___;
    .globl  ${PREFIX}_fprdc
    .type   ${PREFIX}_fprdc,\@function,3
    ${PREFIX}_fprdc:
    .cfi_startproc
      push %r12
    .cfi_adjust_cfa_offset  8
    .cfi_offset r12, -16
      push %r13
    .cfi_adjust_cfa_offset  8
    .cfi_offset r13, -24
      push %r14
    .cfi_adjust_cfa_offset  8
    .cfi_offset r14, -32
      push %r15
    .cfi_adjust_cfa_offset  8
    .cfi_offset r15, -40
      push %rbx
    .cfi_adjust_cfa_offset  8
    .cfi_offset rbx, -48

    $jump_optim

    # Reduction, generic x86 implementation
    lea .Lp503p1(%rip), %rbx

    mov (%rdi), %r11
    mov (%rbx), %rax
    mul %r11
    xor %r8, %r8
    add 0x18(%rdi), %rax
    mov %rax, 0x18(%rsi)  # z3
    adc %rdx, %r8

    xor %r9, %r9
    mov 0x8(%rbx), %rax
    mul %r11
    xor %r10, %r10
    add %rax, %r8
    adc %rdx, %r9

    mov 0x8(%rdi), %r12
    mov (%rbx), %rax
    mul %r12
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10
    add 0x20(%rdi), %r8
    mov %r8, 0x20(%rsi)   # z4
    adc  \$0, %r9
    adc  \$0, %r10

    xor %r8, %r8
    mov 0x10(%rbx), %rax
    mul %r11
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 8(%rbx), %rax
    mul %r12
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x10(%rdi), %r13
    mov (%rbx), %rax
    mul %r13
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8
    add 0x28(%rdi), %r9
    mov %r9, 0x28(%rsi)   # z5
    adc  \$0, %r10
    adc  \$0, %r8

    xor %r9, %r9
    mov 0x18(%rbx), %rax
    mul %r11
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x10(%rbx), %rax
    mul %r12
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x8(%rbx), %rax
    mul %r13
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x18(%rsi), %r14
    mov (%rbx), %rax
    mul %r14
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9
    add 0x30(%rdi), %r10
    mov %r10, 0x30(%rsi)    # z6
    adc  \$0, %r8
    adc  \$0, %r9

    xor %r10, %r10
    mov 0x20(%rbx), %rax
    mul %r11
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x18(%rbx), %rax
    mul %r12
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x10(%rbx), %rax
    mul %r13
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x8(%rbx), %rax
    mul %r14
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x20(%rsi), %r15
    mov (%rbx), %rax
    mul %r15
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10
    add 0x38(%rdi), %r8     # Z7
    mov %r8, 0x38(%rsi)
    adc  \$0, %r9
    adc  \$0, %r10

    xor %r8, %r8
    mov 0x20(%rbx), %rax
    mul %r12
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x18(%rbx), %rax
    mul %r13
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x10(%rbx), %rax
    mul %r14
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x8(%rbx), %rax
    mul %r15
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x28(%rsi), %rcx
    mov (%rbx), %rax
    mul %rcx
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8
    add 0x40(%rdi), %r9
    mov %r9, (%rsi)       # Z9
    adc  \$0, %r10
    adc  \$0, %r8

    xor %r9, %r9
    mov 0x20(%rbx), %rax
    mul %r13
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x18(%rbx), %rax
    mul %r14
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x10(%rbx), %rax
    mul %r15
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 8(%rbx), %rax
    mul %rcx
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x30(%rsi), %r13
    mov (%rbx), %rax
    mul %r13
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9
    add 0x48(%rdi), %r10
    mov %r10, 0x8(%rsi)     # Z1
    adc  \$0, %r8
    adc  \$0, %r9

    xor %r10, %r10
    mov 0x20(%rbx), %rax
    mul %r14
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x18(%rbx), %rax
    mul %r15
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x10(%rbx), %rax
    mul %rcx
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 8(%rbx), %rax
    mul %r13
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x38(%rsi), %r14
    mov (%rbx), %rax
    mul %r14
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10
    add 0x50(%rdi), %r8
    mov %r8, 0x10(%rsi)     # Z2
    adc  \$0, %r9
    adc  \$0, %r10

    xor %r8, %r8
    mov 0x20(%rbx), %rax
    mul %r15
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x18(%rbx), %rax
    mul %rcx
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 0x10(%rbx), %rax
    mul %r13
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8

    mov 8(%rbx), %rax
    mul %r14
    add %rax, %r9
    adc %rdx, %r10
    adc  \$0, %r8
    add 0x58(%rdi), %r9
    mov %r9, 0x18(%rsi)     # Z3
    adc  \$0, %r10
    adc  \$0, %r8

    xor %r9, %r9
    mov 0x20(%rbx), %rax
    mul %rcx
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x18(%rbx), %rax
    mul %r13
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9

    mov 0x10(%rbx), %rax
    mul %r14
    add %rax, %r10
    adc %rdx, %r8
    adc  \$0, %r9
    add 0x60(%rdi), %r10
    mov %r10, 0x20(%rsi)    # Z4
    adc  \$0, %r8
    adc  \$0, %r9

    xor %r10, %r10
    mov 0x20(%rbx), %rax
    mul %r13
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10

    mov 0x18(%rbx), %rax
    mul %r14
    add %rax, %r8
    adc %rdx, %r9
    adc  \$0, %r10
    add 0x68(%rdi), %r8     # Z5
    mov %r8, 0x28(%rsi)     # Z5
    adc  \$0, %r9
    adc  \$0, %r10

    mov 0x20(%rbx), %rax
    mul %r14
    add %rax, %r9
    adc %rdx, %r10
    add 0x70(%rdi), %r9     # Z6
    mov %r9, 0x30(%rsi)     # Z6
    adc  \$0, %r10
    add 0x78(%rdi), %r10    # Z7
    mov %r10, 0x38(%rsi)    # Z7

    pop %rbx
  .cfi_adjust_cfa_offset -8
    pop %r15
  .cfi_adjust_cfa_offset -8
    pop %r14
  .cfi_adjust_cfa_offset -8
    pop %r13
  .cfi_adjust_cfa_offset -8
    pop %r12
  .cfi_adjust_cfa_offset -8
    ret
  .cfi_endproc
___
  return $body;
}

$code.=&rdc();

foreach (split("\n",$code)) {
  s/\`([^\`]*)\`/eval($1)/ge;
  print $_,"\n";
}

close STDOUT;
