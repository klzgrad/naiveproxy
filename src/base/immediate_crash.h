// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_IMMEDIATE_CRASH_H_
#define BASE_IMMEDIATE_CRASH_H_

#include "build/build_config.h"

// Crashes in the fastest possible way with no attempt at logging.
// There are different constraints to satisfy here, see http://crbug.com/664209
// for more context:
// - The trap instructions, and hence the PC value at crash time, have to be
//   distinct and not get folded into the same opcode by the compiler.
//   On Linux/Android this is tricky because GCC still folds identical
//   asm volatile blocks. The workaround is generating distinct opcodes for
//   each CHECK using the __COUNTER__ macro.
// - The debug info for the trap instruction has to be attributed to the source
//   line that has the CHECK(), to make crash reports actionable. This rules
//   out the ability of using a inline function, at least as long as clang
//   doesn't support attribute(artificial).
// - Failed CHECKs should produce a signal that is distinguishable from an
//   invalid memory access, to improve the actionability of crash reports.
// - The compiler should treat the CHECK as no-return instructions, so that the
//   trap code can be efficiently packed in the prologue of the function and
//   doesn't interfere with the main execution flow.
// - When debugging, developers shouldn't be able to accidentally step over a
//   CHECK. This is achieved by putting opcodes that will cause a non
//   continuable exception after the actual trap instruction.
// - Don't cause too much binary bloat.
#if defined(COMPILER_GCC)

#if defined(ARCH_CPU_X86_FAMILY) && !defined(OS_NACL)
// int 3 will generate a SIGTRAP.
#define TRAP_SEQUENCE() \
  asm volatile(         \
      "int3; ud2; push %0;" ::"i"(static_cast<unsigned char>(__COUNTER__)))

#elif defined(ARCH_CPU_ARMEL) && !defined(OS_NACL)
// bkpt will generate a SIGBUS when running on armv7 and a SIGTRAP when running
// as a 32 bit userspace app on arm64. There doesn't seem to be any way to
// cause a SIGTRAP from userspace without using a syscall (which would be a
// problem for sandboxing).
#define TRAP_SEQUENCE() \
  asm volatile("bkpt #0; udf %0;" ::"i"(__COUNTER__ % 256))

#elif defined(ARCH_CPU_ARM64) && !defined(OS_NACL)
// This will always generate a SIGTRAP on arm64.
#define TRAP_SEQUENCE() \
  asm volatile("brk #0; hlt %0;" ::"i"(__COUNTER__ % 65536))

#else
// Crash report accuracy will not be guaranteed on other architectures, but at
// least this will crash as expected.
#define TRAP_SEQUENCE() __builtin_trap()
#endif  // ARCH_CPU_*

#elif defined(COMPILER_MSVC)

// Clang is cleverer about coalescing int3s, so we need to add a unique-ish
// instruction following the __debugbreak() to have it emit distinct locations
// for CHECKs rather than collapsing them all together. It would be nice to use
// a short intrinsic to do this (and perhaps have only one implementation for
// both clang and MSVC), however clang-cl currently does not support intrinsics.
// On the flip side, MSVC x64 doesn't support inline asm. So, we have to have
// two implementations. Normally clang-cl's version will be 5 bytes (1 for
// `int3`, 2 for `ud2`, 2 for `push byte imm`, however, TODO(scottmg):
// https://crbug.com/694670 clang-cl doesn't currently support %'ing
// __COUNTER__, so eventually it will emit the dword form of push.
// TODO(scottmg): Reinvestigate a short sequence that will work on both
// compilers once clang supports more intrinsics. See https://crbug.com/693713.
#if !defined(__clang__)
#define TRAP_SEQUENCE() __debugbreak()
#elif defined(ARCH_CPU_ARM64)
#define TRAP_SEQUENCE() \
  __asm volatile("brk #0\n hlt %0\n" ::"i"(__COUNTER__ % 65536));
#else
#define TRAP_SEQUENCE() ({ {__asm int 3 __asm ud2 __asm push __COUNTER__}; })
#endif  // __clang__

#else
#error Port
#endif  // COMPILER_GCC

// CHECK() and the trap sequence can be invoked from a constexpr function.
// This could make compilation fail on GCC, as it forbids directly using inline
// asm inside a constexpr function. However, it allows calling a lambda
// expression including the same asm.
// The side effect is that the top of the stacktrace will not point to the
// calling function, but to this anonymous lambda. This is still useful as the
// full name of the lambda will typically include the name of the function that
// calls CHECK() and the debugger will still break at the right line of code.
#if !defined(COMPILER_GCC)
#define WRAPPED_TRAP_SEQUENCE() TRAP_SEQUENCE()
#else
#define WRAPPED_TRAP_SEQUENCE() \
  do {                          \
    [] { TRAP_SEQUENCE(); }();  \
  } while (false)
#endif

#if defined(__clang__) || defined(COMPILER_GCC)
#define IMMEDIATE_CRASH()    \
  ({                         \
    WRAPPED_TRAP_SEQUENCE(); \
    __builtin_unreachable(); \
  })
#else
// This is supporting non-chromium user of logging.h to build with MSVC, like
// pdfium. On MSVC there is no __builtin_unreachable().
#define IMMEDIATE_CRASH() WRAPPED_TRAP_SEQUENCE()
#endif

#endif  // BASE_IMMEDIATE_CRASH_H_
