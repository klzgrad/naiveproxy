// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_LIBRARY_LOADER_ANCHOR_FUNCTIONS_H_
#define BASE_ANDROID_LIBRARY_LOADER_ANCHOR_FUNCTIONS_H_

#include <cstdint>

#include "base/base_export.h"
#include "build/build_config.h"

#if defined(ARCH_CPU_ARMEL)
namespace base {
namespace android {

// Start and end of .text, respectively.
BASE_EXPORT extern const size_t kStartOfText;
BASE_EXPORT extern const size_t kEndOfText;

// Basic CHECK()s ensuring that the symbols above are correctly set.
BASE_EXPORT void CheckOrderingSanity();

}  // namespace android
}  // namespace base
#endif  // defined(ARCH_CPU_ARMEL)

#endif  // BASE_ANDROID_LIBRARY_LOADER_ANCHOR_FUNCTIONS_H_
