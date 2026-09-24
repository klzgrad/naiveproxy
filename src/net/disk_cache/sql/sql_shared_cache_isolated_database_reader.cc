// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/sql/sql_shared_cache_isolated_database_reader.h"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/hash/hash.h"
#include "base/logging.h"
#include "components/sqlite_vfs/client.h"
#include "net/disk_cache/sql/sql_shared_cache_isolated_database_queries.h"
#include "sql/statement.h"
#include "sql/transaction.h"

using disk_cache_sql_queries::GetSharedCacheIsolatedDatabaseQuery;
using disk_cache_sql_queries::SharedCacheIsolatedDatabaseQuery;

namespace disk_cache {

SqlSharedCacheIsolatedDatabaseReader::Response::Response(
    std::vector<uint8_t> headers,
    sql::StreamingBlobHandle body)
    : headers_(std::move(headers)), body_(std::move(body)) {}
SqlSharedCacheIsolatedDatabaseReader::Response::~Response() = default;
SqlSharedCacheIsolatedDatabaseReader::Response::Response(Response&&) = default;
SqlSharedCacheIsolatedDatabaseReader::Response&
SqlSharedCacheIsolatedDatabaseReader::Response::operator=(Response&&) = default;

std::vector<uint8_t>
SqlSharedCacheIsolatedDatabaseReader::Response::TakeHeaders() {
  return std::move(headers_);
}

int SqlSharedCacheIsolatedDatabaseReader::Response::GetBodySize() {
  return body_.GetSize();
}

bool SqlSharedCacheIsolatedDatabaseReader::Response::ReadBody(
    base::span<uint8_t> into) {
  return body_.Read(0, into);
}

// static
std::unique_ptr<SqlSharedCacheIsolatedDatabaseReader::DatabaseAssets>
SqlSharedCacheIsolatedDatabaseReader::DatabaseAssets::MaybeCreate(
    sqlite_vfs::PendingFileSet pending_file_set) {
  auto vfs_file_set = sqlite_vfs::SqliteVfsFileSet::Bind(
      sqlite_vfs::Client::kSharedCacheIsolated, std::move(pending_file_set));
  if (!vfs_file_set) {
    return nullptr;
  }
  return std::make_unique<DatabaseAssets>(std::move(*vfs_file_set),
                                          base::PassKey<DatabaseAssets>());
}

SqlSharedCacheIsolatedDatabaseReader::DatabaseAssets::DatabaseAssets(
    sqlite_vfs::SqliteVfsFileSet vfs_file_set,
    base::PassKey<DatabaseAssets>)
    : vfs_file_set_(std::move(vfs_file_set)),
      unregister_runner_(sqlite_vfs::SqliteSandboxedVfsDelegate::GetInstance()
                             ->RegisterSandboxedFiles(vfs_file_set_)),
      db_(sql::DatabaseOptions()
              .set_read_only(vfs_file_set_.read_only())
              .set_exclusive_locking(vfs_file_set_.is_single_connection())
              .set_wal_mode(vfs_file_set_.wal_mode())
              .set_vfs_name_discouraged(
                  sqlite_vfs::SqliteSandboxedVfsDelegate::kSqliteVfsName)
              .set_mmap_enabled(false),
          sql::Database::Tag("SharedCacheIsolated")) {}

SqlSharedCacheIsolatedDatabaseReader::DatabaseAssets::~DatabaseAssets() =
    default;

base::FilePath
SqlSharedCacheIsolatedDatabaseReader::DatabaseAssets::GetDbVirtualFilePath()
    const {
  return vfs_file_set_.GetDbVirtualFilePath();
}

SqlSharedCacheIsolatedDatabaseReader::SqlSharedCacheIsolatedDatabaseReader(
    sqlite_vfs::PendingFileSet pending_file_set)
    : db_assets_(DatabaseAssets::MaybeCreate(std::move(pending_file_set))) {
  if (db_assets_ &&
      !db_assets_->db().Open(db_assets_->GetDbVirtualFilePath())) {
    db_assets_.reset();
  }
}

SqlSharedCacheIsolatedDatabaseReader::~SqlSharedCacheIsolatedDatabaseReader() =
    default;

std::optional<SqlSharedCacheIsolatedDatabaseReader::Response>
SqlSharedCacheIsolatedDatabaseReader::ReadResponse(const std::string_view url) {
  // TODO(crbug.com/473666511): Add UMA histograms and TRACE_EVENTs for results
  // and execution time.
  auto result = ReadResponseInternal(url);
  return result.has_value() ? std::optional<Response>(std::move(*result))
                            : std::nullopt;
}

base::expected<SqlSharedCacheIsolatedDatabaseReader::Response,
               SqlSharedCacheIsolatedDatabaseReader::ReaderError>
SqlSharedCacheIsolatedDatabaseReader::ReadResponseInternal(
    const std::string_view url) {
  if (!db_assets_ || !db_assets_->db().is_open()) {
    return base::unexpected(ReaderError::kDatabaseNotOpen);
  }
  auto& db = db_assets_->db();
  sql::Transaction transaction(&db);
  if (!transaction.Begin()) {
    return base::unexpected(ReaderError::kFailedToStartTransaction);
  }
  std::vector<uint8_t> headers;
  int64_t rowid = 0;
  {
    sql::Statement statement(db.GetCachedStatement(
        SQL_FROM_HERE,
        GetSharedCacheIsolatedDatabaseQuery(
            SharedCacheIsolatedDatabaseQuery::kReaderSelectResource)));
    statement.BindInt64(0, static_cast<int32_t>(base::PersistentHash(url)));
    statement.BindString(1, url);
    if (!statement.is_valid()) {
      return base::unexpected(ReaderError::kInvalidStatement);
    }
    if (!statement.Step()) {
      return base::unexpected(ReaderError::kNotFound);
    }
    rowid = statement.ColumnInt64(0);
  }
  {
    auto headers_blob_handle =
        db.GetStreamingBlob("resources", "headers", rowid, /*readonly=*/true);
    if (!headers_blob_handle) {
      return base::unexpected(ReaderError::kFailedToGetHeadersBlob);
    }
    headers = std::vector<uint8_t>(headers_blob_handle->GetSize());
    if (!headers_blob_handle->Read(0, headers)) {
      return base::unexpected(ReaderError::kFailedToReadHeadersBlob);
    }
  }
  auto body =
      db.GetStreamingBlob("resources", "body", rowid, /*readonly=*/true);
  if (!body.has_value()) {
    return base::unexpected(ReaderError::kFailedToGetBodyBlob);
  }
  if (!transaction.Commit()) {
    return base::unexpected(ReaderError::kFailedToCommitTransaction);
  }
  return Response(std::move(headers), std::move(*body));
}

}  // namespace disk_cache
