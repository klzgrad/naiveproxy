// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_android.h"

#include "base/debug/debugging_buildflags.h"
#include "base/lazy_instance.h"
#include "base/threading/thread_local.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"

namespace base {
namespace android {
namespace {
#if BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)
ABSL_CONST_INIT thread_local void* stack_frame_pointer = nullptr;
#endif
}  // namespace

JNIEnv* AttachCurrentThread() {
  return nullptr;
}

JNIEnv* AttachCurrentThreadWithName(const std::string& thread_name) {
  return nullptr;
}

void DetachFromVM() {
}

void InitVM(JavaVM* vm) {
}

bool IsVMInitialized() {
  return false;
}

void InitReplacementClassLoader(JNIEnv* env,
                                const JavaRef<jobject>& class_loader) {
}

ScopedJavaLocalRef<jclass> GetClass(JNIEnv* env,
                                    const char* class_name,
                                    const std::string& split_name) {
  return nullptr;
}

ScopedJavaLocalRef<jclass> GetClass(JNIEnv* env, const char* class_name) {
  return nullptr;
}

// This is duplicated with LazyGetClass below because these are performance
// sensitive.
jclass LazyGetClass(JNIEnv* env,
                    const char* class_name,
                    const std::string& split_name,
                    std::atomic<jclass>* atomic_class_id) {
  return nullptr;
}

// This is duplicated with LazyGetClass above because these are performance
// sensitive.
jclass LazyGetClass(JNIEnv* env,
                    const char* class_name,
                    std::atomic<jclass>* atomic_class_id) {
  return nullptr;
}

template<MethodID::Type type>
jmethodID MethodID::Get(JNIEnv* env,
                        jclass clazz,
                        const char* method_name,
                        const char* jni_signature) {
  return nullptr;
}

// If |atomic_method_id| set, it'll return immediately. Otherwise, it'll call
// into ::Get() above. If there's a race, it's ok since the values are the same
// (and the duplicated effort will happen only once).
template<MethodID::Type type>
jmethodID MethodID::LazyGet(JNIEnv* env,
                            jclass clazz,
                            const char* method_name,
                            const char* jni_signature,
                            std::atomic<jmethodID>* atomic_method_id) {
  return nullptr;
}

// Various template instantiations.
template jmethodID MethodID::Get<MethodID::TYPE_STATIC>(
    JNIEnv* env, jclass clazz, const char* method_name,
    const char* jni_signature);

template jmethodID MethodID::Get<MethodID::TYPE_INSTANCE>(
    JNIEnv* env, jclass clazz, const char* method_name,
    const char* jni_signature);

template jmethodID MethodID::LazyGet<MethodID::TYPE_STATIC>(
    JNIEnv* env, jclass clazz, const char* method_name,
    const char* jni_signature, std::atomic<jmethodID>* atomic_method_id);

template jmethodID MethodID::LazyGet<MethodID::TYPE_INSTANCE>(
    JNIEnv* env, jclass clazz, const char* method_name,
    const char* jni_signature, std::atomic<jmethodID>* atomic_method_id);

bool HasException(JNIEnv* env) {
  return false;
}

bool ClearException(JNIEnv* env) {
  return true;
}

void CheckException(JNIEnv* env) {
}

std::string GetJavaExceptionInfo(JNIEnv* env, jthrowable java_throwable) {
  return {};
}

#if BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)

JNIStackFrameSaver::JNIStackFrameSaver(void* current_fp)
    : resetter_(&stack_frame_pointer, current_fp) {}

JNIStackFrameSaver::~JNIStackFrameSaver() = default;

void* JNIStackFrameSaver::SavedFrame() {
  return stack_frame_pointer;
}

#endif  // BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)

}  // namespace android
}  // namespace base
