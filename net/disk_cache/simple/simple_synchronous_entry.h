// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SIMPLE_SIMPLE_SYNCHRONOUS_ENTRY_H_
#define NET_DISK_CACHE_SIMPLE_SIMPLE_SYNCHRONOUS_ENTRY_H_

#include <stdint.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/feature_list.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece_forward.h"
#include "base/time/time.h"
#include "net/base/cache_type.h"
#include "net/base/net_export.h"
#include "net/disk_cache/simple/simple_entry_format.h"
#include "net/disk_cache/simple/simple_file_tracker.h"

namespace net {
class GrowableIOBuffer;
class IOBuffer;
}

FORWARD_DECLARE_TEST(DiskCacheBackendTest, SimpleCacheEnumerationLongKeys);

namespace disk_cache {

NET_EXPORT_PRIVATE extern const base::Feature kSimpleCachePrefetchExperiment;
NET_EXPORT_PRIVATE extern const char kSimplePrefetchBytesParam[];

// Returns how large a file would get prefetched on reading the entry.
// If the experiment is disabled, returns 0.
NET_EXPORT_PRIVATE int GetSimpleCachePrefetchSize();

class SimpleSynchronousEntry;

// This class handles the passing of data about the entry between
// SimpleEntryImplementation and SimpleSynchronousEntry and the computation of
// file offsets based on the data size for all streams.
class NET_EXPORT_PRIVATE SimpleEntryStat {
 public:
  SimpleEntryStat(base::Time last_used,
                  base::Time last_modified,
                  const int32_t data_size[],
                  const int32_t sparse_data_size);

  int GetOffsetInFile(size_t key_length, int offset, int stream_index) const;
  int GetEOFOffsetInFile(size_t key_length, int stream_index) const;
  int GetLastEOFOffsetInFile(size_t key_length, int file_index) const;
  int64_t GetFileSize(size_t key_length, int file_index) const;

  base::Time last_used() const { return last_used_; }
  base::Time last_modified() const { return last_modified_; }
  void set_last_used(base::Time last_used) { last_used_ = last_used; }
  void set_last_modified(base::Time last_modified) {
    last_modified_ = last_modified;
  }

  int32_t data_size(int stream_index) const { return data_size_[stream_index]; }
  void set_data_size(int stream_index, int data_size) {
    data_size_[stream_index] = data_size;
  }

  int32_t sparse_data_size() const { return sparse_data_size_; }
  void set_sparse_data_size(int32_t sparse_data_size) {
    sparse_data_size_ = sparse_data_size;
  }

 private:
  base::Time last_used_;
  base::Time last_modified_;
  int32_t data_size_[kSimpleEntryStreamCount];
  int32_t sparse_data_size_;
};

struct SimpleStreamPrefetchData {
  SimpleStreamPrefetchData();
  ~SimpleStreamPrefetchData();

  scoped_refptr<net::GrowableIOBuffer> data;
  uint32_t stream_crc32;
};

struct SimpleEntryCreationResults {
  explicit SimpleEntryCreationResults(SimpleEntryStat entry_stat);
  ~SimpleEntryCreationResults();

  SimpleSynchronousEntry* sync_entry;

  // Expectation is that [0] will always be filled in, but [1] might not be.
  SimpleStreamPrefetchData stream_prefetch_data[2];

  SimpleEntryStat entry_stat;
  int result;
};

// Worker thread interface to the very simple cache. This interface is not
// thread safe, and callers must ensure that it is only ever accessed from
// a single thread between synchronization points.
class SimpleSynchronousEntry {
 public:
  struct CRCRecord {
    CRCRecord();
    CRCRecord(int index_p, bool has_crc32_p, uint32_t data_crc32_p);

    int index;
    bool has_crc32;
    uint32_t data_crc32;
  };

  struct CRCRequest {
    CRCRequest()
        : data_crc32(0),
          request_verify(false),
          performed_verify(false),
          verify_ok(false) {}

    // Initial CRC, to be updated with CRC of block.
    uint32_t data_crc32;

    // If true, CRC should be verified if at end of stream.
    bool request_verify;

