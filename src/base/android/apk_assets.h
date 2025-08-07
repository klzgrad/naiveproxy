// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_APK_ASSETS_H_
#define BASE_ANDROID_APK_ASSETS_H_

#include <string>

#include "base/android/jni_android.h"
#include "base/files/file_path.h"
#include "base/files/memory_mapped_file.h"

namespace base {
namespace android {

// Opens an asset (e.g. a .pak file) from the apk.
// Can be used from renderer process.
// Fails if the asset is not stored uncompressed within the .apk.
// Returns: The File Descriptor of the asset, or -1 upon failure.
// Input arguments:
// - |file_path|: Path to file within .apk. e.g.: assets/foo.pak
// - |split_name|: If non-empty, refers to the split where the asset is located.
// Output arguments:
// - |region|: size & offset (in bytes) within the .apk of the asset.
BASE_EXPORT int OpenApkAsset(const std::string& file_path,
                             const std::string& split_name,
                             base::MemoryMappedFile::Region* region);
BASE_EXPORT int OpenApkAsset(const std::string& file_path,
                             base::MemoryMappedFile::Region* region);

// Registers an uncompressed asset from within the apk in the
// FileDescriptorStore.
// Returns: true in case of success, false otherwise.
BASE_EXPORT bool RegisterApkAssetWithFileDescriptorStore(
    const std::string& key,
    const base::FilePath& file_path);

// If one of the above methods failed, call this to perform a
// `DumpWithoutCrashing` containing errors relevant to the failure.
BASE_EXPORT void DumpLastOpenApkAssetFailure();

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_APK_ASSETS_H_
