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
#include <android/log.h>
#include <jni.h>

#include "src/android_sdk/jni/dev_perfetto_sdk_PerfettoNativeMemoryCleaner.h"
#include "src/android_sdk/jni/dev_perfetto_sdk_PerfettoTrackEventExtra.h"
#include "src/android_sdk/jni/macros.h"
#include "src/android_sdk/nativehelper/JNIHelp.h"
#include "src/android_sdk/nativehelper/scoped_utf_chars.h"
#include "src/android_sdk/nativehelper/utils.h"
#include "src/android_sdk/perfetto_sdk_for_jni/tracing_sdk.h"

namespace perfetto {
namespace jni {
constexpr int kFlushTimeoutMs = 5000;

template <typename T>
inline static T* toPointer(jlong ptr) {
  return reinterpret_cast<T*>(static_cast<uintptr_t>(ptr));
}

template <typename T>
inline static jlong toJLong(T* ptr) {
  return static_cast<jlong>(reinterpret_cast<uintptr_t>(ptr));
}

static jlong dev_perfetto_sdk_PerfettoTrace_get_process_track_uuid() {
  return sdk_for_jni::get_process_track_uuid();
}

static jlong dev_perfetto_sdk_PerfettoTrace_get_thread_track_uuid(jlong tid) {
  return sdk_for_jni::get_thread_track_uuid(tid);
}

static void dev_perfetto_sdk_PerfettoTrace_activate_trigger(JNIEnv* env,
                                                            jclass,
                                                            jstring name,
                                                            jint ttl_ms) {
  ScopedUtfChars name_chars = GET_UTF_OR_RETURN_VOID(env, name);
  sdk_for_jni::activate_trigger(name_chars.c_str(),
                                static_cast<uint32_t>(ttl_ms));
}

void dev_perfetto_sdk_PerfettoTrace_register(JNIEnv*,
                                             jclass,
                                             bool is_backend_in_process) {
  sdk_for_jni::register_perfetto(is_backend_in_process);
}

static jlong dev_perfetto_sdk_PerfettoTraceCategory_init(JNIEnv* env,
                                                         jclass,
                                                         jstring name,
                                                         jobjectArray tags) {
  std::string name_c_str(GET_UTF_OR_RETURN(env, name));
  std::vector<std::string> tags_c_strs;
  for (int i = 0; i < env->GetArrayLength(tags); i++) {
    auto tag = reinterpret_cast<jstring>(env->GetObjectArrayElement(tags, i));
    tags_c_strs.emplace_back(GET_UTF_OR_RETURN(env, tag));
  }

  return toJLong(new sdk_for_jni::Category(name_c_str, tags_c_strs));
}

static jlong dev_perfetto_sdk_PerfettoTraceCategory_delete() {
  return toJLong(&sdk_for_jni::Category::delete_category);
}

static void dev_perfetto_sdk_PerfettoTraceCategory_register(jlong ptr) {
  auto* category = toPointer<sdk_for_jni::Category>(ptr);
  category->register_category();
}

static void dev_perfetto_sdk_PerfettoTraceCategory_unregister(jlong ptr) {
  auto* category = toPointer<sdk_for_jni::Category>(ptr);
  category->unregister_category();
}

static jboolean dev_perfetto_sdk_PerfettoTraceCategory_is_enabled(jlong ptr) {
  auto* category = toPointer<sdk_for_jni::Category>(ptr);
  return category->is_category_enabled();
}

static jlong dev_perfetto_sdk_PerfettoTrace_start_session(
    JNIEnv* env,
    jclass /* obj */,
    jboolean is_backend_in_process,
    jbyteArray config_bytes) {
  jsize length = env->GetArrayLength(config_bytes);
  std::vector<uint8_t> data;
  data.reserve(length);
  env->GetByteArrayRegion(config_bytes, 0, length,
                          reinterpret_cast<jbyte*>(data.data()));

  auto session =
      new sdk_for_jni::Session(is_backend_in_process, data.data(), length);

  return reinterpret_cast<long>(session);
}

static jbyteArray dev_perfetto_sdk_PerfettoTrace_stop_session(
    [[maybe_unused]] JNIEnv* env,
    jclass /* obj */,
    jlong ptr) {
  auto* session = reinterpret_cast<sdk_for_jni::Session*>(ptr);

  session->FlushBlocking(kFlushTimeoutMs);
  session->StopBlocking();

  std::vector<uint8_t> data = session->ReadBlocking();

  delete session;

  jbyteArray bytes = env->NewByteArray(data.size());
  env->SetByteArrayRegion(bytes, 0, data.size(),
                          reinterpret_cast<jbyte*>(data.data()));
  return bytes;
}

static const JNINativeMethod gCategoryMethods[] = {
    {"native_init", "(Ljava/lang/String;[Ljava/lang/String;)J",
     (void*)dev_perfetto_sdk_PerfettoTraceCategory_init},
    {"native_delete", "()J",
     (void*)dev_perfetto_sdk_PerfettoTraceCategory_delete},
    {"native_register", "(J)V",
     (void*)dev_perfetto_sdk_PerfettoTraceCategory_register},
    {"native_unregister", "(J)V",
     (void*)dev_perfetto_sdk_PerfettoTraceCategory_unregister},
    {"native_is_enabled", "(J)Z",
     (void*)dev_perfetto_sdk_PerfettoTraceCategory_is_enabled},
};

static const JNINativeMethod gTraceMethods[] = {
    {"native_get_process_track_uuid", "()J",
     (void*)dev_perfetto_sdk_PerfettoTrace_get_process_track_uuid},
    {"native_get_thread_track_uuid", "(J)J",
     (void*)dev_perfetto_sdk_PerfettoTrace_get_thread_track_uuid},
    {"native_activate_trigger", "(Ljava/lang/String;I)V",
     (void*)dev_perfetto_sdk_PerfettoTrace_activate_trigger},
    {"native_register", "(Z)V", (void*)dev_perfetto_sdk_PerfettoTrace_register},
    {"native_start_session", "(Z[B)J",
     (void*)dev_perfetto_sdk_PerfettoTrace_start_session},
    {"native_stop_session", "(J)[B",
     (void*)dev_perfetto_sdk_PerfettoTrace_stop_session}};

int register_dev_perfetto_sdk_PerfettoTrace(JNIEnv* env) {
  int res = jniRegisterNativeMethods(
      env, TO_MAYBE_JAR_JAR_CLASS_NAME("dev/perfetto/sdk/PerfettoTrace"),
      gTraceMethods, NELEM(gTraceMethods));
  LOG_ALWAYS_FATAL_IF(res < 0, "Unable to register perfetto native methods.");

  res = jniRegisterNativeMethods(
      env,
      TO_MAYBE_JAR_JAR_CLASS_NAME("dev/perfetto/sdk/PerfettoTrace$Category"),
      gCategoryMethods, NELEM(gCategoryMethods));
  LOG_ALWAYS_FATAL_IF(res < 0, "Unable to register category native methods.");

  return 0;
}

}  // namespace jni
}  // namespace perfetto

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  perfetto::jni::register_dev_perfetto_sdk_PerfettoTrace(env);
  perfetto::jni::register_dev_perfetto_sdk_PerfettoTrackEventExtra(env);
  perfetto::jni::register_dev_perfetto_sdk_PerfettoNativeMemoryCleaner(env);

  return JNI_VERSION_1_6;
}
