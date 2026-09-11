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

#include "src/java_sdk/main/cpp/com_google_perfetto_sdk_PerfettoExampleWrapper.h"

#include <jni.h>

#include <string>

#include "src/java_sdk/main/cpp/example.h"

static jint Java_com_google_perfetto_sdk_PerfettoExampleWrapper_runPerfettoMain(
    JNIEnv* env,
    jobject /*thiz*/,
    jstring outputFilePath) {
  const char* cstr = env->GetStringUTFChars(outputFilePath, NULL);
  std::string file_path = std::string(cstr);
  env->ReleaseStringUTFChars(outputFilePath, cstr);
  return run_main(file_path);
}

static jint
Java_com_google_perfetto_sdk_PerfettoExampleWrapper_incrementIntCritical(
    jint value) {
  return value + 1;
}

static const JNINativeMethod myMethods[] = {
    {"runPerfettoMain", "(Ljava/lang/String;)I",
     reinterpret_cast<void*>(
         Java_com_google_perfetto_sdk_PerfettoExampleWrapper_runPerfettoMain)},
    {"incrementIntCritical", "(I)I",
     reinterpret_cast<void*>(
         Java_com_google_perfetto_sdk_PerfettoExampleWrapper_incrementIntCritical)}};

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  jclass myCls =
      env->FindClass("com/google/perfetto/sdk/PerfettoExampleWrapper");
  if (myCls == nullptr) {
    return JNI_ERR;
  }

  int rc = env->RegisterNatives(myCls, myMethods,
                                sizeof(myMethods) / sizeof(JNINativeMethod));
  if (rc != JNI_OK) {
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
