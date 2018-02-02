// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the Android-specific Chromium linker, a tiny shared library
// implementing a custom dynamic linker that can be used to load the
// real Chromium libraries.

// The main point of this linker is to be able to share the RELRO
// section of libchrome.so (or equivalent) between renderer processes.

// This source code *cannot* depend on anything from base/ or the C++
// STL, to keep the final library small, and avoid ugly dependency issues.

#include "linker_jni.h"

#include <sys/mman.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "legacy_linker_jni.h"
#include "modern_linker_jni.h"

namespace chromium_android_linker {

// Variable containing LibInfo for the loaded library.
LibInfo_class s_lib_info_fields;

// Simple scoped UTF String class constructor.
String::String(JNIEnv* env, jstring str) {
  size_ = env->GetStringUTFLength(str);
  ptr_ = static_cast<char*>(::malloc(size_ + 1));

  // Note: This runs before browser native code is loaded, and so cannot
  // rely on anything from base/. This means that we must use
  // GetStringUTFChars() and not base::android::ConvertJavaStringToUTF8().
  //
  // GetStringUTFChars() suffices because the only strings used here are
  // paths to APK files or names of shared libraries, all of which are
  // plain ASCII, defined and hard-coded by the Chromium Android build.
  //
  // For more: see
  //   https://crbug.com/508876
  //
  // Note: GetStringUTFChars() returns Java UTF-8 bytes. This is good
  // enough for the linker though.
  const char* bytes = env->GetStringUTFChars(str, nullptr);
  ::memcpy(ptr_, bytes, size_);
  ptr_[size_] = '\0';

  env->ReleaseStringUTFChars(str, bytes);
}

// Find the jclass JNI reference corresponding to a given |class_name|.
// |env| is the current JNI environment handle.
// On success, return true and set |*clazz|.
bool InitClassReference(JNIEnv* env, const char* class_name, jclass* clazz) {
  *clazz = env->FindClass(class_name);
  if (!*clazz) {
    LOG_ERROR("Could not find class for %s", class_name);
    return false;
  }
  return true;
}

// Initialize a jfieldID corresponding to the field of a given |clazz|,
// with name |field_name| and signature |field_sig|.
// |env| is the current JNI environment handle.
// On success, return true and set |*field_id|.
bool InitFieldId(JNIEnv* env,
                 jclass clazz,
                 const char* field_name,
                 const char* field_sig,
                 jfieldID* field_id) {
  *field_id = env->GetFieldID(clazz, field_name, field_sig);
  if (!*field_id) {
    LOG_ERROR("Could not find ID for field '%s'", field_name);
    return false;
  }
  LOG_INFO("Found ID %p for field '%s'", *field_id, field_name);
  return true;
}

// Initialize a jmethodID corresponding to the static method of a given
// |clazz|, with name |method_name| and signature |method_sig|.
// |env| is the current JNI environment handle.
// On success, return true and set |*method_id|.
bool InitStaticMethodId(JNIEnv* env,
                        jclass clazz,
                        const char* method_name,
                        const char* method_sig,
                        jmethodID* method_id) {
  *method_id = env->GetStaticMethodID(clazz, method_name, method_sig);
  if (!*method_id) {
    LOG_ERROR("Could not find ID for static method '%s'", method_name);
    return false;
  }
  LOG_INFO("Found ID %p for static method '%s'", *method_id, method_name);
  return true;
}

// Initialize a jfieldID corresponding to the static field of a given |clazz|,
// with name |field_name| and signature |field_sig|.
// |env| is the current JNI environment handle.
// On success, return true and set |*field_id|.
bool InitStaticFieldId(JNIEnv* env,
                       jclass clazz,
                       const char* field_name,
                       const char* field_sig,
                       jfieldID* field_id) {
  *field_id = env->GetStaticFieldID(clazz, field_name, field_sig);
  if (!*field_id) {
    LOG_ERROR("Could not find ID for static field '%s'", field_name);
    return false;
  }
  LOG_INFO("Found ID %p for static field '%s'", *field_id, field_name);
  return true;
}

// Initialize a jint corresponding to the static integer field of a class
// with class name |class_name| and field name |field_name|.
// |env| is the current JNI environment handle.
// On success, return true and set |*value|.
bool InitStaticInt(JNIEnv* env,
                   const char* class_name,
                   const char* field_name,
                   jint* value) {
  jclass clazz;
  if (!InitClassReference(env, class_name, &clazz))
    return false;

  jfieldID field_id;
  if (!InitStaticFieldId(env, clazz, field_name, "I", &field_id))
    return false;

  *value = env->GetStaticIntField(clazz, field_id);
  LOG_INFO("Found value %d for class '%s', static field '%s'",
           *value, class_name, field_name);

  return true;
}

// Use Android ASLR to create a random address into which we expect to be
// able to load libraries. Note that this is probabilistic; we unmap the
// address we get from mmap and assume we can re-map into it later. This
// works the majority of the time. If it doesn't, client code backs out and
// then loads the library normally at any available address.
// |env| is the current JNI environment handle, and |clazz| a class.
// Returns the address selected by ASLR, or 0 on error.
jlong GetRandomBaseLoadAddress(JNIEnv* env, jclass clazz) {
  size_t bytes = kAddressSpaceReservationSize;

#if RESERVE_BREAKPAD_GUARD_REGION
  // Pad the requested address space size for a Breakpad guard region.
  bytes += kBreakpadGuardRegionBytes;
#endif

  void* address =
      mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (address == MAP_FAILED) {
    LOG_INFO("Random base load address not determinable");
    return 0;
  }
  munmap(address, bytes);

#if RESERVE_BREAKPAD_GUARD_REGION
  // Allow for a Breakpad guard region ahead of the returned address.
  address = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(address) + kBreakpadGuardRegionBytes);
#endif

