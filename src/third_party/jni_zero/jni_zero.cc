// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/jni_zero/jni_zero.h"

#include <sys/prctl.h>

#include "third_party/jni_zero/generate_jni/JniInit_jni.h"
#include "third_party/jni_zero/jni_methods.h"
#include "third_party/jni_zero/jni_zero_internal.h"
#include "third_party/jni_zero/logging.h"

#if defined(JNI_ZERO_MULTIPLEXING_ENABLED)
extern const int64_t kJniZeroHashWhole;
extern const int64_t kJniZeroHashPriority;
#endif
namespace jni_zero {
namespace {
// Until we fully migrate base's jni_android, we will maintain a copy of this
// global here and will have base set this variable when it sets its own.
JavaVM* g_jvm = nullptr;

jclass (*g_class_resolver)(JNIEnv*, const char*, const char*) = nullptr;

void (*g_exception_handler_callback)(JNIEnv*) = nullptr;
void (*g_native_to_java_callback)(const char*, const char*) = nullptr;

jclass GetClassInternal(JNIEnv* env,
                        const char* class_name,
                        const char* split_name) {
  jclass clazz;
  if (g_class_resolver != nullptr) {
    clazz = g_class_resolver(env, class_name, split_name);
  } else {
    clazz = env->FindClass(class_name);
  }
  if (ClearException(env) || !clazz) {
    JNI_ZERO_FLOG("Failed to find class %s", class_name);
  }
  return clazz;
}

jclass LazyGetClassInternal(JNIEnv* env,
                            const char* class_name,
                            const char* split_name,
                            std::atomic<jclass>* atomic_class_id) {
  jclass ret = nullptr;
  ScopedJavaGlobalRef<jclass> clazz(
      env, GetClassInternal(env, class_name, split_name));
  if (atomic_class_id->compare_exchange_strong(ret, clazz.obj(),
                                               std::memory_order_acq_rel)) {
    // We intentionally leak the global ref since we are now storing it as a raw
    // pointer in |atomic_class_id|.
    ret = clazz.Release();
  }
  return ret;
}

jclass GetSystemClassGlobalRef(JNIEnv* env, const char* class_name) {
  return static_cast<jclass>(env->NewGlobalRef(env->FindClass(class_name)));
}

}  // namespace

jclass g_object_class = nullptr;
jclass g_string_class = nullptr;
LeakedJavaGlobalRef<jstring> g_empty_string = nullptr;
LeakedJavaGlobalRef<jobject> g_empty_list = nullptr;
LeakedJavaGlobalRef<jobject> g_empty_map = nullptr;

JNIEnv* AttachCurrentThread() {
  JNI_ZERO_DCHECK(g_jvm);
  JNIEnv* env = nullptr;
  jint ret = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_2);
  if (ret == JNI_EDETACHED || !env) {
    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_2;
    args.group = nullptr;

    // 16 is the maximum size for thread names on Android.
    char thread_name[16];
    int err = prctl(PR_GET_NAME, thread_name);
    if (err < 0) {
      JNI_ZERO_ELOG("prctl(PR_GET_NAME)");
      args.name = nullptr;
    } else {
      args.name = thread_name;
    }

#if defined(JNI_ZERO_IS_ROBOLECTRIC)
    ret = g_jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), &args);
#else
    ret = g_jvm->AttachCurrentThread(&env, &args);
#endif
    JNI_ZERO_CHECK(ret == JNI_OK);
  }
  return env;
}

JNIEnv* AttachCurrentThreadWithName(const std::string& thread_name) {
  JNI_ZERO_DCHECK(g_jvm);
  JavaVMAttachArgs args;
  args.version = JNI_VERSION_1_2;
  args.name = const_cast<char*>(thread_name.c_str());
  args.group = nullptr;
  JNIEnv* env = nullptr;
#if defined(JNI_ZERO_IS_ROBOLECTRIC)
  jint ret = g_jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), &args);
#else
  jint ret = g_jvm->AttachCurrentThread(&env, &args);
#endif
  JNI_ZERO_CHECK(ret == JNI_OK);
  return env;
}

void DetachFromVM() {
  // Ignore the return value, if the thread is not attached, DetachCurrentThread
  // will fail. But it is ok as the native thread may never be attached.
  if (g_jvm) {
    g_jvm->DetachCurrentThread();
  }
}

void InitVM(JavaVM* vm) {
  if (g_jvm) {
    JNI_ZERO_CHECK(vm == g_jvm);
    return;
  }
  g_jvm = vm;
  JNIEnv* env = AttachCurrentThread();
  g_object_class = GetSystemClassGlobalRef(env, "java/lang/Object");
  g_string_class = GetSystemClassGlobalRef(env, "java/lang/String");
  g_empty_string.Reset(
      env, ScopedJavaLocalRef<jstring>(env, env->NewString(nullptr, 0)));
#if defined(JNI_ZERO_MULTIPLEXING_ENABLED)
  Java_JniInit_crashIfMultiplexingMisaligned(env, kJniZeroHashWhole,
                                             kJniZeroHashPriority);
#else
  // Mark as used when multiplexing not enabled.
  (void)&Java_JniInit_crashIfMultiplexingMisaligned;
#endif
  ScopedJavaLocalRef<jobjectArray> globals = Java_JniInit_init(env);
  g_empty_list.Reset(env,
                     ScopedJavaLocalRef<jobject>(
                         env, env->GetObjectArrayElement(globals.obj(), 0)));
  g_empty_map.Reset(env,
                    ScopedJavaLocalRef<jobject>(
                        env, env->GetObjectArrayElement(globals.obj(), 1)));
}

