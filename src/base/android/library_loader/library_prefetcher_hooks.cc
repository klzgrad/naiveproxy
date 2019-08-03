// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/library_loader/anchor_functions_buildflags.h"
#include "base/android/library_loader/library_loader_hooks.h"
#include "base/android/library_loader/library_prefetcher.h"
#include "base/android/scoped_java_ref.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "jni/LibraryPrefetcher_jni.h"

namespace base {
namespace android {

namespace {

// Whether to pin code in memory. Pinning happens in Java, but is controlled
// from here, to obtain the range.
const Feature kPinOrderedCodeInMemory{"PinOrderedCodeInMemory",
                                      FEATURE_ENABLED_BY_DEFAULT};

}  // namespace

static void JNI_LibraryPrefetcher_ForkAndPrefetchNativeLibrary(JNIEnv* env) {
#if BUILDFLAG(SUPPORTS_CODE_ORDERING)
  return NativeLibraryPrefetcher::ForkAndPrefetchNativeLibrary(
      IsUsingOrderfileOptimization());
#endif
}

static jint JNI_LibraryPrefetcher_PercentageOfResidentNativeLibraryCode(
    JNIEnv* env) {
#if BUILDFLAG(SUPPORTS_CODE_ORDERING)
  return NativeLibraryPrefetcher::PercentageOfResidentNativeLibraryCode();
#else
  return -1;
#endif
}

static void JNI_LibraryPrefetcher_PeriodicallyCollectResidency(JNIEnv* env) {
#if BUILDFLAG(SUPPORTS_CODE_ORDERING)
  NativeLibraryPrefetcher::PeriodicallyCollectResidency();
#else
  LOG(WARNING) << "Collecting residency is not supported.";
#endif
}

static ScopedJavaLocalRef<jobject> JNI_LibraryPrefetcher_GetOrderedCodeInfo(
    JNIEnv* env) {
#if BUILDFLAG(SUPPORTS_CODE_ORDERING)
  if (!FeatureList::IsEnabled(kPinOrderedCodeInMemory))
    return {};

  std::string filename;
  size_t start_offset, size;
  bool ok = NativeLibraryPrefetcher::GetOrderedCodeInfo(&filename,
                                                        &start_offset, &size);
  if (!ok)
    return {};

  auto java_filename = ConvertUTF8ToJavaString(env, filename);
  return Java_OrderedCodeInfo_Constructor(env, java_filename,
                                          static_cast<jlong>(start_offset),
                                          static_cast<jlong>(size));
#else
  return {};
#endif  // BUILDFLAG(SUPPORTS_CODE_ORDERING)
}

}  // namespace android
}  // namespace base
