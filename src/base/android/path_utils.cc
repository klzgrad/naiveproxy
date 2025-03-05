// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/path_utils.h"

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/files/file_path.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/base_jni/PathUtils_jni.h"

namespace base {
namespace android {

bool GetDataDirectory(FilePath* result) {
  JNIEnv* env = AttachCurrentThread();
  std::string path = Java_PathUtils_getDataDirectory(env);
  FilePath data_path(path);
  *result = data_path;
  return true;
}

bool GetCacheDirectory(FilePath* result) {
  JNIEnv* env = AttachCurrentThread();
  std::string path = Java_PathUtils_getCacheDirectory(env);
  FilePath cache_path(path);
  *result = cache_path;
  return true;
}

bool GetThumbnailCacheDirectory(FilePath* result) {
  JNIEnv* env = AttachCurrentThread();
  std::string path = Java_PathUtils_getThumbnailCacheDirectory(env);
  FilePath thumbnail_cache_path(path);
  *result = thumbnail_cache_path;
  return true;
}

bool GetDownloadsDirectory(FilePath* result) {
  JNIEnv* env = AttachCurrentThread();
  std::string path = Java_PathUtils_getDownloadsDirectory(env);
  FilePath downloads_path(path);
  *result = downloads_path;
  return true;
}

std::vector<FilePath> GetAllPrivateDownloadsDirectories() {
  std::vector<std::string> dirs;
  JNIEnv* env = AttachCurrentThread();
  auto jarray = Java_PathUtils_getAllPrivateDownloadsDirectories(env);
  base::android::AppendJavaStringArrayToStringVector(env, jarray, &dirs);

  std::vector<base::FilePath> file_paths;
  for (const auto& dir : dirs) {
    file_paths.emplace_back(dir);
  }
  return file_paths;
}

std::vector<FilePath> GetSecondaryStorageDownloadDirectories() {
  std::vector<std::string> dirs;
  JNIEnv* env = AttachCurrentThread();
  auto jarray = Java_PathUtils_getExternalDownloadVolumesNames(env);
  base::android::AppendJavaStringArrayToStringVector(env, jarray, &dirs);

  std::vector<base::FilePath> file_paths;
  for (const auto& dir : dirs) {
    file_paths.emplace_back(dir);
  }
  return file_paths;
}

bool GetNativeLibraryDirectory(FilePath* result) {
  JNIEnv* env = AttachCurrentThread();
  std::string path = Java_PathUtils_getNativeLibraryDirectory(env);
  FilePath library_path(path);
  *result = library_path;
  return true;
}

bool GetExternalStorageDirectory(FilePath* result) {
  JNIEnv* env = AttachCurrentThread();
  std::string path = Java_PathUtils_getExternalStorageDirectory(env);
  FilePath storage_path(path);
  *result = storage_path;
  return true;
}

}  // namespace android
}  // namespace base
