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

#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_database.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/sqlite/sqlite_database.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor {

PerfettoSqlDatabase::PerfettoSqlDatabase(StringPool* pool)
    : pool_(pool), sqlite_database_(std::make_shared<SqliteDatabase>()) {}

PerfettoSqlDatabase::~PerfettoSqlDatabase() = default;

base::Status PerfettoSqlDatabase::RegisterPackage(
    const std::string& name,
    sql_modules::RegisteredPackage package) {
  {
    std::lock_guard<std::mutex> lock(include_mu_);
    for (auto m = package.modules.GetIterator(); m; ++m) {
      if (included_modules_.count(m.key()) != 0) {
        return base::ErrStatus(
            "Cannot register package '%s': module '%s' has already been "
            "included on this database; re-registration would silently "
            "shadow the imported body.",
            name.c_str(), m.key().c_str());
      }
      if (poisoned_modules_.count(m.key()) != 0) {
        return base::ErrStatus(
            "Cannot register package '%s': module '%s' is poisoned by an "
            "earlier failed include.",
            name.c_str(), m.key().c_str());
      }
    }
  }
  // Same-name re-registrations replace silently; the caller decides whether
  // to allow that. Prefix overlaps with *different* package names are always
  // rejected because |FindPackageForModule| assumes at most one package can
  // match any given key.
  packages_.Erase(name);
  for (auto pkg = packages_.GetIterator(); pkg; ++pkg) {
    if (sql_modules::IsPackagePrefixOf(name, pkg.key()) ||
        sql_modules::IsPackagePrefixOf(pkg.key(), name)) {
      return base::ErrStatus(
          "Package '%s' clashes with existing package '%s'. Package names "
          "cannot be prefixes of each other.",
          name.c_str(), pkg.key().c_str());
    }
  }
  packages_.Insert(name, std::move(package));
  return base::OkStatus();
}

void PerfettoSqlDatabase::ErasePackage(const std::string& name) {
  packages_.Erase(name);
}

sql_modules::RegisteredPackage* PerfettoSqlDatabase::FindPackage(
    const std::string& name) {
  return packages_.Find(name);
}

const sql_modules::RegisteredPackage* PerfettoSqlDatabase::FindPackage(
    const std::string& name) const {
  return packages_.Find(name);
}

sql_modules::RegisteredPackage* PerfettoSqlDatabase::FindPackageForModule(
    const std::string& key) {
  // Find the package whose name is a prefix of the key. Due to prefix clash
  // checking during registration, at most one package can match any given key.
  for (auto pkg = packages_.GetIterator(); pkg; ++pkg) {
    if (sql_modules::IsPackagePrefixOf(pkg.key(), key)) {
      return &pkg.value();
    }
  }
  return nullptr;
}

const sql_modules::RegisteredPackage* PerfettoSqlDatabase::FindPackageForModule(
    const std::string& key) const {
  // Find the package whose name is a prefix of the key. Due to prefix clash
  // checking during registration, at most one package can match any given key.
  for (auto pkg = packages_.GetIterator(); pkg; ++pkg) {
    if (sql_modules::IsPackagePrefixOf(pkg.key(), key)) {
      return &pkg.value();
    }
  }
  return nullptr;
}

std::vector<std::pair<std::string, std::string>>
PerfettoSqlDatabase::GetModules() const {
  std::vector<std::pair<std::string, std::string>> result;
  for (auto pkg = packages_.GetIterator(); pkg; ++pkg) {
    for (auto mod = pkg.value().modules.GetIterator(); mod; ++mod) {
      result.emplace_back(pkg.key(), mod.key());
    }
  }
  return result;
}

// 'std::unique_lock' doesn't work well with thread annotations
// (see https://github.com/llvm/llvm-project/issues/63239), so we suppress
// thread safety static analysis for this method. The lock is held
// throughout; analysis on every other method in the file is unaffected.
PerfettoSqlDatabase::IncludeClaimResult PerfettoSqlDatabase::TryClaimInclude(
    const std::string& key) PERFETTO_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(include_mu_);
  // Wait until no peer connection is mid-import for the same key. Cycles
  // (same connection re-entering for a key it already holds) are caught at
  // the call site; reaching that case here would deadlock on ourselves.
  include_cv_.wait(lock, [&]() PERFETTO_EXCLUSIVE_LOCKS_REQUIRED(include_mu_) {
    return include_in_progress_.count(key) == 0;
  });
  IncludeClaimResult res;
  if (included_modules_.count(key) != 0) {
    res.already_included = true;
    return res;
  }
  if (auto it = poisoned_modules_.find(key); it != poisoned_modules_.end()) {
    res.poisoned = true;
    res.poison_reason = it->second;
    return res;
  }
  include_in_progress_.insert(key);
  res.claim = IncludeClaim(this, key);
  return res;
}

void PerfettoSqlDatabase::ReleaseClaimSuccess(const std::string& key) {
  {
    std::lock_guard<std::mutex> lock(include_mu_);
    include_in_progress_.erase(key);
    included_modules_.insert(key);
  }
  include_cv_.notify_all();
}

void PerfettoSqlDatabase::ReleaseClaimPoisoned(const std::string& key,
                                               std::string reason) {
  {
    std::lock_guard<std::mutex> lock(include_mu_);
    include_in_progress_.erase(key);
    poisoned_modules_[key] = std::move(reason);
  }
  include_cv_.notify_all();
}

void PerfettoSqlDatabase::ReleaseClaimTransient(const std::string& key) {
  {
    std::lock_guard<std::mutex> lock(include_mu_);
    include_in_progress_.erase(key);
  }
  include_cv_.notify_all();
}

bool PerfettoSqlDatabase::IsModuleIncluded(const std::string& key) const {
  std::lock_guard<std::mutex> lock(include_mu_);
  return included_modules_.count(key) != 0;
}

bool PerfettoSqlDatabase::IsModulePoisoned(const std::string& key) const {
  std::lock_guard<std::mutex> lock(include_mu_);
  return poisoned_modules_.count(key) != 0;
}

PerfettoSqlDatabase::IncludeClaim::IncludeClaim(IncludeClaim&& o) noexcept
    : db_(std::exchange(o.db_, nullptr)), key_(std::move(o.key_)) {}

PerfettoSqlDatabase::IncludeClaim& PerfettoSqlDatabase::IncludeClaim::operator=(
    IncludeClaim&& o) noexcept {
  if (this != &o) {
    ResetTransient();
    db_ = std::exchange(o.db_, nullptr);
    key_ = std::move(o.key_);
  }
  return *this;
}

PerfettoSqlDatabase::IncludeClaim::~IncludeClaim() {
  ResetTransient();
}

void PerfettoSqlDatabase::IncludeClaim::ReleaseSuccess() {
  if (!db_) {
    return;
  }
  db_->ReleaseClaimSuccess(key_);
  db_ = nullptr;
  key_.clear();
}

void PerfettoSqlDatabase::IncludeClaim::ReleasePoisoned(std::string reason) {
  if (!db_) {
    return;
  }
  db_->ReleaseClaimPoisoned(key_, std::move(reason));
  db_ = nullptr;
  key_.clear();
}

void PerfettoSqlDatabase::IncludeClaim::ResetTransient() {
  if (!db_) {
    return;
  }
  db_->ReleaseClaimTransient(key_);
  db_ = nullptr;
  key_.clear();
}

}  // namespace perfetto::trace_processor