void DisableJvmForTesting() {
  g_jvm = nullptr;
}

bool IsVMInitialized() {
  return g_jvm != nullptr;
}

JavaVM* GetVM() {
  return g_jvm;
}

bool HasException(JNIEnv* env) {
  return env->ExceptionCheck() != JNI_FALSE;
}

bool ClearException(JNIEnv* env) {
  if (!HasException(env)) {
    return false;
  }
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}

void SetExceptionHandler(void (*callback)(JNIEnv*)) {
  g_exception_handler_callback = callback;
}

void SetNativeToJavaCallback(void (*callback)(char const*, char const*)) {
  g_native_to_java_callback = callback;
}

void CallNativeToJavaCallback(char const* class_name, char const* method_name) {
  if (g_native_to_java_callback) {
    g_native_to_java_callback(class_name, method_name);
  }
}

void CheckException(JNIEnv* env) {
  if (!HasException(env)) {
    return;
  }

  if (g_exception_handler_callback) {
    return g_exception_handler_callback(env);
  }
  env->ExceptionDescribe();
  JNI_ZERO_FLOG("jni_zero crashing due to uncaught Java exception");
}

void SetClassResolver(jclass (*resolver)(JNIEnv*, const char*, const char*)) {
  g_class_resolver = resolver;
}

ScopedJavaLocalRef<jclass> GetClass(JNIEnv* env,
                                    const char* class_name,
                                    const char* split_name) {
  return ScopedJavaLocalRef<jclass>(
      env, GetClassInternal(env, class_name, split_name));
}

ScopedJavaLocalRef<jclass> GetClass(JNIEnv* env, const char* class_name) {
  return ScopedJavaLocalRef<jclass>(env, GetClassInternal(env, class_name, ""));
}

template <MethodID::Type type>
jmethodID MethodID::Get(JNIEnv* env,
                        jclass clazz,
                        const char* method_name,
                        const char* jni_signature) {
  auto get_method_ptr = type == MethodID::TYPE_STATIC
                            ? &JNIEnv::GetStaticMethodID
                            : &JNIEnv::GetMethodID;
  jmethodID id = (env->*get_method_ptr)(clazz, method_name, jni_signature);
  if (ClearException(env) || !id) {
    JNI_ZERO_FLOG("Failed to find class %smethod %s %s",
                  (type == TYPE_STATIC ? "static " : ""), method_name,
                  jni_signature);
  }
  return id;
}

// If |atomic_method_id| set, it'll return immediately. Otherwise, it'll call
// into ::Get() above. If there's a race, it's ok since the values are the same
// (and the duplicated effort will happen only once).
template <MethodID::Type type>
jmethodID MethodID::LazyGet(JNIEnv* env,
                            jclass clazz,
                            const char* method_name,
                            const char* jni_signature,
                            std::atomic<jmethodID>* atomic_method_id) {
  const jmethodID value = atomic_method_id->load(std::memory_order_acquire);
  if (value) {
    return value;
  }
  jmethodID id = MethodID::Get<type>(env, clazz, method_name, jni_signature);
  atomic_method_id->store(id, std::memory_order_release);
  return id;
}

// Various template instantiations.
template jmethodID MethodID::Get<MethodID::TYPE_STATIC>(
    JNIEnv* env,
    jclass clazz,
    const char* method_name,
    const char* jni_signature);

template jmethodID MethodID::Get<MethodID::TYPE_INSTANCE>(
    JNIEnv* env,
    jclass clazz,
    const char* method_name,
    const char* jni_signature);

template jmethodID MethodID::LazyGet<MethodID::TYPE_STATIC>(
    JNIEnv* env,
    jclass clazz,
    const char* method_name,
    const char* jni_signature,
    std::atomic<jmethodID>* atomic_method_id);

template jmethodID MethodID::LazyGet<MethodID::TYPE_INSTANCE>(
    JNIEnv* env,
    jclass clazz,
    const char* method_name,
    const char* jni_signature,
    std::atomic<jmethodID>* atomic_method_id);

namespace internal {
jclass LazyGetClass(JNIEnv* env,
                    const char* class_name,
                    const char* split_name,
                    std::atomic<jclass>* atomic_class_id) {
  jclass ret = atomic_class_id->load(std::memory_order_acquire);
  if (ret == nullptr) {
    ret = LazyGetClassInternal(env, class_name, split_name, atomic_class_id);
  }
  return ret;
}

jclass LazyGetClass(JNIEnv* env,
                    const char* class_name,
                    std::atomic<jclass>* atomic_class_id) {
  jclass ret = atomic_class_id->load(std::memory_order_acquire);
  if (ret == nullptr) {
    ret = LazyGetClassInternal(env, class_name, "", atomic_class_id);
  }
  return ret;
}

}  // namespace internal
}  // namespace jni_zero
