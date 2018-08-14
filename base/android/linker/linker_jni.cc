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

#include <android/log.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "build/build_config.h"

#include <crazy_linker.h>

// Set this to 1 to enable debug traces to the Android log.
// Note that LOG() from "base/logging.h" cannot be used, since it is
// in base/ which hasn't been loaded yet.
#define DEBUG 0

#define TAG "cr_ChromiumAndroidLinker"

#if DEBUG
#define LOG_INFO(FORMAT, ...)                                             \
  __android_log_print(ANDROID_LOG_INFO, TAG, "%s: " FORMAT, __FUNCTION__, \
                      ##__VA_ARGS__)
#else
#define LOG_INFO(FORMAT, ...) ((void)0)
#endif
#define LOG_ERROR(FORMAT, ...)                                             \
  __android_log_print(ANDROID_LOG_ERROR, TAG, "%s: " FORMAT, __FUNCTION__, \
                      ##__VA_ARGS__)

#define UNUSED __attribute__((unused))

// See commentary in crazy_linker_elf_loader.cpp for the effect of setting
// this. If changing there, change here also.
//
// For more, see:
//   https://crbug.com/504410
#define RESERVE_BREAKPAD_GUARD_REGION 1

#if defined(ARCH_CPU_X86)
// Dalvik JIT generated code doesn't guarantee 16-byte stack alignment on
// x86 - use force_align_arg_pointer to realign the stack at the JNI
// boundary. https://crbug.com/655248
#define JNI_GENERATOR_EXPORT \
  extern "C" __attribute__((visibility("default"), force_align_arg_pointer))
#else
#define JNI_GENERATOR_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace chromium_android_linker {

namespace {

// Larger than the largest library we might attempt to load.
constexpr size_t kAddressSpaceReservationSize = 192 * 1024 * 1024;

// Size of any Breakpad guard region. 16MB is comfortably larger than the
// ~6MB relocation packing of the current 64-bit libchrome.so, the largest we
// expect to encounter.
#if RESERVE_BREAKPAD_GUARD_REGION
constexpr size_t kBreakpadGuardRegionBytes = 16 * 1024 * 1024;
#endif

// A simple scoped UTF String class that can be initialized from
// a Java jstring handle. Modeled like std::string, which cannot
// be used here.
class String {
 public:
  String(JNIEnv* env, jstring str);

  inline ~String() { ::free(ptr_); }

  inline const char* c_str() const { return ptr_ ? ptr_ : ""; }
  inline size_t size() const { return size_; }

 private:
  char* ptr_;
  size_t size_;
};

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

// A class used to model the field IDs of the org.chromium.base.Linker
// LibInfo inner class, used to communicate data with the Java side
// of the linker.
struct LibInfo_class {
  jfieldID load_address_id;
  jfieldID load_size_id;
  jfieldID relro_start_id;
  jfieldID relro_size_id;
  jfieldID relro_fd_id;

  // Initialize an instance.
  bool Init(JNIEnv* env) {
    jclass clazz;
    if (!InitClassReference(
            env, "org/chromium/base/library_loader/Linker$LibInfo", &clazz)) {
      return false;
    }

    return InitFieldId(env, clazz, "mLoadAddress", "J", &load_address_id) &&
           InitFieldId(env, clazz, "mLoadSize", "J", &load_size_id) &&
           InitFieldId(env, clazz, "mRelroStart", "J", &relro_start_id) &&
           InitFieldId(env, clazz, "mRelroSize", "J", &relro_size_id) &&
           InitFieldId(env, clazz, "mRelroFd", "I", &relro_fd_id);
  }

  void SetLoadInfo(JNIEnv* env,
                   jobject library_info_obj,
                   size_t load_address,
                   size_t load_size) {
    env->SetLongField(library_info_obj, load_address_id, load_address);
    env->SetLongField(library_info_obj, load_size_id, load_size);
  }

  void SetRelroInfo(JNIEnv* env,
                    jobject library_info_obj,
                    size_t relro_start,
                    size_t relro_size,
                    int relro_fd) {
    env->SetLongField(library_info_obj, relro_start_id, relro_start);
    env->SetLongField(library_info_obj, relro_size_id, relro_size);
    env->SetIntField(library_info_obj, relro_fd_id, relro_fd);
  }

  // Use this instance to convert a RelroInfo reference into
  // a crazy_library_info_t.
  void GetRelroInfo(JNIEnv* env,
                    jobject library_info_obj,
                    size_t* relro_start,
                    size_t* relro_size,
                    int* relro_fd) {
    if (relro_start) {
      *relro_start = static_cast<size_t>(
          env->GetLongField(library_info_obj, relro_start_id));
    }

    if (relro_size) {
      *relro_size = static_cast<size_t>(
          env->GetLongField(library_info_obj, relro_size_id));
    }

    if (relro_fd) {
      *relro_fd = env->GetIntField(library_info_obj, relro_fd_id);
    }
  }
};

// Variable containing LibInfo for the loaded library.
LibInfo_class s_lib_info_fields;

// Return true iff |address| is a valid address for the target CPU.
inline bool IsValidAddress(jlong address) {
  return static_cast<jlong>(static_cast<size_t>(address)) == address;
}

// The linker uses a single crazy_context_t object created on demand.
// There is no need to protect this against concurrent access, locking
// is already handled on the Java side.
crazy_context_t* GetCrazyContext() {
  static crazy_context_t* s_crazy_context = nullptr;

  if (!s_crazy_context) {
    // Create new context.
    s_crazy_context = crazy_context_create();

    // Ensure libraries located in the same directory as the linker
    // can be loaded before system ones.
    crazy_context_add_search_path_for_address(
        s_crazy_context, reinterpret_cast<void*>(&GetCrazyContext));
  }

  return s_crazy_context;
}

// A scoped crazy_library_t that automatically closes the handle
// on scope exit, unless Release() has been called.
class ScopedLibrary {
 public:
  ScopedLibrary() : lib_(nullptr) {}

  ~ScopedLibrary() {
    if (lib_)
      crazy_library_close_with_context(lib_, GetCrazyContext());
  }

  crazy_library_t* Get() { return lib_; }

  crazy_library_t** GetPtr() { return &lib_; }

  crazy_library_t* Release() {
    crazy_library_t* ret = lib_;
    lib_ = nullptr;
    return ret;
  }

 private:
  crazy_library_t* lib_;
};

// Retrieve the SDK build version and pass it into the crazy linker. This
// needs to be done early in initialization, before any other crazy linker
// code is run.
// |env| is the current JNI environment handle.
// On success, return true.
bool InitSDKVersionInfo(JNIEnv* env) {
  jint value = 0;
  if (!InitStaticInt(env, "android/os/Build$VERSION", "SDK_INT", &value))
    return false;

  crazy_set_sdk_build_version(static_cast<int>(value));
  LOG_INFO("Set SDK build version to %d", static_cast<int>(value));

  return true;
}

}  // namespace

// Use Android ASLR to create a random address into which we expect to be
// able to load libraries. Note that this is probabilistic; we unmap the
// address we get from mmap and assume we can re-map into it later. This
// works the majority of the time. If it doesn't, client code backs out and
// then loads the library normally at any available address.
// |env| is the current JNI environment handle, and |clazz| a class.
// Returns the address selected by ASLR, or 0 on error.
JNI_GENERATOR_EXPORT jlong
Java_org_chromium_base_library_1loader_Linker_nativeGetRandomBaseLoadAddress(
    JNIEnv* env,
    jclass clazz) {
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

// We identify the abi tag for which the linker is running. This allows
// us to select the library which matches the abi of the linker.

#if defined(__arm__) && defined(__ARM_ARCH_7A__)
#define CURRENT_ABI "armeabi-v7a"
#elif defined(__arm__)
#define CURRENT_ABI "armeabi"
#elif defined(__i386__)
#define CURRENT_ABI "x86"
#elif defined(__mips__)
#define CURRENT_ABI "mips"
#elif defined(__x86_64__)
#define CURRENT_ABI "x86_64"
#elif defined(__aarch64__)
#define CURRENT_ABI "arm64-v8a"
#else
#error "Unsupported target abi"
#endif

// Add a zip archive file path to the context's current search path
// list. Making it possible to load libraries directly from it.
JNI_GENERATOR_EXPORT bool
Java_org_chromium_base_library_1loader_Linker_nativeAddZipArchivePath(
    JNIEnv* env,
    jclass clazz,
    jstring apk_path_obj) {
  String apk_path(env, apk_path_obj);

  char search_path[512];
  snprintf(search_path, sizeof(search_path), "%s!lib/" CURRENT_ABI "/",
           apk_path.c_str());

  crazy_context_t* context = GetCrazyContext();
  crazy_context_add_search_path(context, search_path);
  return true;
}

// Load a library with the chromium linker. This will also call its
// JNI_OnLoad() method, which shall register its methods. Note that
// lazy native method resolution will _not_ work after this, because
// Dalvik uses the system's dlsym() which won't see the new library,
// so explicit registration is mandatory.
//
// |env| is the current JNI environment handle.
// |clazz| is the static class handle for org.chromium.base.Linker,
// and is ignored here.
// |library_name| is the library name (e.g. libfoo.so).
// |load_address| is an explicit load address.
// |lib_info_obj| is a LibInfo handle used to communicate information
// with the Java side.
// Return true on success.
JNI_GENERATOR_EXPORT bool
Java_org_chromium_base_library_1loader_Linker_nativeLoadLibrary(
    JNIEnv* env,
    jclass clazz,
    jstring lib_name_obj,
    jlong load_address,
    jobject lib_info_obj) {
  String library_name(env, lib_name_obj);
  LOG_INFO("Called for %s, at address 0x%llx", library_name, load_address);
  crazy_context_t* context = GetCrazyContext();

  if (!IsValidAddress(load_address)) {
    LOG_ERROR("Invalid address 0x%llx",
              static_cast<unsigned long long>(load_address));
    return false;
  }

  // Set the desired load address (0 means randomize it).
  crazy_context_set_load_address(context, static_cast<size_t>(load_address));

  ScopedLibrary library;
  if (!crazy_library_open(library.GetPtr(), library_name.c_str(), context)) {
    return false;
  }

  crazy_library_info_t info;
  if (!crazy_library_get_info(library.Get(), context, &info)) {
    LOG_ERROR("Could not get library information for %s: %s",
              library_name.c_str(), crazy_context_get_error(context));
    return false;
  }

  // Release library object to keep it alive after the function returns.
  library.Release();

  s_lib_info_fields.SetLoadInfo(env, lib_info_obj, info.load_address,
                                info.load_size);
  LOG_INFO("Success loading library %s", library_name.c_str());
  return true;
}

// Class holding the Java class and method ID for the Java side Linker
// postCallbackOnMainThread method.
struct JavaCallbackBindings_class {
  jclass clazz;
  jmethodID method_id;

  // Initialize an instance.
  bool Init(JNIEnv* env, jclass linker_class) {
    clazz = reinterpret_cast<jclass>(env->NewGlobalRef(linker_class));
    return InitStaticMethodId(env, linker_class, "postCallbackOnMainThread",
                              "(J)V", &method_id);
  }
};

static JavaCallbackBindings_class s_java_callback_bindings;

// Designated receiver function for callbacks from Java. Its name is known
// to the Java side.
// |env| is the current JNI environment handle and is ignored here.
// |clazz| is the static class handle for org.chromium.base.Linker,
// and is ignored here.
// |arg| is a pointer to an allocated crazy_callback_t, deleted after use.
JNI_GENERATOR_EXPORT void
Java_org_chromium_base_library_1loader_Linker_nativeRunCallbackOnUiThread(
    JNIEnv* env,
    jclass clazz,
    jlong arg) {
  crazy_callback_t* callback = reinterpret_cast<crazy_callback_t*>(arg);

  LOG_INFO("Called back from java with handler %p, opaque %p",
           callback->handler, callback->opaque);

  crazy_callback_run(callback);
  delete callback;
}

// Request a callback from Java. The supplied crazy_callback_t is valid only
// for the duration of this call, so we copy it to a newly allocated
// crazy_callback_t and then call the Java side's postCallbackOnMainThread.
// This will call back to to our RunCallbackOnUiThread some time
// later on the UI thread.
// |callback_request| is a crazy_callback_t.
// |poster_opaque| is unused.
// Returns true if the callback request succeeds.
static bool PostForLaterExecution(crazy_callback_t* callback_request,
                                  void* poster_opaque UNUSED) {
  crazy_context_t* context = GetCrazyContext();

  JavaVM* vm;
  int minimum_jni_version;
  crazy_context_get_java_vm(context, reinterpret_cast<void**>(&vm),
                            &minimum_jni_version);

  // Do not reuse JNIEnv from JNI_OnLoad, but retrieve our own.
  JNIEnv* env;
  if (JNI_OK !=
      vm->GetEnv(reinterpret_cast<void**>(&env), minimum_jni_version)) {
    LOG_ERROR("Could not create JNIEnv");
    return false;
  }

  // Copy the callback; the one passed as an argument may be temporary.
  crazy_callback_t* callback = new crazy_callback_t();
  *callback = *callback_request;

  LOG_INFO("Calling back to java with handler %p, opaque %p", callback->handler,
           callback->opaque);

  jlong arg = static_cast<jlong>(reinterpret_cast<uintptr_t>(callback));

  env->CallStaticVoidMethod(s_java_callback_bindings.clazz,
                            s_java_callback_bindings.method_id, arg);

  // Back out and return false if we encounter a JNI exception.
  if (env->ExceptionCheck() == JNI_TRUE) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    delete callback;
    return false;
  }

  return true;
}

JNI_GENERATOR_EXPORT jboolean
Java_org_chromium_base_library_1loader_Linker_nativeCreateSharedRelro(
    JNIEnv* env,
    jclass clazz,
    jstring library_name,
    jlong load_address,
    jobject lib_info_obj) {
  String lib_name(env, library_name);

  LOG_INFO("Called for %s", lib_name.c_str());

  if (!IsValidAddress(load_address)) {
    LOG_ERROR("Invalid address 0x%llx",
              static_cast<unsigned long long>(load_address));
    return false;
  }

  ScopedLibrary library;
  if (!crazy_library_find_by_name(lib_name.c_str(), library.GetPtr())) {
    LOG_ERROR("Could not find %s", lib_name.c_str());
    return false;
  }

  crazy_context_t* context = GetCrazyContext();
  size_t relro_start = 0;
  size_t relro_size = 0;
  int relro_fd = -1;

  if (!crazy_library_create_shared_relro(
          library.Get(), context, static_cast<size_t>(load_address),
          &relro_start, &relro_size, &relro_fd)) {
    LOG_ERROR("Could not create shared RELRO sharing for %s: %s\n",
              lib_name.c_str(), crazy_context_get_error(context));
    return false;
  }

  s_lib_info_fields.SetRelroInfo(env, lib_info_obj, relro_start, relro_size,
                                 relro_fd);
  return true;
}

JNI_GENERATOR_EXPORT jboolean
Java_org_chromium_base_library_1loader_Linker_nativeUseSharedRelro(
    JNIEnv* env,
    jclass clazz,
    jstring library_name,
    jobject lib_info_obj) {
  String lib_name(env, library_name);

  LOG_INFO("Called for %s, lib_info_ref=%p", lib_name.c_str(), lib_info_obj);

  ScopedLibrary library;
  if (!crazy_library_find_by_name(lib_name.c_str(), library.GetPtr())) {
    LOG_ERROR("Could not find %s", lib_name.c_str());
    return false;
  }

  crazy_context_t* context = GetCrazyContext();
  size_t relro_start = 0;
  size_t relro_size = 0;
  int relro_fd = -1;
  s_lib_info_fields.GetRelroInfo(env, lib_info_obj, &relro_start, &relro_size,
                                 &relro_fd);

  LOG_INFO("library=%s relro start=%p size=%p fd=%d", lib_name.c_str(),
           (void*)relro_start, (void*)relro_size, relro_fd);

  if (!crazy_library_use_shared_relro(library.Get(), context, relro_start,
                                      relro_size, relro_fd)) {
    LOG_ERROR("Could not use shared RELRO for %s: %s", lib_name.c_str(),
              crazy_context_get_error(context));
    return false;
  }

  LOG_INFO("Library %s using shared RELRO section!", lib_name.c_str());

  return true;
}

static bool LinkerJNIInit(JavaVM* vm, JNIEnv* env) {
  LOG_INFO("Entering");

  // Initialize SDK version info.
  LOG_INFO("Retrieving SDK version info");
  if (!InitSDKVersionInfo(env))
    return false;

  // Find LibInfo field ids.
  LOG_INFO("Caching field IDs");
  if (!s_lib_info_fields.Init(env)) {
    return false;
  }

  // Register native methods.
  jclass linker_class;
  if (!InitClassReference(env, "org/chromium/base/library_loader/Linker",
                          &linker_class))
    return false;

  // Resolve and save the Java side Linker callback class and method.
  LOG_INFO("Resolving callback bindings");
  if (!s_java_callback_bindings.Init(env, linker_class)) {
    return false;
  }

  // Save JavaVM* handle into context.
  crazy_context_t* context = GetCrazyContext();
  crazy_context_set_java_vm(context, vm, JNI_VERSION_1_4);

  // Register the function that the crazy linker can call to post code
  // for later execution.
  crazy_context_set_callback_poster(context, &PostForLaterExecution, nullptr);

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
  if (!LinkerJNIInit(vm, env)) {
    return -1;
  }

  LOG_INFO("Done");
  return JNI_VERSION_1_4;
}

}  // namespace chromium_android_linker

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  return chromium_android_linker::JNI_OnLoad(vm, reserved);
}
