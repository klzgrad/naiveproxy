// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SQL_SQL_SHARED_CACHE_ISOLATED_DATABASE_READER_H_
#define NET_DISK_CACHE_SQL_SQL_SHARED_CACHE_ISOLATED_DATABASE_READER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "base/types/pass_key.h"
#include "components/sqlite_vfs/pending_file_set.h"
#include "components/sqlite_vfs/sqlite_sandboxed_vfs.h"
#include "net/base/net_export.h"
#include "sql/database.h"
#include "sql/streaming_blob_handle.h"

namespace disk_cache {

// Provides read-only access to a single isolated SQLite database shard.
//
// This class is designed for use in renderer processes, receiving a read-only
// PendingFileSet from the network service process via Mojo. It binds the
// PendingFileSet using SandboxedVfs and opens a read-only SQLite connection to
// directly fetch cached resources without IPC overhead.
class NET_EXPORT_PRIVATE SqlSharedCacheIsolatedDatabaseReader {
 public:
  class NET_EXPORT_PRIVATE Response {
   public:
    Response(std::vector<uint8_t> headers, sql::StreamingBlobHandle body);
    ~Response();
    Response(Response&&);
    Response& operator=(Response&&);

    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;

    // Returns the response headers, moving them out of the Response object.
    std::vector<uint8_t> TakeHeaders();
    // Returns the size of the response body in bytes.
    int GetBodySize();
    // Reads the response body into the provided span. Returns true on success.
    bool ReadBody(base::span<uint8_t> into);

   private:
    std::vector<uint8_t> headers_;
    sql::StreamingBlobHandle body_;
  };

  enum class ReaderError {
    kNotFound = 1,
    kDatabaseNotOpen = 2,
    kFailedToStartTransaction = 3,
    kInvalidStatement = 4,
    kFailedToGetHeadersBlob = 5,
    kFailedToReadHeadersBlob = 6,
    kFailedToGetBodyBlob = 7,
    kFailedToCommitTransaction = 8,
  };

  explicit SqlSharedCacheIsolatedDatabaseReader(
      sqlite_vfs::PendingFileSet pending_file_set);
  ~SqlSharedCacheIsolatedDatabaseReader();

  SqlSharedCacheIsolatedDatabaseReader(
      const SqlSharedCacheIsolatedDatabaseReader&) = delete;
  SqlSharedCacheIsolatedDatabaseReader& operator=(
      const SqlSharedCacheIsolatedDatabaseReader&) = delete;

  // Reads the response headers and body blob handle for the given resource URL.
  // Returns std::nullopt if the resource is not found or not in the ready
  // state.
  std::optional<Response> ReadResponse(const std::string_view url);

 private:
  class DatabaseAssets {
   public:
    static std::unique_ptr<DatabaseAssets> MaybeCreate(
        sqlite_vfs::PendingFileSet pending_file_set);

    DatabaseAssets(sqlite_vfs::SqliteVfsFileSet vfs_file_set,
                   base::PassKey<DatabaseAssets>);
    DatabaseAssets(const DatabaseAssets&) = delete;
    DatabaseAssets& operator=(const DatabaseAssets&) = delete;

    ~DatabaseAssets();

    sql::Database& db() { return db_; }
    base::FilePath GetDbVirtualFilePath() const;

   private:
    sqlite_vfs::SqliteVfsFileSet vfs_file_set_;
    sqlite_vfs::SqliteSandboxedVfsDelegate::UnregisterRunner unregister_runner_;
    sql::Database db_;
  };

  base::expected<Response, ReaderError> ReadResponseInternal(
      const std::string_view url);

  std::unique_ptr<DatabaseAssets> db_assets_;
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SQL_SQL_SHARED_CACHE_ISOLATED_DATABASE_READER_H_
