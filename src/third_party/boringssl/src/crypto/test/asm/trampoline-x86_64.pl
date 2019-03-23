#!/usr/bin/env perl
# Copyright (c) 2018, Google Inc.
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

# This file defines helper functions for crypto/test/abi_test.h on x86_64. See
# that header for details on how to use this.
#
# For convenience, this file is linked into libcrypto, where consuming builds
# already support architecture-specific sources. The static linker should drop
# this code in non-test binaries. This includes a shared library build of
# libcrypto, provided --gc-sections (ELF), -dead_strip (Mac), or equivalent is
# used.
#
# References:
#
# SysV ABI: https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf
# Win64 ABI: https://docs.microsoft.com/en-us/cpp/build/x64-software-conventions?view=vs-2017

use strict;

my $flavour = shift;
my $output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

my $win64 = 0;
$win64 = 1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/;
my $dir = $1;
my $xlate;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT, "| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT = *OUT;

# @inp is the registers used for function inputs, in order.
my @inp = $win64 ? ("%rcx", "%rdx", "%r8", "%r9") :
                   ("%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9");

# @caller_state is the list of registers that the callee must preserve for the
# caller. This must match the definition of CallerState in abi_test.h.
my @caller_state = ("%rbx", "%rbp", "%r12", "%r13", "%r14", "%r15");
if ($win64) {
  @caller_state = ("%rbx", "%rbp", "%rdi", "%rsi", "%r12", "%r13", "%r14",
                   "%r15", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10",
                   "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15");
}

# $caller_state_size is the size of CallerState, in bytes.
my $caller_state_size = 0;
foreach (@caller_state) {
  if (/^%r/) {
    $caller_state_size += 8;
  } elsif (/^%xmm/) {
    $caller_state_size += 16;
  } else {
    die "unknown register $_";
  }
}

# load_caller_state returns code which loads a CallerState structure at
# $off($reg) into the respective registers. No other registers are touched, but
# $reg may not be a register in CallerState. $cb is an optional callback to
# add extra lines after each movq or movdqa. $cb is passed the offset, relative
# to $reg, and name of each register.
sub load_caller_state {
  my ($off, $reg, $cb) = @_;
  my $ret = "";
  foreach (@caller_state) {
    my $old_off = $off;
    if (/^%r/) {
      $ret .= "\tmovq\t$off($reg), $_\n";
      $off += 8;
    } elsif (/^%xmm/) {
      $ret .= "\tmovdqa\t$off($reg), $_\n";
      $off += 16;
    } else {
      die "unknown register $_";
    }
    $ret .= $cb->($old_off, $_) if (defined($cb));
  }
  return $ret;
}

# store_caller_state behaves like load_caller_state, except that it writes the
# current values of the registers into $off($reg).
sub store_caller_state {
  my ($off, $reg, $cb) = @_;
  my $ret = "";
  foreach (@caller_state) {
    my $old_off = $off;
    if (/^%r/) {
      $ret .= "\tmovq\t$_, $off($reg)\n";
      $off += 8;
    } elsif (/^%xmm/) {
      $ret .= "\tmovdqa\t$_, $off($reg)\n";
      $off += 16;
    } else {
      die "unknown register $_";
    }
    $ret .= $cb->($old_off, $_) if (defined($cb));
  }
  return $ret;
}

# $max_params is the maximum number of parameters abi_test_trampoline supports.
my $max_params = 10;

# Windows reserves stack space for the register-based parameters, while SysV
# only reserves space for the overflow ones.
my $stack_params_skip = $win64 ? scalar(@inp) : 0;
my $num_stack_params = $win64 ? $max_params : $max_params - scalar(@inp);

my ($func, $state, $argv, $argc) = @inp;
my $code = <<____;
.text

# abi_test_trampoline loads callee-saved registers from |state|, calls |func|
# with |argv|, then saves the callee-saved registers into |state|. It returns
# the result of |func|.
# uint64_t abi_test_trampoline(void (*func)(...), CallerState *state,
#                              const uint64_t *argv, size_t argc);
.type	abi_test_trampoline, \@abi-omnipotent
.globl	abi_test_trampoline
.align	16
abi_test_trampoline:
.Labi_test_trampoline_begin:
.cfi_startproc
	# Stack layout:
	#   8 bytes - align
	#   $caller_state_size bytes - saved caller registers
	#   8 bytes - scratch space
	#   8 bytes - saved copy of \$state
	#   8 bytes - saved copy of \$func
	#   8 bytes - if needed for stack alignment
	#   8*$num_stack_params bytes - parameters for \$func
