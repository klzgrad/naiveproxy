/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_DATABASE_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_DATABASE_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/base/thread_annotations.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/parser/perfetto_sql_parser.h"
#include "src/trace_processor/sqlite/committed_state_manager.h"
#include "src/trace_processor/sqlite/sqlite_database.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor {

// Database-scoped state shared by every |PerfettoSqlConnection| attached to
// it: the underlying |SqliteDatabase|, package/macro registries, and the
// committed view of per-vtab state for each vtab module.
class PerfettoSqlDatabase {
 public:
  using Macro = PerfettoSqlParser::Macro;

  explicit PerfettoSqlDatabase(StringPool* pool);
  ~PerfettoSqlDatabase();

  PerfettoSqlDatabase(const PerfettoSqlDatabase&) = delete;
  PerfettoSqlDatabase& operator=(const PerfettoSqlDatabase&) = delete;

  StringPool* pool() const { return pool_; }
  std::shared_ptr<SqliteDatabase> sqlite_database() const {
    return sqlite_database_;
  }

  // Registers |package| under |name|. Fails if any module key in the new
  // package has already been recorded as included or poisoned on this
  // database, since re-registration would silently shadow a body the
  // connection has already imported.
  base::Status RegisterPackage(const std::string& name,
                               sql_modules::RegisteredPackage package)
      PERFETTO_LOCKS_EXCLUDED(include_mu_);
  void ErasePackage(const std::string& name);
  sql_modules::RegisteredPackage* FindPackage(const std::string& name);
  const sql_modules::RegisteredPackage* FindPackage(
      const std::string& name) const;
  sql_modules::RegisteredPackage* FindPackageForModule(const std::string& key);
  const sql_modules::RegisteredPackage* FindPackageForModule(
      const std::string& key) const;
  std::vector<std::pair<std::string, std::string>> GetModules() const;
  base::FlatHashMap<std::string, sql_modules::RegisteredPackage>& packages() {
    return packages_;
  }

  base::FlatHashMap<std::string, Macro>& macros() { return macros_; }
  const base::FlatHashMap<std::string, Macro>& macros() const {
    return macros_;
  }
  size_t macro_count() const { return macros_.size(); }

  // One committed-state store per vtab module type. Per-connection
  // |sqlite::ModuleStateManager|s are constructed against these so the
  // committed view is shared across every connection on this database.
  sqlite::CommittedStateManager& committed_dataframes() {
    return committed_dataframes_;
  }
  sqlite::CommittedStateManager& committed_runtime_table_functions() {
    return committed_runtime_table_functions_;
  }
  sqlite::CommittedStateManager& committed_static_table_functions() {
    return committed_static_table_functions_;
  }

  // RAII guard for the in-flight slot of an INCLUDE PERFETTO MODULE.
  //
  // The caller owns the slot for the module key and disposes of it via
  // exactly one of:
  //   - |ReleaseSuccess()|         the body ran cleanly; future
  //                                 |TryClaimInclude| calls for the same
  //                                 key short-circuit with
  //                                 |already_included = true|.
  //   - |ReleasePoisoned(reason)|  the body errored in a non-retryable
  //                                 way; the reason is recorded and replayed
  //                                 to subsequent callers without ever
  //                                 re-running the body.
  //   - destructor with neither    the slot is freed without record. Used
  //                                 when the claim was opened but no
  //                                 user-visible work was attempted (e.g.
  //                                 the caller bailed early).
  class IncludeClaim {
   public:
    IncludeClaim() = default;
    IncludeClaim(IncludeClaim&&) noexcept;
    IncludeClaim& operator=(IncludeClaim&&) noexcept;
    IncludeClaim(const IncludeClaim&) = delete;
    IncludeClaim& operator=(const IncludeClaim&) = delete;
    ~IncludeClaim();

    void ReleaseSuccess();
    void ReleasePoisoned(std::string reason);

   private:
    friend class PerfettoSqlDatabase;
    IncludeClaim(PerfettoSqlDatabase* db, std::string key)
        : db_(db), key_(std::move(key)) {}

    void ResetTransient();

    PerfettoSqlDatabase* db_ = nullptr;
    std::string key_;
  };

  struct IncludeClaimResult {
    // Owns the slot iff none of the flags below are set.
    IncludeClaim claim;
    bool already_included = false;
    bool poisoned = false;
    std::string poison_reason;
  };

  // Claims the include slot for |key|. Blocks until no peer connection is
  // mid-import for the same key, then returns one of:
  //   - |already_included = true|      a peer already finished a successful
  //                                     import of this key; caller noops.
  //   - |poisoned = true|              a previous attempt failed; caller
  //                                     surfaces |poison_reason| without
  //                                     re-running the body.
  //   - claim populated                caller owns the slot and must
  //                                     dispose of it via the claim.
  // The caller must guarantee that |key| is not already mid-import on the
  // calling connection; otherwise this call deadlocks waiting on its own
  // claim. Connections detect that case (cycle) by walking their own
  // execution stack before calling |TryClaimInclude|.
  IncludeClaimResult TryClaimInclude(const std::string& key)
      PERFETTO_LOCKS_EXCLUDED(include_mu_);

  // True iff |key| has been recorded as successfully included. Test-only.
  bool IsModuleIncluded(const std::string& key) const
      PERFETTO_LOCKS_EXCLUDED(include_mu_);

  // True iff |key| has been recorded as poisoned. Test-only.
  bool IsModulePoisoned(const std::string& key) const
      PERFETTO_LOCKS_EXCLUDED(include_mu_);

 private:
  friend class IncludeClaim;
  void ReleaseClaimSuccess(const std::string& key)
      PERFETTO_LOCKS_EXCLUDED(include_mu_);
  void ReleaseClaimPoisoned(const std::string& key, std::string reason)
      PERFETTO_LOCKS_EXCLUDED(include_mu_);
  void ReleaseClaimTransient(const std::string& key)
      PERFETTO_LOCKS_EXCLUDED(include_mu_);

  StringPool* const pool_;
  std::shared_ptr<SqliteDatabase> sqlite_database_;
  base::FlatHashMap<std::string, sql_modules::RegisteredPackage> packages_;
  base::FlatHashMap<std::string, Macro> macros_;

  sqlite::CommittedStateManager committed_dataframes_;
  sqlite::CommittedStateManager committed_runtime_table_functions_;
  sqlite::CommittedStateManager committed_static_table_functions_;

  // Single mutex+condvar guards include_in_progress_, included_modules_ and
  // poisoned_modules_. Waiters on |TryClaimInclude| block here while a peer
  // connection holds the in-flight slot for the same key. Same-connection
  // re-entry (cycles) is detected at the call site by walking the
  // connection's execution stack — the database has no notion of cycles.
  mutable std::mutex include_mu_;
  std::condition_variable include_cv_;
  std::unordered_set<std::string> include_in_progress_
      PERFETTO_GUARDED_BY(include_mu_);
  std::unordered_set<std::string> included_modules_
      PERFETTO_GUARDED_BY(include_mu_);
  std::unordered_map<std::string, std::string> poisoned_modules_
      PERFETTO_GUARDED_BY(include_mu_);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_DATABASE_H_
