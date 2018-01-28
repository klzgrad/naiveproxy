// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/simple/simple_version_upgrade.h"

#include <cstring>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/logging.h"
#include "base/pickle.h"
#include "net/disk_cache/simple/simple_backend_version.h"
#include "net/disk_cache/simple/simple_entry_format_history.h"
#include "third_party/zlib/zlib.h"

namespace {

// It is not possible to upgrade cache structures on disk that are of version
// below this, the entire cache should be dropped for them.
const uint32_t kMinVersionAbleToUpgrade = 5;

const char kFakeIndexFileName[] = "index";
const char kIndexFileName[] = "the-real-index";

void LogMessageFailedUpgradeFromVersion(int version) {
  LOG(ERROR) << "Failed to upgrade Simple Cache from version: " << version;
}

bool WriteFakeIndexFile(const base::FilePath& file_name,
                        const disk_cache::SimpleExperiment& experiment) {
  base::File file(file_name, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  if (!file.IsValid())
    return false;

  disk_cache::FakeIndexData file_contents;
  file_contents.initial_magic_number =
      disk_cache::simplecache_v5::kSimpleInitialMagicNumber;
  file_contents.version = disk_cache::kSimpleVersion;
  file_contents.experiment_type = experiment.type;
  file_contents.experiment_param = experiment.param;

  int bytes_written = file.Write(0, reinterpret_cast<char*>(&file_contents),
                                 sizeof(file_contents));
  if (bytes_written != sizeof(file_contents)) {
    LOG(ERROR) << "Failed to write fake index file: "
               << file_name.LossyDisplayName();
    return false;
  }
  return true;
}

bool SimpleExperimentMatches(const disk_cache::FakeIndexData& index_data,
                             const disk_cache::SimpleExperiment& experiment) {
  return index_data.experiment_type == experiment.type &&
         index_data.experiment_param == experiment.param;
}

}  // namespace

namespace disk_cache {

FakeIndexData::FakeIndexData() {
  // Make hashing repeatable: leave no padding bytes untouched.
  std::memset(this, 0, sizeof(*this));
}

// Migrates the cache directory from version 4 to version 5.
// Returns true iff it succeeds.
//
// The V5 and V6 caches differ in the name of the index file (it moved to a
// subdirectory) and in the file format (directory last-modified time observed
// by the index writer has gotten appended to the pickled format).
//
// To keep complexity small this specific upgrade code *deletes* the old index
// file. The directory for the new index file has to be created lazily anyway,
// so it is not done in the upgrader.
//
// Below is the detailed description of index file format differences. It is for
// reference purposes. This documentation would be useful to move closer to the
// next index upgrader when the latter gets introduced.
//
// Path:
//   V5: $cachedir/the-real-index
//   V6: $cachedir/index-dir/the-real-index
//
// Pickled file format:
//   Both formats extend Pickle::Header by 32bit value of the CRC-32 of the
//   pickled data.
//   <v5-index> ::= <v5-index-metadata> <entry-info>*
//   <v5-index-metadata> ::= UInt64(kSimpleIndexMagicNumber)
//                           UInt32(4)
//                           UInt64(<number-of-entries>)
//                           UInt64(<cache-size-in-bytes>)
//   <entry-info> ::= UInt64(<hash-of-the-key>)
//                    Int64(<entry-last-used-time>)
//                    UInt64(<entry-size-in-bytes>)
//   <v6-index> ::= <v6-index-metadata>
//                  <entry-info>*
//                  Int64(<cache-dir-mtime>)
//   <v6-index-metadata> ::= UInt64(kSimpleIndexMagicNumber)
//                           UInt32(5)
//                           UInt64(<number-of-entries>)
//                           UInt64(<cache-size-in-bytes>)
//   Where:
//     <entry-size-in-bytes> is equal the sum of all file sizes of the entry.
//     <cache-dir-mtime> is the last modification time with nanosecond precision
//       of the directory, where all files for entries are stored.
//     <hash-of-the-key> represent the first 64 bits of a SHA-1 of the key.
bool UpgradeIndexV5V6(const base::FilePath& cache_directory) {
  const base::FilePath old_index_file =
      cache_directory.AppendASCII(kIndexFileName);
  if (!base::DeleteFile(old_index_file, /* recursive = */ false))
    return false;
  return true;
}

// Some points about the Upgrade process are still not clear:
// 1. if the upgrade path requires dropping cache it would be faster to just
//    return an initialization error here and proceed with asynchronous cache
//    cleanup in CacheCreator. Should this hack be considered valid? Some smart
//    tests may fail.
// 2. Because Android process management allows for killing a process at any
//    time, the upgrade process may need to deal with a partially completed
//    previous upgrade. For example, while upgrading A -> A + 2 we are the
//    process gets killed and some parts are remaining at version A + 1. There
//    are currently no generic mechanisms to resolve this situation, co the
//    upgrade codes need to ensure they can continue after being stopped in the
//    middle. It also means that the "fake index" must be flushed in between the
//    upgrade steps. Atomicity of this is an interesting research topic. The
//    intermediate fake index flushing must be added as soon as we add more
//    upgrade steps.
bool UpgradeSimpleCacheOnDisk(const base::FilePath& path,
                              const SimpleExperiment& experiment) {
  // There is a convention among disk cache backends: looking at the magic in
  // the file "index" it should be sufficient to determine if the cache belongs
  // to the currently running backend. The Simple Backend stores its index in
  // the file "the-real-index" (see simple_index_file.cc) and the file "index"
  // only signifies presence of the implementation's magic and version. There
  // are two reasons for that:
  // 1. Absence of the index is itself not a fatal error in the Simple Backend
  // 2. The Simple Backend has pickled file format for the index making it hacky
  //    to have the magic in the right place.
  const base::FilePath fake_index = path.AppendASCII(kFakeIndexFileName);
  base::File fake_index_file(fake_index,
                             base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (!fake_index_file.IsValid()) {
    if (fake_index_file.error_details() == base::File::FILE_ERROR_NOT_FOUND) {
      return WriteFakeIndexFile(fake_index, experiment);
    }
    return false;
  }

  FakeIndexData file_header;
  int bytes_read = fake_index_file.Read(0,
                                        reinterpret_cast<char*>(&file_header),
                                        sizeof(file_header));
  if (bytes_read != sizeof(file_header) ||
      file_header.initial_magic_number !=
          disk_cache::simplecache_v5::kSimpleInitialMagicNumber) {
    LOG(ERROR) << "File structure does not match the disk cache backend.";
    return false;
  }
  fake_index_file.Close();

  uint32_t version_from = file_header.version;
  if (version_from < kMinVersionAbleToUpgrade ||
      version_from > kSimpleVersion) {
    LOG(ERROR) << "Inconsistent cache version.";
    return false;
  }

  if (!SimpleExperimentMatches(file_header, experiment)) {
    LOG(WARNING) << "Rebuilding cache due to experiment change";
    return false;
  }

  bool new_fake_index_needed = (version_from != kSimpleVersion);

  // There should be one upgrade routine here for each incremental upgrade
  // starting at kMinVersionAbleToUpgrade.
  static_assert(kMinVersionAbleToUpgrade == 5, "upgrade routines don't match");
  DCHECK_LE(5U, version_from);
  if (version_from == 5) {
    // Upgrade only the index for V5 -> V6 move.
    if (!UpgradeIndexV5V6(path)) {
      LogMessageFailedUpgradeFromVersion(file_header.version);
      return false;
    }
    version_from++;
  }
  DCHECK_LE(6U, version_from);
  if (version_from == 6) {
    // No upgrade from V6 -> V7, because the entry format has not changed and
    // the V7 index reader is backwards compatible.
    version_from++;
  }

  if (version_from == 7) {
    // Likewise, V7 -> V8 is handled entirely by the index reader.
    version_from++;
  }

  DCHECK_EQ(kSimpleVersion, version_from);

  if (!new_fake_index_needed)
    return true;

  const base::FilePath temp_fake_index = path.AppendASCII("upgrade-index");
  if (!WriteFakeIndexFile(temp_fake_index, experiment)) {
    base::DeleteFile(temp_fake_index, /* recursive = */ false);
    LOG(ERROR) << "Failed to write a new fake index.";
    LogMessageFailedUpgradeFromVersion(file_header.version);
    return false;
  }
  if (!base::ReplaceFile(temp_fake_index, fake_index, NULL)) {
    LOG(ERROR) << "Failed to replace the fake index.";
    LogMessageFailedUpgradeFromVersion(file_header.version);
    return false;
  }
  return true;
}

}  // namespace disk_cache
