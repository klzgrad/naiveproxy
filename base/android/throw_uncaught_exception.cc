// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/throw_uncaught_exception.h"

#include "base/android/jni_android.h"

#include "jni/ThrowUncaughtException_jni.h"

namespace base {
namespace android {

void ThrowUncaughtException() {
  Java_ThrowUncaughtException_post(AttachCurrentThread());
}

}  // namespace android
}  // namespace base
