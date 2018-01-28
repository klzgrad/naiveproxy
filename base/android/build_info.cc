// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/build_info.h"

#include <string>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/scoped_java_ref.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/strings/string_number_conversions.h"
#include "jni/BuildInfo_jni.h"

namespace base {
namespace android {

namespace {

// We are leaking these strings.
const char* StrDupParam(const std::vector<std::string>& params, int index) {
  return strdup(params[index].c_str());
}

int SdkIntParam(const std::vector<std::string>& params, int index) {
  int ret = 0;
  bool success = StringToInt(params[index], &ret);
  DCHECK(success);
  return ret;
}

}  // namespace

struct BuildInfoSingletonTraits {
  static BuildInfo* New() {
    JNIEnv* env = AttachCurrentThread();
    ScopedJavaLocalRef<jobjectArray> params_objs = Java_BuildInfo_getAll(env);
    std::vector<std::string> params;
    AppendJavaStringArrayToStringVector(env, params_objs.obj(), &params);
    return new BuildInfo(params);
  }

  static void Delete(BuildInfo* x) {
    // We're leaking this type, see kRegisterAtExit.
    NOTREACHED();
  }

  static const bool kRegisterAtExit = false;
#if DCHECK_IS_ON()
  static const bool kAllowedToAccessOnNonjoinableThread = true;
#endif
};

BuildInfo::BuildInfo(const std::vector<std::string>& params)
    : brand_(StrDupParam(params, 0)),
      device_(StrDupParam(params, 1)),
      android_build_id_(StrDupParam(params, 2)),
      manufacturer_(StrDupParam(params, 3)),
      model_(StrDupParam(params, 4)),
      sdk_int_(SdkIntParam(params, 5)),
      build_type_(StrDupParam(params, 6)),
      package_label_(StrDupParam(params, 7)),
      package_name_(StrDupParam(params, 8)),
      package_version_code_(StrDupParam(params, 9)),
      package_version_name_(StrDupParam(params, 10)),
      android_build_fp_(StrDupParam(params, 11)),
      gms_version_code_(StrDupParam(params, 12)),
      installer_package_name_(StrDupParam(params, 13)),
      abi_name_(StrDupParam(params, 14)),
      extracted_file_suffix_(params[15]),
      java_exception_info_(NULL) {}

// static
BuildInfo* BuildInfo::GetInstance() {
  return Singleton<BuildInfo, BuildInfoSingletonTraits >::get();
}

void BuildInfo::SetJavaExceptionInfo(const std::string& info) {
  DCHECK(!java_exception_info_) << "info should be set only once.";
  java_exception_info_ = strndup(info.c_str(), 4096);
}

void BuildInfo::ClearJavaExceptionInfo() {
  delete java_exception_info_;
  java_exception_info_ = nullptr;
}

}  // namespace android
}  // namespace base
