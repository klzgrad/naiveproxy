// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <jni.h>

#include <map>
#include <string>

#include "base/android/jni_string.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "jni/FieldTrialList_jni.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

static ScopedJavaLocalRef<jstring> JNI_FieldTrialList_FindFullName(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jtrial_name) {
  std::string trial_name(ConvertJavaStringToUTF8(env, jtrial_name));
  return ConvertUTF8ToJavaString(
      env, base::FieldTrialList::FindFullName(trial_name));
}

static jboolean JNI_FieldTrialList_TrialExists(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jtrial_name) {
  std::string trial_name(ConvertJavaStringToUTF8(env, jtrial_name));
  return base::FieldTrialList::TrialExists(trial_name);
}

static ScopedJavaLocalRef<jstring> JNI_FieldTrialList_GetVariationParameter(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jtrial_name,
    const JavaParamRef<jstring>& jparameter_key) {
  std::map<std::string, std::string> parameters;
  base::GetFieldTrialParams(ConvertJavaStringToUTF8(env, jtrial_name),
                            &parameters);
  return ConvertUTF8ToJavaString(
      env, parameters[ConvertJavaStringToUTF8(env, jparameter_key)]);
}
