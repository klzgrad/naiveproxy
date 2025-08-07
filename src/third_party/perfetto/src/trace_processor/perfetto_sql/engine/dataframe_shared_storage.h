/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_DATAFRAME_SHARED_STORAGE_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_DATAFRAME_SHARED_STORAGE_H_

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/thread_annotations.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/uuid.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/types.h"

namespace perfetto::trace_processor {

// Shared storage for Dataframe objects and Dataframe indexes.
//
// The problem we are trying to solve is as follows:
//  1) We want to have multiple PerfettoSqlEngine instances which are working
//     on different threads.
//  2) There are several large tables in trace processor which will be used by
//     all the engines; these are both the static tables and the tables in the
//     SQL modules.
//  3) We don't want to duplicate the memory for these tables across the
//     engines.
//  4) So we need some shared storage for such dataframe objects: that's where
//     this class comes in.
//
// Specifically, this class works by having the notion of a "key" which is a
// unique identifier for a dataframe *before* any dataframe is created. The
// engines will use the key to lookup whether the dataframe has already been
// created. If it has, then the engine will use the existing dataframe. If it
// hasn't, then the engine will create a new dataframe and insert it into the
// shared storage for others to use.
//
// For convenience, even dataframes which we don't want to share can be stored
// to reduce complexity. We just given them a unique key with a random UUID.
//
// Usage:
//  auto key = DataframeSharedStorage::MakeKeyForSqlModuleTable(
//      "sql_module_name", "table_name");
//  auto df = DataframeSharedStorage::Find(key);
//  if (!df) {
//    df = DataframeSharedStorage::Insert(key, ComputeDataframe());
//  }
//
// This class is thread-safe.
class DataframeSharedStorage {
 private:
  template <typename T>
  struct Refcounted {
    T value;
    uint32_t refcount = 0;
  };

 public:
  template <typename T>
  struct Handle {
   public:
    Handle(Handle&&) = default;
    Handle& operator=(Handle&&) = default;

    ~Handle() {
      if (storage_) {
        storage_.get()->template Erase<T>(key_);
      }
    }
    T& value() { return value_; }
    const T& value() const { return value_; }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }

    const std::string& key() const { return key_; }

   private:
    friend class DataframeSharedStorage;
    Handle(std::string key, T value, DataframeSharedStorage& storage)
        : key_(std::move(key)), value_(std::move(value)), storage_(&storage) {}
    static int Close(DataframeSharedStorage*) { return 0; }

    std::string key_;
    T value_;
    base::ScopedResource<DataframeSharedStorage*, &Close, nullptr> storage_;
  };
  using DataframeHandle = Handle<dataframe::Dataframe>;
  using IndexHandle = Handle<dataframe::Index>;

  // Checks whether a dataframe with the given key has already been created.
  //
  // Returns nullopt if no such dataframe exists.
  std::optional<DataframeHandle> Find(const std::string& key) {
    return Find<dataframe::Dataframe>(key);
  }

  // Inserts a dataframe into the shared storage to be associated with the given
  // key.
  //
  // Returns the dataframe which is now owned by the shared storage. This might
  // be the same dataframe which was passed in as the argument or it might be a
  // a dataframe which is already stored in the shared storage.
  DataframeHandle Insert(std::string key, dataframe::Dataframe df) {
    PERFETTO_DCHECK(df.finalized());
    return Insert<dataframe::Dataframe>(std::move(key), std::move(df));
  }

  // Checks whether a index with the given key has already been created.
  //
  // Returns nullptr if no such index exists.
  std::optional<IndexHandle> FindIndex(const std::string& key) {
    return Find<dataframe::Index>(key);
  }

  // Inserts a dataframe index into the shared storage to be associated with the
  // given key.
  //
  // Returns the index which is now owned by the shared storage. This might
  // be the same index which was passed in as the argument or it might be a
  // a index which is already stored in the shared storage.
  IndexHandle InsertIndex(std::string key, dataframe::Index raw) {
    return Insert<dataframe::Index>(std::move(key), std::move(raw));
  }

  static std::string MakeKeyForSqlModuleTable(const std::string& module_name,
                                              const std::string& table_name) {
    return "sql_module:" + module_name + ":" + table_name;
  }
  static std::string MakeKeyForStaticTable(const std::string& table_name) {
    return "static_table:" + table_name;
  }
  static std::string MakeUniqueKey() {
    static std::atomic<uint64_t> next_key_counter_ = 0;
    return "unique:" + std::to_string(next_key_counter_.fetch_add(
                           1, std::memory_order_relaxed));
  }
  static std::string MakeIndexKey(const std::string& key,
                                  const uint32_t* col_start,
                                  const uint32_t* col_end) {
    std::string index_serialized = key + ":";
    for (const auto* it = col_start; it != col_end; ++it) {
      index_serialized += std::to_string(*it);
    }
    return index_serialized;
  }

 private:
  using DataframeMap =
      base::FlatHashMap<std::string, Refcounted<dataframe::Dataframe>>;
  using IndexMap = base::FlatHashMap<std::string, Refcounted<dataframe::Index>>;

  template <typename V>
  std::optional<Handle<V>> Find(const std::string& key) {
    std::lock_guard<std::mutex> mu(mutex_);
    auto& map = GetMap<V>();
    auto* it = map.Find(key);
    if (!it) {
      return std::nullopt;
    }
    it->refcount++;
    return Handle<V>(key, Copy(it->value), *this);
  }

  template <typename V>
  Handle<V> Insert(std::string key, V value) {
    std::lock_guard<std::mutex> mu(mutex_);
    auto& map = GetMap<V>();
    if (auto* it = map.Find(key); it) {
      it->refcount++;
      return Handle<V>(std::move(key), Copy(it->value), *this);
    }
    auto [it, inserted] = map.Insert(key, Refcounted<V>{std::move(value)});
    PERFETTO_CHECK(inserted);
    it->refcount++;
    return Handle<V>(std::move(key), Copy(it->value), *this);
  }

  template <typename V>
  void Erase(const std::string& key) {
    std::lock_guard<std::mutex> mu(mutex_);
    auto& map = GetMap<V>();
    auto* it = map.Find(key);
    PERFETTO_CHECK(it);
    PERFETTO_CHECK(it->refcount > 0);
    if (--it->refcount == 0) {
      map.Erase(key);
    }
  }

  static dataframe::Dataframe Copy(const dataframe::Dataframe& df) {
    return df.CopyFinalized();
  }

  static dataframe::Index Copy(const dataframe::Index& df) { return df.Copy(); }

  template <typename T>
  auto& GetMap() PERFETTO_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if constexpr (std::is_same_v<T, dataframe::Dataframe>) {
      return dataframes_;
    } else if constexpr (std::is_same_v<T, dataframe::Index>) {
      return indexes_;
    } else {
      static_assert(!std::is_same_v<T, T>, "Unsupported type");
    }
  }

  std::mutex mutex_;
  DataframeMap dataframes_ PERFETTO_GUARDED_BY(mutex_);
  IndexMap indexes_ PERFETTO_GUARDED_BY(mutex_);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_DATAFRAME_SHARED_STORAGE_H_
