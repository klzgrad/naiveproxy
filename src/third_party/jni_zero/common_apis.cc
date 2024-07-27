// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/jni_zero/common_apis.h"

namespace jni_zero {

ScopedJavaLocalRef<jobjectArray> CollectionToArray(
    JNIEnv* env,
    const JavaRef<jobject>& collection) {
  return nullptr;
}

ScopedJavaLocalRef<jobject> ArrayToList(JNIEnv* env,
                                        const JavaRef<jobjectArray>& array) {
  return nullptr;
}

}  // namespace jni_zero