    // If true, CRC was actually checked.
    bool performed_verify;
    bool verify_ok;
  };

  struct EntryOperationData {
    EntryOperationData(int index_p, int offset_p, int buf_len_p);
    EntryOperationData(int index_p,
                       int offset_p,
                       int buf_len_p,
                       bool truncate_p,
                       bool doomed_p);
    EntryOperationData(int64_t sparse_offset_p, int buf_len_p);

    int index;
    int offset;
    int64_t sparse_offset;
    int buf_len;
    bool truncate;
    bool doomed;
  };

  // Opens a disk cache entry on disk. The |key| parameter is optional, if empty
  // the operation may be slower. The |entry_hash| parameter is required.
  // |had_index| is provided only for histograms.
  // |time_enqueued| is when this operation was added to the I/O thread pool,
  //  and is provided only for histograms.
  static void OpenEntry(net::CacheType cache_type,
                        const base::FilePath& path,
                        const std::string& key,
                        uint64_t entry_hash,
                        bool had_index,
                        const base::TimeTicks& time_enqueued,
                        SimpleFileTracker* file_tracker,
                        SimpleEntryCreationResults* out_results);

  static void CreateEntry(net::CacheType cache_type,
                          const base::FilePath& path,
                          const std::string& key,
                          uint64_t entry_hash,
                          bool had_index,
                          const base::TimeTicks& time_enqueued,
                          SimpleFileTracker* file_tracker,
                          SimpleEntryCreationResults* out_results);

  // Deletes an entry from the file system without affecting the state of the
  // corresponding instance, if any (allowing operations to continue to be
  // executed through that instance). Returns a net error code.
  static int DoomEntry(const base::FilePath& path, uint64_t entry_hash);

  // Like |DoomEntry()| above, except that it truncates the entry files rather
  // than deleting them. Used when dooming entries after the backend has
  // shutdown. See implementation of |SimpleEntryImpl::DoomEntryInternal()| for
  // more.
  static int TruncateEntryFiles(const base::FilePath& path,
                                uint64_t entry_hash);

  // Like |DoomEntry()| above. Deletes all entries corresponding to the
  // |key_hashes|. Succeeds only when all entries are deleted. Returns a net
  // error code.
  static int DoomEntrySet(const std::vector<uint64_t>* key_hashes,
                          const base::FilePath& path);

  // N.B. ReadData(), WriteData(), CheckEOFRecord(), ReadSparseData(),
  // WriteSparseData() and Close() may block on IO.
  //
  // All of these methods will put the //net return value into |*out_result|.

  // |crc_request| can be nullptr here, to denote that no CRC computation is
  // requested.
  void ReadData(const EntryOperationData& in_entry_op,
                CRCRequest* crc_request,
                SimpleEntryStat* entry_stat,
                net::IOBuffer* out_buf,
                int* out_result);
  void WriteData(const EntryOperationData& in_entry_op,
                 net::IOBuffer* in_buf,
                 SimpleEntryStat* out_entry_stat,
                 int* out_result);
  int CheckEOFRecord(base::File* file,
                     int stream_index,
                     const SimpleEntryStat& entry_stat,
                     uint32_t expected_crc32);

  void ReadSparseData(const EntryOperationData& in_entry_op,
                      net::IOBuffer* out_buf,
                      base::Time* out_last_used,
                      int* out_result);
  void WriteSparseData(const EntryOperationData& in_entry_op,
                       net::IOBuffer* in_buf,
                       uint64_t max_sparse_data_size,
                       SimpleEntryStat* out_entry_stat,
                       int* out_result);
  void GetAvailableRange(const EntryOperationData& in_entry_op,
                         int64_t* out_start,
                         int* out_result);

  // Close all streams, and add write EOF records to streams indicated by the
  // CRCRecord entries in |crc32s_to_write|.
  void Close(const SimpleEntryStat& entry_stat,
             std::unique_ptr<std::vector<CRCRecord>> crc32s_to_write,
             net::GrowableIOBuffer* stream_0_data);

