// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/java_exception_reporter.h"

#include "base/android/build_info.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/debug/dump_without_crashing.h"
#include "jni/JavaExceptionReporter_jni.h"

using base::android::JavaParamRef;

namespace base {
namespace android {

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

void JNI_JavaExceptionReporter_ReportJavaException(
    JNIEnv* env,
    const JavaParamRef<jclass>& jcaller,
    jboolean crash_after_report,
    const JavaParamRef<jthrowable>& e) {
  std::string exception_info = base::android::GetJavaExceptionInfo(env, e);
  // Set the exception_string in BuildInfo so that breakpad can read it.
  base::android::BuildInfo::GetInstance()->SetJavaExceptionInfo(exception_info);
  if (crash_after_report) {
    LOG(ERROR) << exception_info;
    LOG(FATAL) << "Uncaught exception";
  }
  base::debug::DumpWithoutCrashing();
  base::android::BuildInfo::GetInstance()->ClearJavaExceptionInfo();
}

void JNI_JavaExceptionReporter_ReportJavaStackTrace(
    JNIEnv* env,
    const JavaParamRef<jclass>& jcaller,
    const JavaParamRef<jstring>& stackTrace) {
  base::android::BuildInfo::GetInstance()->SetJavaExceptionInfo(
      ConvertJavaStringToUTF8(stackTrace));
  base::debug::DumpWithoutCrashing();
  base::android::BuildInfo::GetInstance()->ClearJavaExceptionInfo();
}

}  // namespace android
}  // namespace base
