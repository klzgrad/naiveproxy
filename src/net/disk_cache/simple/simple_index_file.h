// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SIMPLE_SIMPLE_INDEX_FILE_H_
#define NET_DISK_CACHE_SIMPLE_SIMPLE_INDEX_FILE_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/pickle.h"
#include "net/base/cache_type.h"
#include "net/base/net_export.h"
#include "net/disk_cache/simple/simple_backend_version.h"
#include "net/disk_cache/simple/simple_index.h"

namespace base {
class SequencedTaskRunner;
}

namespace disk_cache {
class BackendFileOperations;
class BackendFileOperationsFactory;

const uint64_t kSimpleIndexMagicNumber = UINT64_C(0x656e74657220796f);

struct NET_EXPORT_PRIVATE SimpleIndexLoadResult {
  SimpleIndexLoadResult();
  ~SimpleIndexLoadResult();
  void Reset();

  bool did_load = false;
  SimpleIndex::EntrySet entries;
  SimpleIndex::IndexWriteToDiskReason index_write_reason =
      SimpleIndex::INDEX_WRITE_REASON_MAX;
  SimpleIndex::IndexInitMethod init_method;
  bool flush_required = false;
};

// Simple Index File format is a pickle of IndexMetadata and EntryMetadata
// objects. The file format is as follows: one instance of |IndexMetadata|
// followed by |EntryMetadata| repeated |entry_count| times. To learn more about
// the format see |SimpleIndexFile::Serialize()| and
// |SimpleIndexFile::LoadFromDisk()|.
//
// The non-static methods must run on the source creation sequence. All the real
// work is done in the static methods, which are run on the cache thread
// or in worker threads. Synchronization between methods is the
// responsibility of the caller.
class NET_EXPORT_PRIVATE SimpleIndexFile {
 public:
  class NET_EXPORT_PRIVATE IndexMetadata {
   public:
    IndexMetadata();
    IndexMetadata(SimpleIndex::IndexWriteToDiskReason reason,
                  uint64_t entry_count,
                  uint64_t cache_size);

    virtual void Serialize(base::Pickle* pickle) const;
    bool Deserialize(base::PickleIterator* it);

    bool CheckIndexMetadata();

    SimpleIndex::IndexWriteToDiskReason reason() const { return reason_; }
    uint64_t entry_count() const { return entry_count_; }
    bool has_entry_in_memory_data() const { return version_ >= 8; }
    bool app_cache_has_trailer_prefetch_size() const { return version_ >= 9; }

   private:
    FRIEND_TEST_ALL_PREFIXES(IndexMetadataTest, Basics);
    FRIEND_TEST_ALL_PREFIXES(IndexMetadataTest, Serialize);
    FRIEND_TEST_ALL_PREFIXES(IndexMetadataTest, ReadV6Format);
    FRIEND_TEST_ALL_PREFIXES(SimpleIndexFileTest, ReadV7Format);
    FRIEND_TEST_ALL_PREFIXES(SimpleIndexFileTest, ReadV8Format);
    FRIEND_TEST_ALL_PREFIXES(SimpleIndexFileTest, ReadV8FormatAppCache);
    friend class V6IndexMetadataForTest;
    friend class V7IndexMetadataForTest;
    friend class V8IndexMetadataForTest;

    uint64_t magic_number_ = kSimpleIndexMagicNumber;
    uint32_t version_ = kSimpleVersion;
    SimpleIndex::IndexWriteToDiskReason reason_;
    uint64_t entry_count_;
    uint64_t cache_size_;  // Total cache storage size in bytes.
  };

  SimpleIndexFile(
      scoped_refptr<base::SequencedTaskRunner> cache_runner,
      scoped_refptr<BackendFileOperationsFactory> file_operations_factory,
      net::CacheType cache_type,
      const base::FilePath& cache_directory);

  SimpleIndexFile(const SimpleIndexFile&) = delete;
  SimpleIndexFile& operator=(const SimpleIndexFile&) = delete;

  virtual ~SimpleIndexFile();

  // Gets index entries based on current disk context. On error it may leave
  // |out_result.did_load| untouched, but still return partial and consistent
  // results in |out_result.entries|.
  virtual void LoadIndexEntries(base::Time cache_last_modified,
                                base::OnceClosure callback,
                                SimpleIndexLoadResult* out_result);