  const base::FilePath& path() const { return path_; }
  std::string key() const { return key_; }
  const SimpleFileTracker::EntryFileKey& entry_file_key() const {
    return entry_file_key_;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(::DiskCacheBackendTest,
                           SimpleCacheEnumerationLongKeys);
  friend class SimpleFileTrackerTest;

  enum CreateEntryResult {
    CREATE_ENTRY_SUCCESS = 0,
    CREATE_ENTRY_PLATFORM_FILE_ERROR = 1,
    CREATE_ENTRY_CANT_WRITE_HEADER = 2,
    CREATE_ENTRY_CANT_WRITE_KEY = 3,
    CREATE_ENTRY_MAX = 4,
  };

  enum FileRequired {
    FILE_NOT_REQUIRED,
    FILE_REQUIRED
  };

  struct SparseRange {
    int64_t offset;
    int64_t length;
    uint32_t data_crc32;
    int64_t file_offset;

    bool operator<(const SparseRange& other) const {
      return offset < other.offset;
    }
  };

  // When opening an entry without knowing the key, the header must be read
  // without knowing the size of the key. This is how much to read initially, to
  // make it likely the entire key is read.
  static const size_t kInitialHeaderRead = 64 * 1024;

  NET_EXPORT_PRIVATE SimpleSynchronousEntry(
      net::CacheType cache_type,
      const base::FilePath& path,
      const std::string& key,
      uint64_t entry_hash,
      bool had_index,
      SimpleFileTracker* simple_file_tracker);

  // Like Entry, the SimpleSynchronousEntry self releases when Close() is
  // called.
  NET_EXPORT_PRIVATE ~SimpleSynchronousEntry();

  // Tries to open one of the cache entry files. Succeeds if the open succeeds
  // or if the file was not found and is allowed to be omitted if the
  // corresponding stream is empty.
  bool MaybeOpenFile(int file_index,
                     base::File::Error* out_error);
  // Creates one of the cache entry files if necessary. If the file is allowed
  // to be omitted if the corresponding stream is empty, and if |file_required|
  // is FILE_NOT_REQUIRED, then the file is not created; otherwise, it is.
  bool MaybeCreateFile(int file_index,
                       FileRequired file_required,
                       base::File::Error* out_error);
  bool OpenFiles(SimpleEntryStat* out_entry_stat);
  bool CreateFiles(SimpleEntryStat* out_entry_stat);
  void CloseFile(int index);
  void CloseFiles();

  // Read the header and key at the beginning of the file, and validate that
  // they are correct. If this entry was opened with a key, the key is checked
  // for a match. If not, then the |key_| member is set based on the value in
  // this header. Records histograms if any check is failed.
  bool CheckHeaderAndKey(base::File* file, int file_index);

  // Returns a net error, i.e. net::OK on success.
  int InitializeForOpen(SimpleEntryStat* out_entry_stat,
                        SimpleStreamPrefetchData stream_prefetch_data[2]);

  // Writes the header and key to a newly-created stream file. |index| is the
  // index of the stream. Returns true on success; returns false and sets
  // |*out_result| on failure.
  bool InitializeCreatedFile(int index, CreateEntryResult* out_result);

  // Returns a net error, including net::OK on success and net::FILE_EXISTS
  // when the entry already exists.
  int InitializeForCreate(SimpleEntryStat* out_entry_stat);

  // Allocates and fills a buffer with stream 0 data in |stream_0_data|, then
  // checks its crc32. May also optionally read in |stream_1_data| and its
  // crc, but might decide not to.
  int ReadAndValidateStream0AndMaybe1(
      int file_size,
      SimpleEntryStat* out_entry_stat,
      SimpleStreamPrefetchData stream_prefetch_data[2]);

  // Reads the EOF record located at |file_offset| in file |file_index|,
  // with |file_0_prefetch| potentially having prefetched file 0 content.
  // Puts the result into |*eof_record| and sanity-checks it.
  // Returns net status, and records any failures to UMA.
  int GetEOFRecordData(base::File* file,
                       base::StringPiece file_0_prefetch,
                       int file_index,
                       int file_offset,
                       SimpleFileEOF* eof_record);

  // Reads either from |file_0_prefetch| or |file|.
  // Range-checks all the in-memory reads.
  bool ReadFromFileOrPrefetched(base::File* file,
                                base::StringPiece file_0_prefetch,
                                int file_index,
                                int offset,
                                int size,
                                char* dest);

  // Extracts out the payload of stream |stream_index|, reading either from
  // |file_0_prefetch|, if available, or |file|. |entry_stat| will be used to
  // determine file layout, though |extra_size| additional bytes will be read
  // past the stream payload end.
  //
  // |*stream_data| will be pointed to a fresh buffer with the results,
  // and |*out_crc32| will get the checksum, which will be verified against
  // |eof_record|.
  int PreReadStreamPayload(base::File* file,
                           base::StringPiece file_0_prefetch,
                           int stream_index,
                           int extra_size,
                           const SimpleEntryStat& entry_stat,
                           const SimpleFileEOF& eof_record,
                           SimpleStreamPrefetchData* out);

  void Doom() const;

  // Opens the sparse data file and scans it if it exists.
  bool OpenSparseFileIfExists(int32_t* out_sparse_data_size);

  // Creates and initializes the sparse data file.
  bool CreateSparseFile();

  // Closes the sparse data file.
  void CloseSparseFile();

  // Writes the header to the (newly-created) sparse file.
  bool InitializeSparseFile(base::File* file);

  // Removes all but the header of the sparse file.
  bool TruncateSparseFile(base::File* sparse_file);

  // Scans the existing ranges in the sparse file. Populates |sparse_ranges_|
  // and sets |*out_sparse_data_size| to the total size of all the ranges (not
  // including headers).
  bool ScanSparseFile(base::File* sparse_file, int32_t* out_sparse_data_size);

  // Reads from a single sparse range. If asked to read the entire range, also
  // verifies the CRC32.
  bool ReadSparseRange(base::File* sparse_file,
                       const SparseRange* range,
                       int offset,
                       int len,
                       char* buf);

  // Writes to a single (existing) sparse range. If asked to write the entire
  // range, also updates the CRC32; otherwise, invalidates it.
  bool WriteSparseRange(base::File* sparse_file,
                        SparseRange* range,
                        int offset,
                        int len,
                        const char* buf);

  // Appends a new sparse range to the sparse data file.
  bool AppendSparseRange(base::File* sparse_file,
                         int64_t offset,
                         int len,
                         const char* buf);

  static bool DeleteFileForEntryHash(const base::FilePath& path,
                                     uint64_t entry_hash,
                                     int file_index);
  static bool DeleteFilesForEntryHash(const base::FilePath& path,
                                      uint64_t entry_hash);
  static bool TruncateFilesForEntryHash(const base::FilePath& path,
                                        uint64_t entry_hash);

  void RecordSyncCreateResult(CreateEntryResult result, bool had_index);

  base::FilePath GetFilenameFromFileIndex(int file_index);

  bool sparse_file_open() const { return sparse_file_open_; }

  const net::CacheType cache_type_;
  const base::FilePath path_;
  SimpleFileTracker::EntryFileKey entry_file_key_;
  const bool had_index_;
  std::string key_;

  bool have_open_files_;
  bool initialized_;

  // Normally false. This is set to true when an entry is opened without
  // checking the file headers. Any subsequent read will perform the check
  // before completing.
  bool header_and_key_check_needed_[kSimpleEntryNormalFileCount] = {
      false,
  };

  SimpleFileTracker* file_tracker_;

  // True if the corresponding stream is empty and therefore no on-disk file
  // was created to store it.
  bool empty_file_omitted_[kSimpleEntryNormalFileCount];

  typedef std::map<int64_t, SparseRange> SparseRangeOffsetMap;
  typedef SparseRangeOffsetMap::iterator SparseRangeIterator;
  SparseRangeOffsetMap sparse_ranges_;
  bool sparse_file_open_;

  // Offset of the end of the sparse file (where the next sparse range will be
  // written).
  int64_t sparse_tail_offset_;

  // True if the entry was created, or false if it was opened. Used to log
  // SimpleCache.*.EntryCreatedWithStream2Omitted only for created entries.
  bool files_created_;
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SIMPLE_SIMPLE_SYNCHRONOUS_ENTRY_H_