____
my $stack_alloc_size = 8 + $caller_state_size + 8*3 + 8*$num_stack_params;
# SysV and Windows both require the stack to be 16-byte-aligned. The call
# instruction offsets it by 8, so stack allocations must be 8 mod 16.
if ($stack_alloc_size % 16 != 8) {
  $num_stack_params++;
  $stack_alloc_size += 8;
}
my $stack_params_offset = 8 * $stack_params_skip;
my $func_offset = 8 * $num_stack_params;
my $state_offset = $func_offset + 8;
my $scratch_offset = $state_offset + 8;
my $caller_state_offset = $scratch_offset + 8;
$code .= <<____;
	subq	\$$stack_alloc_size, %rsp
.cfi_adjust_cfa_offset	$stack_alloc_size
.Labi_test_trampoline_prolog_alloc:
____
# Store our caller's state. This is needed because we modify it ourselves, and
# also to isolate the test infrastruction from the function under test failing
# to save some register.
my %reg_offsets;
$code .= store_caller_state($caller_state_offset, "%rsp", sub {
  my ($off, $reg) = @_;
  $reg = substr($reg, 1);
  $reg_offsets{$reg} = $off;
  $off -= $stack_alloc_size + 8;
  return <<____;
.cfi_offset	$reg, $off
.Labi_test_trampoline_prolog_$reg:
____
});
$code .= <<____;
.Labi_test_trampoline_prolog_end:
____

$code .= load_caller_state(0, $state);
$code .= <<____;
	# Stash \$func and \$state, so they are available after the call returns.
	movq	$func, $func_offset(%rsp)
	movq	$state, $state_offset(%rsp)

	# Load parameters. Note this will clobber \$argv and \$argc, so we can
	# only use non-parameter volatile registers. There are three, and they
	# are the same between SysV and Win64: %rax, %r10, and %r11.
	movq	$argv, %r10
	movq	$argc, %r11
____
foreach (@inp) {
	$code .= <<____;
	dec	%r11
	js	.Lcall
	movq	(%r10), $_
	addq	\$8, %r10
____
}
$code .= <<____;
	leaq	$stack_params_offset(%rsp), %rax
.Largs_loop:
	dec	%r11
	js	.Lcall

  # This block should be:
  #    movq (%r10), %rtmp
  #    movq %rtmp, (%rax)
  # There are no spare registers available, so we spill into the scratch space.
	movq	%r11, $scratch_offset(%rsp)
	movq	(%r10), %r11
	movq	%r11, (%rax)
	movq	$scratch_offset(%rsp), %r11

	addq	\$8, %r10
	addq	\$8, %rax
	jmp	.Largs_loop

.Lcall:
	movq	$func_offset(%rsp), %rax
	call	*%rax

	# Store what \$func did our state, so our caller can check.
  movq  $state_offset(%rsp), $state
____
$code .= store_caller_state(0, $state);

# Restore our caller's state.
$code .= load_caller_state($caller_state_offset, "%rsp", sub {
  my ($off, $reg) = @_;
  $reg = substr($reg, 1);
  return ".cfi_restore\t$reg\n";
});
$code .= <<____;
	addq	\$$stack_alloc_size, %rsp
.cfi_adjust_cfa_offset	-$stack_alloc_size

  # %rax already contains \$func's return value, unmodified.
	ret
.cfi_endproc
.Labi_test_trampoline_end:
.size	abi_test_trampoline,.-abi_test_trampoline
____

# abi_test_clobber_* zeros the corresponding register. These are used to test
# the ABI-testing framework.
foreach ("ax", "bx", "cx", "dx", "di", "si", "bp", 8..15) {
  $code .= <<____;
.type	abi_test_clobber_r$_, \@abi-omnipotent
.globl	abi_test_clobber_r$_
.align	16
abi_test_clobber_r$_:
	xorq	%r$_, %r$_
	ret
.size	abi_test_clobber_r$_,.-abi_test_clobber_r$_
____
}

