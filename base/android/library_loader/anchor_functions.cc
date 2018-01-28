// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/library_loader/anchor_functions.h"

#include "base/logging.h"

// asm() macros below don't compile on x86, and haven't been validated outside
// ARM.
#if defined(ARCH_CPU_ARMEL)
// These functions are here to, respectively:
// 1. Check that functions are ordered
// 2. Delimit the start of .text
// 3. Delimit the end of .text
//
// (2) and (3) require a suitably constructed orderfile, with these
// functions at the beginning and end. (1) doesn't need to be in it.
//
// These functions are weird: this is due to ICF (Identical Code Folding).
// The linker merges functions that have the same code, which would be the case
// if these functions were empty, or simple.
// Gold's flag --icf=safe will *not* alias functions when their address is used
// in code, but as of November 2017, we use the default setting that
// deduplicates function in this case as well.
//
// Thus these functions are made to be unique, using inline .word in assembly.
//
// Note that code |CheckOrderingSanity()| below will make sure that these
// functions are not aliased, in case the toolchain becomes really clever.
extern "C" {

void dummy_function_to_check_ordering() {
  asm(".word 0xe19c683d");
  asm(".word 0xb3d2b56");
}

void dummy_function_to_anchor_text() {
  asm(".word 0xe1f8940b");
  asm(".word 0xd5190cda");
}

void dummy_function_at_the_end_of_text() {
  asm(".word 0x133b9613");
  asm(".word 0xdcd8c46a");
}

}  // extern "C"

namespace base {
namespace android {

const size_t kStartOfText =
    reinterpret_cast<size_t>(dummy_function_to_anchor_text);
const size_t kEndOfText =
    reinterpret_cast<size_t>(dummy_function_at_the_end_of_text);

void CheckOrderingSanity() {
  // The linker usually keeps the input file ordering for symbols.
  // dummy_function_to_anchor_text() should then be after
  // dummy_function_to_check_ordering() without ordering.
  // This check is thus intended to catch the lack of ordering.
  CHECK_LT(kStartOfText,
           reinterpret_cast<size_t>(&dummy_function_to_check_ordering));
  CHECK_LT(kStartOfText, kEndOfText);
  CHECK_LT(kStartOfText,
           reinterpret_cast<size_t>(&dummy_function_to_check_ordering));
  CHECK_LT(kStartOfText, reinterpret_cast<size_t>(&CheckOrderingSanity));
  CHECK_GT(kEndOfText, reinterpret_cast<size_t>(&CheckOrderingSanity));
}

}  // namespace android
}  // namespace base
#endif  // defined(ARCH_CPU_ARMEL)