  // Writes the specified set of entries to disk.
  virtual void WriteToDisk(net::CacheType cache_type,
                           SimpleIndex::IndexWriteToDiskReason reason,
                           const SimpleIndex::EntrySet& entry_set,
                           uint64_t cache_size,
                           base::OnceClosure callback);

 private:
  friend class WrappedSimpleIndexFile;

  // Used for cache directory traversal.
  using EntryFileCallback =
      base::RepeatingCallback<void(const base::FilePath&,
                                   base::Time last_accessed,
                                   base::Time last_modified,
                                   int64_t size)>;

  // When loading the entries from disk, add this many extra hash buckets to
  // prevent reallocation on the creation sequence when merging in new live
  // entries.
  static const int kExtraSizeForMerge = 512;

  // Synchronous (IO performing) implementation of LoadIndexEntries.
  static void SyncLoadIndexEntries(
      std::unique_ptr<BackendFileOperations> file_operations,
      net::CacheType cache_type,
      base::Time cache_last_modified,
      const base::FilePath& cache_directory,
      const base::FilePath& index_file_path,
      SimpleIndexLoadResult* out_result);

  // Load the index file from disk returning an EntrySet.
  static void SyncLoadFromDisk(BackendFileOperations* file_operations,
                               net::CacheType cache_type,
                               const base::FilePath& index_filename,
                               base::Time* out_last_cache_seen_by_index,
                               SimpleIndexLoadResult* out_result);

  // Returns a scoped_ptr for a newly allocated base::Pickle containing the
  // serialized
  // data to be written to a file. Note: the pickle is not in a consistent state
  // immediately after calling this menthod, one needs to call
  // SerializeFinalData to make it ready to write to a file.
  static std::unique_ptr<base::Pickle> Serialize(
      net::CacheType cache_type,
      const SimpleIndexFile::IndexMetadata& index_metadata,
      const SimpleIndex::EntrySet& entries);

  // Appends cache modification time data to the serialized format. This is
  // performed on a thread accessing the disk. It is not combined with the main
  // serialization path to avoid extra thread hops or copying the pickle to the
  // worker thread.
  static void SerializeFinalData(base::Time cache_modified,
                                 base::Pickle* pickle);

  // Given the contents of an index file `data`, returns the corresponding
  // EntrySet and associated metadata. `out_result->did_load` will return
  // whether successful or not.
  static void Deserialize(net::CacheType cache_type,
                          base::span<const uint8_t> data,
                          base::Time* out_cache_last_modified,
                          SimpleIndexLoadResult* out_result);

  // Writes the index file to disk atomically.
  static void SyncWriteToDisk(
      std::unique_ptr<BackendFileOperations> file_operations,
      net::CacheType cache_type,
      const base::FilePath& cache_directory,
      const base::FilePath& index_filename,
      const base::FilePath& temp_index_filename,
      std::unique_ptr<base::Pickle> pickle);

  // Scan the index directory for entries, returning an EntrySet of all entries
  // found.
  static void SyncRestoreFromDisk(BackendFileOperations* file_operations,
                                  net::CacheType cache_type,
                                  const base::FilePath& cache_directory,
                                  const base::FilePath& index_file_path,
                                  SimpleIndexLoadResult* out_result);

  // Determines if an index file is stale relative to the time of last
  // modification of the cache directory. Obsolete, used only for a histogram to
  // compare with the new method.
  // TODO(pasko): remove this method after getting enough data.
  static bool LegacyIsIndexFileStale(BackendFileOperations* file_operations,
                                     base::Time cache_last_modified,
                                     const base::FilePath& index_file_path);

  const scoped_refptr<base::SequencedTaskRunner> cache_runner_;
  const scoped_refptr<BackendFileOperationsFactory> file_operations_factory_;
  const net::CacheType cache_type_;
  const base::FilePath cache_directory_;
  const base::FilePath index_file_;
  const base::FilePath temp_index_file_;

  static const char kIndexDirectory[];
  static const char kIndexFileName[];
  static const char kTempIndexFileName[];
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SIMPLE_SIMPLE_INDEX_FILE_H_
