// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_utils.h"

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/containers/flat_map.h"
#include "base/lazy_instance.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/base_minimal_jni/JNIUtils_jni.h"

namespace base {
namespace android {

namespace {
struct LockAndMap {
  base::Lock lock;
  base::flat_map<const char*, ScopedJavaGlobalRef<jobject>> map;
};
LockAndMap* GetLockAndMap() {
  static base::NoDestructor<LockAndMap> lock_and_map;
  return lock_and_map.get();
}
}  // namespace

jobject GetSplitClassLoader(JNIEnv* env, const char* split_name) {
  LockAndMap* lock_and_map = GetLockAndMap();
  base::AutoLock guard(lock_and_map->lock);
  auto it = lock_and_map->map.find(split_name);
  if (it != lock_and_map->map.end()) {
    return it->second.obj();
  }

  ScopedJavaGlobalRef<jobject> class_loader(
      env, Java_JNIUtils_getSplitClassLoader(env, split_name));
  jobject class_loader_obj = class_loader.obj();
  lock_and_map->map.insert({split_name, std::move(class_loader)});
  return class_loader_obj;
}

}  // namespace android
}  // namespace base

DEFINE_JNI_FOR_JNIUtils()