foreach (0..15) {
  $code .= <<____;
.type	abi_test_clobber_xmm$_, \@abi-omnipotent
.globl	abi_test_clobber_xmm$_
.align	16
abi_test_clobber_xmm$_:
	pxor	%xmm$_, %xmm$_
	ret
.size	abi_test_clobber_xmm$_,.-abi_test_clobber_xmm$_
____
}

if ($win64) {
  # Add unwind metadata for SEH.
  #
  # TODO(davidben): This is all manual right now. Once we've added SEH tests,
  # add support for emitting these in x86_64-xlate.pl, probably based on MASM
  # and Yasm's unwind directives, and unify with CFI. (Sadly, NASM does not
  # support these directives.) Then push that upstream to replace the
  # error-prone and non-standard custom handlers.

  # See https://docs.microsoft.com/en-us/cpp/build/struct-unwind-code?view=vs-2017
  my $UWOP_ALLOC_LARGE = 1;
  my $UWOP_ALLOC_SMALL = 2;
  my $UWOP_SAVE_NONVOL = 4;
  my $UWOP_SAVE_XMM128 = 8;

  my %UWOP_REG_NUMBER = (rax => 0, rcx => 1, rdx => 2, rbx => 3, rsp => 4,
                         rbp => 5, rsi => 6, rdi => 7,
                         map(("r$_" => $_), (8..15)));

  my $unwind_codes = "";
  my $num_slots = 0;
  if ($stack_alloc_size <= 128) {
    my $info = $UWOP_ALLOC_SMALL | ((($stack_alloc_size - 8) / 8) << 4);
    $unwind_codes .= <<____;
	.byte	.Labi_test_trampoline_prolog_alloc-.Labi_test_trampoline_begin
	.byte	$info
____
    $num_slots++;
  } else {
    die "stack allocation needs three unwind slots" if ($stack_alloc_size > 512 * 1024 + 8);
    my $info = $UWOP_ALLOC_LARGE;
    my $value = $stack_alloc_size / 8;
    $unwind_codes .= <<____;
	.byte	.Labi_test_trampoline_prolog_alloc-.Labi_test_trampoline_begin
	.byte	$info
	.value	$value
____
    $num_slots += 2;
  }

  foreach my $reg (@caller_state) {
    $reg = substr($reg, 1);
    die "unknown register $reg" unless exists($reg_offsets{$reg});
    if ($reg =~ /^r/) {
      die "unknown register $reg" unless exists($UWOP_REG_NUMBER{$reg});
      my $info = $UWOP_SAVE_NONVOL | ($UWOP_REG_NUMBER{$reg} << 4);
      my $value = $reg_offsets{$reg} / 8;
      $unwind_codes .= <<____;
	.byte	.Labi_test_trampoline_prolog_$reg-.Labi_test_trampoline_begin
	.byte	$info
	.value	$value
____
      $num_slots += 2;
    } elsif ($reg =~ /^xmm/) {
      my $info = $UWOP_SAVE_XMM128 | (substr($reg, 3) << 4);
      my $value = $reg_offsets{$reg} / 16;
      $unwind_codes .= <<____;
	.byte	.Labi_test_trampoline_prolog_$reg-.Labi_test_trampoline_begin
	.byte	$info
	.value	$value
____
      $num_slots += 2;
    } else {
      die "unknown register $reg";
    }
  }

  $code .= <<____;
.section	.pdata
.align	4
	# https://docs.microsoft.com/en-us/cpp/build/struct-runtime-function?view=vs-2017
	.rva	.Labi_test_trampoline_begin
	.rva	.Labi_test_trampoline_end
	.rva	.Labi_test_trampoline_info

.section	.xdata
.align	8
.Labi_test_trampoline_info:
	# https://docs.microsoft.com/en-us/cpp/build/struct-unwind-info?view=vs-2017
	.byte	1	# version 1, no flags
	.byte	.Labi_test_trampoline_prolog_end-.Labi_test_trampoline_begin
	.byte	$num_slots
	.byte	0	# no frame register
$unwind_codes
____
}

print $code;
close STDOUT;
