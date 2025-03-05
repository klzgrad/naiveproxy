// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_JNI_UTILS_H_
#define BASE_ANDROID_JNI_UTILS_H_

#include <jni.h>

#include "base/android/scoped_java_ref.h"

namespace base {

namespace android {

// Gets a ClassLoader instance which can load Java classes from the specified
// split.
jobject GetSplitClassLoader(JNIEnv* env, const char* split_name);

// Gets a ClassLoader instance capable of loading Chromium java classes.
// This should be called either from JNI_OnLoad or from within a method called
// via JNI from Java.
inline jobject GetClassLoader(JNIEnv* env) {
  return GetSplitClassLoader(env, "");
}

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_JNI_UTILS_H_
