// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/shared_preferences/shared_preferences_manager.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/check.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/base_shared_preferences_jni/SharedPreferencesManager_jni.h"

namespace base::android {

SharedPreferencesManager::SharedPreferencesManager(const JavaRef<jobject>& jobj,
                                                   JNIEnv* env)
    : java_obj_(jobj), env_(env) {}

SharedPreferencesManager::SharedPreferencesManager(
    const SharedPreferencesManager& other)
    : java_obj_(other.java_obj_), env_(other.env_) {}

SharedPreferencesManager::~SharedPreferencesManager() = default;

void SharedPreferencesManager::RemoveKey(
    const std::string& shared_preference_key) {
  Java_SharedPreferencesManager_removeKey(env_, java_obj_,
                                          shared_preference_key);
}

bool SharedPreferencesManager::ContainsKey(
    const std::string& shared_preference_key) {
  return Java_SharedPreferencesManager_contains(env_, java_obj_,
                                                shared_preference_key);
}

bool SharedPreferencesManager::ReadBoolean(
    const std::string& shared_preference_key,
    bool default_value) {
  return Java_SharedPreferencesManager_readBoolean(
      env_, java_obj_, shared_preference_key, default_value);
}

int SharedPreferencesManager::ReadInt(const std::string& shared_preference_key,
                                      int default_value) {
  return Java_SharedPreferencesManager_readInt(
      env_, java_obj_, shared_preference_key, default_value);
}

std::string SharedPreferencesManager::ReadString(
    const std::string& shared_preference_key,
    const std::string& default_value) {
  return Java_SharedPreferencesManager_readString(
      env_, java_obj_, shared_preference_key, default_value);
}

void SharedPreferencesManager::WriteString(
    const std::string& shared_preference_key,
    const std::string& value) {
  Java_SharedPreferencesManager_writeString(env_, java_obj_,
                                            shared_preference_key, value);
}

}  // namespace base::android
