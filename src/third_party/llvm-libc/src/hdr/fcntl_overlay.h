//===-- Including fcntl.h in overlay mode ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_HDR_FCNTL_OVERLAY_H
#define LLVM_LIBC_HDR_FCNTL_OVERLAY_H

#ifdef LIBC_FULL_BUILD
#error "This header should only be included in overlay mode"
#endif

// Overlay mode

// glibc <fcntl.h> header might provide extern inline definitions for few
// functions, causing external alias errors.  They are guarded by
// `__USE_FORTIFY_LEVEL`, which will be temporarily disabled
// with `_FORTIFY_SOURCE`.

#ifdef __USE_FORTIFY_LEVEL
#define LIBC_OLD_USE_FORTIFY_LEVEL __USE_FORTIFY_LEVEL
#undef __USE_FORTIFY_LEVEL
#define __USE_FORTIFY_LEVEL 0
#endif

#include <fcntl.h>

#ifdef LIBC_OLD_USE_FORTIFY_LEVEL
#undef __USE_FORTIFY_LEVEL
#define __USE_FORTIFY_LEVEL LIBC_OLD_USE_FORTIFY_LEVEL
#undef LIBC_OLD_USE_FORTIFY_LEVEL
#endif

#endif // LLVM_LIBC_HDR_FCNTL_OVERLAY_H