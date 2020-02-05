// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/cache_util.h"

#include <limits>

#include "base/bind.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"

namespace {

const int kMaxOldFolders = 100;

// Returns a fully qualified name from path and name, using a given name prefix
// and index number. For instance, if the arguments are "/foo", "bar" and 5, it
// will return "/foo/old_bar_005".
base::FilePath GetPrefixedName(const base::FilePath& path,
                               const std::string& name,
                               int index) {
  std::string tmp = base::StringPrintf("%s%s_%03d", "old_",
                                       name.c_str(), index);
  return path.AppendASCII(tmp);
}

// This is a simple callback to cleanup old caches.
void CleanupCallback(const base::FilePath& path, const std::string& name) {
  for (int i = 0; i < kMaxOldFolders; i++) {
    base::FilePath to_delete = GetPrefixedName(path, name, i);
    disk_cache::DeleteCache(to_delete, true);
  }
}

// Returns a full path to rename the current cache, in order to delete it. path
// is the current folder location, and name is the current folder name.
base::FilePath GetTempCacheName(const base::FilePath& path,
                                const std::string& name) {
  // We'll attempt to have up to kMaxOldFolders folders for deletion.
  for (int i = 0; i < kMaxOldFolders; i++) {
    base::FilePath to_delete = GetPrefixedName(path, name, i);
    if (!base::PathExists(to_delete))
      return to_delete;
  }
  return base::FilePath();
}

int64_t PreferredCacheSizeInternal(int64_t available) {
  using disk_cache::kDefaultCacheSize;
  // Return 80% of the available space if there is not enough space to use
  // kDefaultCacheSize.
  if (available < kDefaultCacheSize * 10 / 8)
    return available * 8 / 10;

  // Return kDefaultCacheSize if it uses 10% to 80% of the available space.
  if (available < kDefaultCacheSize * 10)
    return kDefaultCacheSize;

  // Return 10% of the available space if the target size
  // (2.5 * kDefaultCacheSize) is more than 10%.
  if (available < static_cast<int64_t>(kDefaultCacheSize) * 25)
    return available / 10;

  // Return the target size (2.5 * kDefaultCacheSize) if it uses 10% to 1%
  // of the available space.
  if (available < static_cast<int64_t>(kDefaultCacheSize) * 250)
    return kDefaultCacheSize * 5 / 2;

  // Return 1% of the available space.
  return available / 100;
}

}  // namespace

namespace disk_cache {

const int kDefaultCacheSize = 80 * 1024 * 1024;

void DeleteCache(const base::FilePath& path, bool remove_folder) {
  if (remove_folder) {
    if (!base::DeleteFile(path, /* recursive */ true))
      LOG(WARNING) << "Unable to delete cache folder.";
    return;
  }

  base::FileEnumerator iter(
      path,
      /* recursive */ false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath file = iter.Next(); !file.value().empty();
       file = iter.Next()) {
    if (!base::DeleteFile(file, /* recursive */ true)) {
      LOG(WARNING) << "Unable to delete cache.";
      return;
    }
  }
}

// In order to process a potentially large number of files, we'll rename the
// cache directory to old_ + original_name + number, (located on the same parent
// directory), and use a worker thread to delete all the files on all the stale
// cache directories. The whole process can still fail if we are not able to
// rename the cache directory (for instance due to a sharing violation), and in
// that case a cache for this profile (on the desired path) cannot be created.
bool DelayedCacheCleanup(const base::FilePath& full_path) {
  // GetTempCacheName() and MoveCache() use synchronous file
  // operations.
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  base::FilePath current_path = full_path.StripTrailingSeparators();

  base::FilePath path = current_path.DirName();
  base::FilePath name = current_path.BaseName();
#if defined(OS_WIN)
  // We created this file so it should only contain ASCII.
  std::string name_str = base::UTF16ToASCII(name.value());
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
  std::string name_str = name.value();
#endif

  base::FilePath to_delete = GetTempCacheName(path, name_str);
  if (to_delete.empty()) {
    LOG(ERROR) << "Unable to get another cache folder";
    return false;
  }

  if (!disk_cache::MoveCache(full_path, to_delete)) {
    LOG(ERROR) << "Unable to move cache folder " << full_path.value() << " to "
               << to_delete.value();
    return false;
  }

  base::PostTask(
      FROM_HERE,
      {base::ThreadPool(), base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&CleanupCallback, path, name_str));
  return true;
}

// Returns the preferred maximum number of bytes for the cache given the
// number of available bytes.
int PreferredCacheSize(int64_t available, net::CacheType type) {
  if (available < 0)
    return kDefaultCacheSize;

  int64_t preferred_cache_size = PreferredCacheSizeInternal(available);

  // Limit cache size to somewhat less than kint32max to avoid potential
  // integer overflows in cache backend implementations.
  //
  // Note: the 4x limit is of course far below that; historically it came
  // from the blockfile backend with the following explanation:
  // "Let's not use more than the default size while we tune-up the performance
  // of bigger caches. "
  int64_t size_limit = static_cast<int64_t>(kDefaultCacheSize) * 4;
  // Native code entries can be large, so we would like a larger cache.
  // Make the size limit 50% larger in that case.
  if (type == net::GENERATED_NATIVE_CODE_CACHE) {
    size_limit = (size_limit / 2) * 3;
  }

  DCHECK_LT(size_limit, std::numeric_limits<int32_t>::max());
  return static_cast<int32_t>(std::min(preferred_cache_size, size_limit));
}

}  // namespace disk_cache
