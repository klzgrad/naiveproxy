// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_PATH_UTILS_H_
#define BASE_ANDROID_PATH_UTILS_H_

#include <jni.h>

#include <vector>

#include "base/base_export.h"

namespace base {

class FilePath;

namespace android {

// Retrieves the absolute path to the data directory of the current
// application. The result is placed in the FilePath pointed to by 'result'.
// This method is dedicated for base_paths_android.c, Using
// PathService::Get(base::DIR_ANDROID_APP_DATA, ...) gets the data dir.
BASE_EXPORT bool GetDataDirectory(FilePath* result);

// Retrieves the absolute path to the cache directory. The result is placed in
// the FilePath pointed to by 'result'. This method is dedicated for
// base_paths_android.c, Using PathService::Get(base::DIR_CACHE, ...) gets the
// cache dir.
BASE_EXPORT bool GetCacheDirectory(FilePath* result);

// Retrieves the path to the thumbnail cache directory. The result is placed
// in the FilePath pointed to by 'result'.
BASE_EXPORT bool GetThumbnailCacheDirectory(FilePath* result);

// Retrieves the path to the public downloads directory. The result is placed
// in the FilePath pointed to by 'result'.
BASE_EXPORT bool GetDownloadsDirectory(FilePath* result);

// Retrieves the paths to all download directories, including default storage
// directory, and a private directory on external SD card.
BASE_EXPORT std::vector<FilePath> GetAllPrivateDownloadsDirectories();

// Retrieves the paths to all secondary storage download directories. e.g.
// /storage/1AEF-1A1E/Download/.
BASE_EXPORT std::vector<FilePath> GetSecondaryStorageDownloadDirectories();

// Retrieves the path to the native JNI libraries via
// ApplicationInfo.nativeLibraryDir on the Java side. The result is placed in
// the FilePath pointed to by 'result'.
BASE_EXPORT bool GetNativeLibraryDirectory(FilePath* result);

// Retrieves the absolute path to the external storage directory. The result
// is placed in the FilePath pointed to by 'result'.
BASE_EXPORT bool GetExternalStorageDirectory(FilePath* result);

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_PATH_UTILS_H_
