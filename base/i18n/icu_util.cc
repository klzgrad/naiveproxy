// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/icu_util.h"

#if defined(OS_WIN)
#include <windows.h>
#endif

#include <string>

#include "base/debug/alias.h"
#include "base/files/file_path.h"
#include "base/files/memory_mapped_file.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "build/build_config.h"
#include "third_party/icu/source/common/unicode/putil.h"
#include "third_party/icu/source/common/unicode/udata.h"
#if (defined(OS_LINUX) && !defined(OS_CHROMEOS)) || defined(OS_ANDROID)
#include "third_party/icu/source/i18n/unicode/timezone.h"
#endif

#if defined(OS_ANDROID)
#include "base/android/apk_assets.h"
#include "base/android/timezone_utils.h"
#endif

#if defined(OS_IOS)
#include "base/ios/ios_util.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/foundation_util.h"
#endif

#if defined(OS_FUCHSIA)
#include "base/base_paths_fuchsia.h"
#endif

namespace base {
namespace i18n {

#if ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_SHARED
#define ICU_UTIL_DATA_SYMBOL "icudt" U_ICU_VERSION_SHORT "_dat"
#if defined(OS_WIN)
#define ICU_UTIL_DATA_SHARED_MODULE_NAME "icudt.dll"
#endif
#endif

namespace {
#if !defined(OS_NACL)
#if DCHECK_IS_ON()
// Assert that we are not called more than once.  Even though calling this
// function isn't harmful (ICU can handle it), being called twice probably
// indicates a programming error.
bool g_check_called_once = true;
bool g_called_once = false;
#endif  // DCHECK_IS_ON()

#if ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE

// Use an unversioned file name to simplify a icu version update down the road.
// No need to change the filename in multiple places (gyp files, windows
// build pkg configurations, etc). 'l' stands for Little Endian.
// This variable is exported through the header file.
const char kIcuDataFileName[] = "icudtl.dat";
#if defined(OS_ANDROID)
const char kAndroidAssetsIcuDataFileName[] = "assets/icudtl.dat";
#endif

// File handle intentionally never closed. Not using File here because its
// Windows implementation guards against two instances owning the same
// PlatformFile (which we allow since we know it is never freed).
PlatformFile g_icudtl_pf = kInvalidPlatformFile;
MemoryMappedFile* g_icudtl_mapped_file = nullptr;
MemoryMappedFile::Region g_icudtl_region;

void LazyInitIcuDataFile() {
  if (g_icudtl_pf != kInvalidPlatformFile) {
    return;
  }
#if defined(OS_ANDROID)
  int fd = base::android::OpenApkAsset(kAndroidAssetsIcuDataFileName,
                                       &g_icudtl_region);
  g_icudtl_pf = fd;
  if (fd != -1) {
    return;
  }
// For unit tests, data file is located on disk, so try there as a fallback.
#endif  // defined(OS_ANDROID)
#if !defined(OS_MACOSX)
  FilePath data_path;
  if (!PathService::Get(DIR_ASSETS, &data_path)) {
    LOG(ERROR) << "Can't find " << kIcuDataFileName;
    return;
  }
  data_path = data_path.AppendASCII(kIcuDataFileName);
#else
  // Assume it is in the framework bundle's Resources directory.
  ScopedCFTypeRef<CFStringRef> data_file_name(
      SysUTF8ToCFStringRef(kIcuDataFileName));
  FilePath data_path = mac::PathForFrameworkBundleResource(data_file_name);
#if defined(OS_IOS)
  FilePath override_data_path = base::ios::FilePathOfEmbeddedICU();
  if (!override_data_path.empty()) {
    data_path = override_data_path;
  }
#endif  // !defined(OS_IOS)
  if (data_path.empty()) {
    LOG(ERROR) << kIcuDataFileName << " not found in bundle";
    return;
  }
#endif  // !defined(OS_MACOSX)
  File file(data_path, File::FLAG_OPEN | File::FLAG_READ);
  if (file.IsValid()) {
    g_icudtl_pf = file.TakePlatformFile();
    g_icudtl_region = MemoryMappedFile::Region::kWholeFile;
  }
}

bool InitializeICUWithFileDescriptorInternal(
    PlatformFile data_fd,
    const MemoryMappedFile::Region& data_region) {
  // This can be called multiple times in tests.
  if (g_icudtl_mapped_file) {
    return true;
  }
  if (data_fd == kInvalidPlatformFile) {
    LOG(ERROR) << "Invalid file descriptor to ICU data received.";
    return false;
  }

  std::unique_ptr<MemoryMappedFile> icudtl_mapped_file(new MemoryMappedFile());
  if (!icudtl_mapped_file->Initialize(File(data_fd), data_region)) {
    LOG(ERROR) << "Couldn't mmap icu data file";
    return false;
  }
  g_icudtl_mapped_file = icudtl_mapped_file.release();

  UErrorCode err = U_ZERO_ERROR;
  udata_setCommonData(const_cast<uint8_t*>(g_icudtl_mapped_file->data()), &err);
#if defined(OS_ANDROID)
  if (err == U_ZERO_ERROR) {
    // On Android, we can't leave it up to ICU to set the default timezone
    // because ICU's timezone detection does not work in many timezones (e.g.
    // Australia/Sydney, Asia/Seoul, Europe/Paris ). Use JNI to detect the host
    // timezone and set the ICU default timezone accordingly in advance of
    // actual use. See crbug.com/722821 and
    // https://ssl.icu-project.org/trac/ticket/13208 .
    base::string16 timezone_id = base::android::GetDefaultTimeZoneId();
    icu::TimeZone::adoptDefault(icu::TimeZone::createTimeZone(
        icu::UnicodeString(FALSE, timezone_id.data(), timezone_id.length())));
  }
#endif
  // Never try to load ICU data from files.
  udata_setFileAccess(UDATA_ONLY_PACKAGES, &err);
  return err == U_ZERO_ERROR;
}
#endif  // ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE
#endif  // !defined(OS_NACL)

}  // namespace

#if !defined(OS_NACL)
#if ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE
#if defined(OS_ANDROID)
bool InitializeICUWithFileDescriptor(
    PlatformFile data_fd,
    const MemoryMappedFile::Region& data_region) {
#if DCHECK_IS_ON()
  DCHECK(!g_check_called_once || !g_called_once);
  g_called_once = true;
#endif
  return InitializeICUWithFileDescriptorInternal(data_fd, data_region);
}

PlatformFile GetIcuDataFileHandle(MemoryMappedFile::Region* out_region) {
  CHECK_NE(g_icudtl_pf, kInvalidPlatformFile);
  *out_region = g_icudtl_region;
  return g_icudtl_pf;
}
#endif

const uint8_t* GetRawIcuMemory() {
  CHECK(g_icudtl_mapped_file);
  return g_icudtl_mapped_file->data();
}

bool InitializeICUFromRawMemory(const uint8_t* raw_memory) {
#if !defined(COMPONENT_BUILD)
#if DCHECK_IS_ON()
  DCHECK(!g_check_called_once || !g_called_once);
  g_called_once = true;
#endif

  UErrorCode err = U_ZERO_ERROR;
  udata_setCommonData(const_cast<uint8_t*>(raw_memory), &err);
  // Never try to load ICU data from files.
  udata_setFileAccess(UDATA_ONLY_PACKAGES, &err);
  return err == U_ZERO_ERROR;
#else
  return true;
#endif
}

#endif  // ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE

bool InitializeICU() {
#if DCHECK_IS_ON()
  DCHECK(!g_check_called_once || !g_called_once);
  g_called_once = true;
#endif

  bool result;
#if (ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_SHARED)
  FilePath data_path;
  PathService::Get(DIR_ASSETS, &data_path);
  data_path = data_path.AppendASCII(ICU_UTIL_DATA_SHARED_MODULE_NAME);

  HMODULE module = LoadLibrary(data_path.value().c_str());
  if (!module) {
    LOG(ERROR) << "Failed to load " << ICU_UTIL_DATA_SHARED_MODULE_NAME;
    return false;
  }

  FARPROC addr = GetProcAddress(module, ICU_UTIL_DATA_SYMBOL);
  if (!addr) {
    LOG(ERROR) << ICU_UTIL_DATA_SYMBOL << ": not found in "
               << ICU_UTIL_DATA_SHARED_MODULE_NAME;
    return false;
  }

  UErrorCode err = U_ZERO_ERROR;
  udata_setCommonData(reinterpret_cast<void*>(addr), &err);
  // Never try to load ICU data from files.
  udata_setFileAccess(UDATA_ONLY_PACKAGES, &err);
  result = (err == U_ZERO_ERROR);
#elif (ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_STATIC)
  // The ICU data is statically linked.
  result = true;
#elif (ICU_UTIL_DATA_IMPL == ICU_UTIL_DATA_FILE)
  // If the ICU data directory is set, ICU won't actually load the data until
  // it is needed.  This can fail if the process is sandboxed at that time.
  // Instead, we map the file in and hand off the data so the sandbox won't
  // cause any problems.
  LazyInitIcuDataFile();
  result =
      InitializeICUWithFileDescriptorInternal(g_icudtl_pf, g_icudtl_region);
#endif

// To respond to the timezone change properly, the default timezone
// cache in ICU has to be populated on starting up.
// TODO(jungshik): Some callers do not care about tz at all. If necessary,
// add a boolean argument to this function to init'd the default tz only
// when requested.
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  if (result)
    std::unique_ptr<icu::TimeZone> zone(icu::TimeZone::createDefault());
#endif
  return result;
}
#endif  // !defined(OS_NACL)

void AllowMultipleInitializeCallsForTesting() {
#if DCHECK_IS_ON() && !defined(OS_NACL)
  g_check_called_once = false;
#endif
}

}  // namespace i18n
}  // namespace base
