// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/android/jni_string.h"
#include "base/metrics/statistics_recorder.h"
#include "jni/StatisticsRecorderAndroid_jni.h"

using base::android::JavaParamRef;
using base::android::ConvertUTF8ToJavaString;

namespace base {
namespace android {

static ScopedJavaLocalRef<jstring> ToJson(JNIEnv* env,
                                          const JavaParamRef<jclass>& clazz) {
  return ConvertUTF8ToJavaString(
      env, base::StatisticsRecorder::ToJSON(std::string()));
}

}  // namespace android
}  // namespace base
