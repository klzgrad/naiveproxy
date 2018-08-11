// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_THROW_UNCAUGHT_EXCEPTION_H_
#define BASE_ANDROID_THROW_UNCAUGHT_EXCEPTION_H_

#include "base/base_export.h"

namespace base {
namespace android {

// Throw that completely unwinds the java stack. In particular, this will not
// trigger a jni CheckException crash.
BASE_EXPORT void ThrowUncaughtException();

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_THROW_UNCAUGHT_EXCEPTION_H_