  LOG_INFO("Random base load address is %p", address);
  return static_cast<jlong>(reinterpret_cast<uintptr_t>(address));
}

namespace {

const JNINativeMethod kNativeMethods[] = {
    {"nativeGetRandomBaseLoadAddress",
     "("
     ")"
     "J",
     reinterpret_cast<void*>(&GetRandomBaseLoadAddress)},
};

const size_t kNumNativeMethods =
    sizeof(kNativeMethods) / sizeof(kNativeMethods[0]);

// JNI_OnLoad() initialization hook.
bool LinkerJNIInit(JavaVM* vm, JNIEnv* env) {
  LOG_INFO("Entering");

  // Register native methods.
  jclass linker_class;
  if (!InitClassReference(env,
                          "org/chromium/base/library_loader/Linker",
                          &linker_class))
    return false;

  LOG_INFO("Registering native methods");
  if (env->RegisterNatives(linker_class, kNativeMethods, kNumNativeMethods) < 0)
    return false;

  // Find LibInfo field ids.
  LOG_INFO("Caching field IDs");
  if (!s_lib_info_fields.Init(env)) {
    return false;
  }

  return true;
}

// JNI_OnLoad() hook called when the linker library is loaded through
// the regular System.LoadLibrary) API. This shall save the Java VM
// handle and initialize LibInfo fields.
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  LOG_INFO("Entering");
  // Get new JNIEnv
  JNIEnv* env;
  if (JNI_OK != vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4)) {
    LOG_ERROR("Could not create JNIEnv");
    return -1;
  }

  // Initialize linker base and implementations.
  if (!LinkerJNIInit(vm, env)
      || !LegacyLinkerJNIInit(vm, env) || !ModernLinkerJNIInit(vm, env)) {
    return -1;
  }

  LOG_INFO("Done");
  return JNI_VERSION_1_4;
}

}  // namespace
}  // namespace chromium_android_linker

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  return chromium_android_linker::JNI_OnLoad(vm, reserved);
}
