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

#ifndef SRC_ANDROID_SDK_JNI_MACROS_H_
#define SRC_ANDROID_SDK_JNI_MACROS_H_
#include <string_view>

// This is a very basic check to make sure we get the
// 'PERFETTO_JNI_JARJAR_PREFIX' in the form of 'com/android/internal/'.
constexpr static bool IsValidJavaPackage(const std::string_view str) {
  if (str.empty())
    return false;
  if (str.front() == '/')
    return false;
  if (str.back() != '/')
    return false;
  for (const char c : str) {
    if (!((c >= 'a' && c <= 'z') || c == '/')) {
      return false;
    }
  }
  return true;
}

#define TO_STRING1(x) #x
#define TO_STRING(x) TO_STRING1(x)

#ifdef PERFETTO_JNI_JARJAR_PREFIX
static_assert(IsValidJavaPackage(TO_STRING(PERFETTO_JNI_JARJAR_PREFIX)));
#define TO_MAYBE_JAR_JAR_CLASS_NAME(className) \
  TO_STRING(PERFETTO_JNI_JARJAR_PREFIX) className
#else
#define TO_MAYBE_JAR_JAR_CLASS_NAME(className) className
#endif

#ifndef LOG_ALWAYS_FATAL_IF
#define LOG_ALWAYS_FATAL_IF(cond, fmt) \
  if (cond)                            \
    __android_log_assert(nullptr, "PerfettoJNI", fmt);
#endif

#endif  // SRC_ANDROID_SDK_JNI_MACROS_H_
