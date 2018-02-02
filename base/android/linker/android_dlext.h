// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Definitions for android_dlopen_ext().
//
// This function was added for Android L-MR1 and made available in android-21
// but we currently build Chromium with android-16. Placing the declarations
// we need here allows code that uses android_dlopen_ext() to build with
// android-16. At runtime we check the target's SDK_INT to ensure that we
// are on a system new enough to offer this function, and also only access
// it with dlsym so that the runtime linker on pre-Android L-MR1 targets will
// not complain about a missing symbol when loading our library.
//
// Details below taken from:
//   third_party/android_tools/ndk/platforms/android-21
//       /arch-arm/usr/include/android/dlext.h
//
// Although taken specifically from arch-arm, there are no architecture-
// specific elements in dlext.h. All android-21/arch-* directories contain
// identical copies of dlext.h.

#ifndef BASE_ANDROID_LINKER_ANDROID_DLEXT_H_
#define BASE_ANDROID_LINKER_ANDROID_DLEXT_H_

#include <stddef.h>
#include <stdint.h>

/* bitfield definitions for android_dlextinfo.flags */
enum {
  /* When set, the reserved_addr and reserved_size fields must point to an
   * already-reserved region of address space which will be used to load the
   * library if it fits. If the reserved region is not large enough, the load
   * will fail.
   */
  ANDROID_DLEXT_RESERVED_ADDRESS      = 0x1,

  /* As DLEXT_RESERVED_ADDRESS, but if the reserved region is not large enough,
   * the linker will choose an available address instead.
   */
  ANDROID_DLEXT_RESERVED_ADDRESS_HINT = 0x2,

  /* When set, write the GNU RELRO section of the mapped library to relro_fd
   * after relocation has been performed, to allow it to be reused by another
   * process loading the same library at the same address. This implies
   * ANDROID_DLEXT_USE_RELRO.
   */
  ANDROID_DLEXT_WRITE_RELRO           = 0x4,

  /* When set, compare the GNU RELRO section of the mapped library to relro_fd
   * after relocation has been performed, and replace any relocated pages that
   * are identical with a version mapped from the file.
   */
  ANDROID_DLEXT_USE_RELRO             = 0x8,

  /* Instruct dlopen to use library_fd instead of opening file by name.
   * The filename parameter is still used to identify the library.
   */
  ANDROID_DLEXT_USE_LIBRARY_FD        = 0x10,

  /* Mask of valid bits */
  ANDROID_DLEXT_VALID_FLAG_BITS       = ANDROID_DLEXT_RESERVED_ADDRESS |
                                        ANDROID_DLEXT_RESERVED_ADDRESS_HINT |
                                        ANDROID_DLEXT_WRITE_RELRO |
                                        ANDROID_DLEXT_USE_RELRO |
                                        ANDROID_DLEXT_USE_LIBRARY_FD,
};

typedef struct {
  uint64_t flags;
  void*   reserved_addr;
  size_t  reserved_size;
  int     relro_fd;
  int     library_fd;
} android_dlextinfo;

#endif  // BASE_ANDROID_LINKER_ANDROID_DLEXT_H_
