/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/android_sdk/jni/dev_perfetto_sdk_PerfettoNativeMemoryCleaner.h"

#include <jni.h>

#include "src/android_sdk/jni/macros.h"
#include "src/android_sdk/nativehelper/JNIHelp.h"

namespace perfetto {
namespace jni {

typedef void (*FreeFunction)(void*);
// Copied from
// https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/core/jni/platform/host/HostRuntime.cpp;l=56;drc=9a629e24776b648be56188aa3364e7d3953dae11
static void dev_perfetto_sdk_PerfettoTrackEventExtra_applyNativeFunction(
    jlong freeFunction,
    jlong ptr) {
  void* nativePtr = reinterpret_cast<void*>(static_cast<uintptr_t>(ptr));
  FreeFunction nativeFreeFunction =
      reinterpret_cast<FreeFunction>(static_cast<uintptr_t>(freeFunction));
  nativeFreeFunction(nativePtr);
}

static const JNINativeMethod gNativeMemoryCleanerMethods[] = {
    {"applyNativeFunction", "(JJ)V",
     (void*)dev_perfetto_sdk_PerfettoTrackEventExtra_applyNativeFunction}};

int register_dev_perfetto_sdk_PerfettoNativeMemoryCleaner(JNIEnv* env) {
  int res = jniRegisterNativeMethods(
      env,
      TO_MAYBE_JAR_JAR_CLASS_NAME(
          "dev/perfetto/sdk/PerfettoNativeMemoryCleaner"),
      gNativeMemoryCleanerMethods, NELEM(gNativeMemoryCleanerMethods));
  LOG_ALWAYS_FATAL_IF(
      res < 0,
      "Unable to register PerfettoNativeMemoryCleaner native methods.");
  return 0;
}

}  // namespace jni
}  // namespace perfetto
