// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/java_exception_reporter.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/debug/dump_without_crashing.h"
#include "jni/JavaExceptionReporter_jni.h"

using base::android::JavaParamRef;

namespace base {
namespace android {

namespace {

void (*g_java_exception_callback)(const char*);

}  // namespace

void InitJavaExceptionReporter() {
  JNIEnv* env = base::android::AttachCurrentThread();
  constexpr bool crash_after_report = false;
  Java_JavaExceptionReporter_installHandler(env, crash_after_report);
}

void InitJavaExceptionReporterForChildProcess() {
  JNIEnv* env = base::android::AttachCurrentThread();
  constexpr bool crash_after_report = true;
  Java_JavaExceptionReporter_installHandler(env, crash_after_report);
}

void SetJavaExceptionCallback(void (*callback)(const char*)) {
  DCHECK(!g_java_exception_callback);
  g_java_exception_callback = callback;
}

void SetJavaException(const char* exception) {
  DCHECK(g_java_exception_callback);
  g_java_exception_callback(exception);
}

void JNI_JavaExceptionReporter_ReportJavaException(
    JNIEnv* env,
    jboolean crash_after_report,
    const JavaParamRef<jthrowable>& e) {
  std::string exception_info = base::android::GetJavaExceptionInfo(env, e);
  SetJavaException(exception_info.c_str());
  if (crash_after_report) {
    LOG(ERROR) << exception_info;
    LOG(FATAL) << "Uncaught exception";
  }
  base::debug::DumpWithoutCrashing();
  SetJavaException(nullptr);
}

void JNI_JavaExceptionReporter_ReportJavaStackTrace(
    JNIEnv* env,
    const JavaParamRef<jstring>& stackTrace) {
  SetJavaException(ConvertJavaStringToUTF8(stackTrace).c_str());
  base::debug::DumpWithoutCrashing();
  SetJavaException(nullptr);
}

}  // namespace android
}  // namespace base
