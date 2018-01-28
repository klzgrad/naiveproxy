// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "jni/CommandLine_jni.h"

using base::android::ConvertUTF8ToJavaString;
using base::android::ConvertJavaStringToUTF8;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using base::CommandLine;

namespace {

void AppendJavaStringArrayToCommandLine(JNIEnv* env,
                                        const JavaParamRef<jobjectArray>& array,
                                        bool includes_program) {
  std::vector<std::string> vec;
  if (array)
    base::android::AppendJavaStringArrayToStringVector(env, array, &vec);
  if (!includes_program)
    vec.insert(vec.begin(), std::string());
  CommandLine extra_command_line(vec);
  CommandLine::ForCurrentProcess()->AppendArguments(extra_command_line,
                                                    includes_program);
}

}  // namespace

static jboolean HasSwitch(JNIEnv* env,
                          const JavaParamRef<jclass>& clazz,
                          const JavaParamRef<jstring>& jswitch) {
  std::string switch_string(ConvertJavaStringToUTF8(env, jswitch));
  return CommandLine::ForCurrentProcess()->HasSwitch(switch_string);
}

static ScopedJavaLocalRef<jstring> GetSwitchValue(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jswitch) {
  std::string switch_string(ConvertJavaStringToUTF8(env, jswitch));
  std::string value(CommandLine::ForCurrentProcess()->GetSwitchValueNative(
      switch_string));
  if (value.empty())
    return ScopedJavaLocalRef<jstring>();
  return ConvertUTF8ToJavaString(env, value);
}

static void AppendSwitch(JNIEnv* env,
                         const JavaParamRef<jclass>& clazz,
                         const JavaParamRef<jstring>& jswitch) {
  std::string switch_string(ConvertJavaStringToUTF8(env, jswitch));
  CommandLine::ForCurrentProcess()->AppendSwitch(switch_string);
}

static void AppendSwitchWithValue(JNIEnv* env,
                                  const JavaParamRef<jclass>& clazz,
                                  const JavaParamRef<jstring>& jswitch,
                                  const JavaParamRef<jstring>& jvalue) {
  std::string switch_string(ConvertJavaStringToUTF8(env, jswitch));
  std::string value_string (ConvertJavaStringToUTF8(env, jvalue));
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(switch_string,
                                                      value_string);
}

static void AppendSwitchesAndArguments(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jobjectArray>& array) {
  AppendJavaStringArrayToCommandLine(env, array, false);
}

static void Init(JNIEnv* env,
                 const JavaParamRef<jclass>& jclazz,
                 const JavaParamRef<jobjectArray>& init_command_line) {
  // TODO(port): Make an overload of Init() that takes StringVector rather than
  // have to round-trip via AppendArguments.
  CommandLine::Init(0, nullptr);
  AppendJavaStringArrayToCommandLine(env, init_command_line, true);
}
