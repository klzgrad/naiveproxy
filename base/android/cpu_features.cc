// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cpu-features.h>

#include "base/android/jni_android.h"
#include "jni/CpuFeatures_jni.h"

namespace base {
namespace android {

jint GetCoreCount(JNIEnv*, const JavaParamRef<jclass>&) {
  return android_getCpuCount();
}

jlong GetCpuFeatures(JNIEnv*, const JavaParamRef<jclass>&) {
  return android_getCpuFeatures();
}

}  // namespace android
}  // namespace base
