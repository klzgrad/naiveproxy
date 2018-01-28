// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/memory/mem_backend_impl.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/sys_info.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "base/trace_event/process_memory_dump.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/cache_util.h"
#include "net/disk_cache/memory/mem_entry_impl.h"

using base::Time;

namespace disk_cache {

namespace {

const int kDefaultInMemoryCacheSize = 10 * 1024 * 1024;
const int kDefaultEvictionSize = kDefaultInMemoryCacheSize / 10;

bool CheckLRUListOrder(const base::LinkedList<MemEntryImpl>& lru_list) {
  // TODO(gavinp): Check MemBackendImpl::current_size_ here as well.
  base::Time previous_last_use_time;
  for (base::LinkNode<MemEntryImpl>* node = lru_list.head();
       node != lru_list.end(); node = node->next()) {
    if (node->value()->GetLastUsed() < previous_last_use_time)
      return false;
    previous_last_use_time = node->value()->GetLastUsed();
  }
  return true;
}

}  // namespace

MemBackendImpl::MemBackendImpl(net::NetLog* net_log)
    : max_size_(0), current_size_(0), net_log_(net_log), weak_factory_(this) {
}

MemBackendImpl::~MemBackendImpl() {
  DCHECK(CheckLRUListOrder(lru_list_));
  while (!entries_.empty())
    entries_.begin()->second->Doom();
  DCHECK_EQ(0, current_size_);

  if (!post_cleanup_callback_.is_null())
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(post_cleanup_callback_));
}

// static
std::unique_ptr<MemBackendImpl> MemBackendImpl::CreateBackend(
    int max_bytes,
    net::NetLog* net_log) {
  std::unique_ptr<MemBackendImpl> cache(
      std::make_unique<MemBackendImpl>(net_log));
  cache->SetMaxSize(max_bytes);
  if (cache->Init())
    return cache;

  LOG(ERROR) << "Unable to create cache";
  return nullptr;
}

bool MemBackendImpl::Init() {
  if (max_size_)
    return true;

  int64_t total_memory = base::SysInfo::AmountOfPhysicalMemory();

  if (total_memory <= 0) {
    max_size_ = kDefaultInMemoryCacheSize;
    return true;
  }

  // We want to use up to 2% of the computer's memory, with a limit of 50 MB,
  // reached on system with more than 2.5 GB of RAM.
  total_memory = total_memory * 2 / 100;
  if (total_memory > kDefaultInMemoryCacheSize * 5)
    max_size_ = kDefaultInMemoryCacheSize * 5;
  else
    max_size_ = static_cast<int32_t>(total_memory);

  return true;
}

bool MemBackendImpl::SetMaxSize(int max_bytes) {
  static_assert(sizeof(max_bytes) == sizeof(max_size_),
                "unsupported int model");
  if (max_bytes < 0)
    return false;

  // Zero size means use the default.
  if (!max_bytes)
    return true;

  max_size_ = max_bytes;
  return true;
}

int MemBackendImpl::MaxFileSize() const {
  return max_size_ / 8;
}

void MemBackendImpl::OnEntryInserted(MemEntryImpl* entry) {
  lru_list_.Append(entry);
}

void MemBackendImpl::OnEntryUpdated(MemEntryImpl* entry) {
  DCHECK(CheckLRUListOrder(lru_list_));
  // LinkedList<>::RemoveFromList() removes |entry| from |lru_list_|.
  entry->RemoveFromList();
  lru_list_.Append(entry);
}

void MemBackendImpl::OnEntryDoomed(MemEntryImpl* entry) {
  DCHECK(CheckLRUListOrder(lru_list_));
  if (entry->type() == MemEntryImpl::PARENT_ENTRY)
    entries_.erase(entry->key());
  // LinkedList<>::RemoveFromList() removes |entry| from |lru_list_|.
  entry->RemoveFromList();
}

void MemBackendImpl::ModifyStorageSize(int32_t delta) {
  current_size_ += delta;
  if (delta > 0)
    EvictIfNeeded();
}

bool MemBackendImpl::HasExceededStorageSize() const {
  return current_size_ > max_size_;
}

void MemBackendImpl::SetPostCleanupCallback(base::OnceClosure cb) {
  DCHECK(post_cleanup_callback_.is_null());
  post_cleanup_callback_ = std::move(cb);
}

net::CacheType MemBackendImpl::GetCacheType() const {
  return net::MEMORY_CACHE;
}

int32_t MemBackendImpl::GetEntryCount() const {
  return static_cast<int32_t>(entries_.size());
}

int MemBackendImpl::OpenEntry(const std::string& key,
                              Entry** entry,
                              const CompletionCallback& callback) {
  EntryMap::iterator it = entries_.find(key);
  if (it == entries_.end())
    return net::ERR_FAILED;

  it->second->Open();

  *entry = it->second;
  return net::OK;
}

int MemBackendImpl::CreateEntry(const std::string& key,
                                Entry** entry,
                                const CompletionCallback& callback) {
  std::pair<EntryMap::iterator, bool> create_result =
      entries_.insert(EntryMap::value_type(key, nullptr));
  const bool did_insert = create_result.second;
  if (!did_insert)
    return net::ERR_FAILED;

  MemEntryImpl* cache_entry = new MemEntryImpl(this, key, net_log_);
  create_result.first->second = cache_entry;
  *entry = cache_entry;
  return net::OK;
}

int MemBackendImpl::DoomEntry(const std::string& key,
                              const CompletionCallback& callback) {
  EntryMap::iterator it = entries_.find(key);
  if (it == entries_.end())
    return net::ERR_FAILED;

  it->second->Doom();
  return net::OK;
}

int MemBackendImpl::DoomAllEntries(const CompletionCallback& callback) {
  return DoomEntriesBetween(Time(), Time(), callback);
}

int MemBackendImpl::DoomEntriesBetween(Time initial_time,
                                       Time end_time,
                                       const CompletionCallback& callback) {
  if (end_time.is_null())
    end_time = Time::Max();
  DCHECK_GE(end_time, initial_time);

  base::LinkNode<MemEntryImpl>* node = lru_list_.head();
  while (node != lru_list_.end() && node->value()->GetLastUsed() < initial_time)
    node = node->next();
  while (node != lru_list_.end() && node->value()->GetLastUsed() < end_time) {
    MemEntryImpl* to_doom = node->value();
    node = node->next();
    to_doom->Doom();
  }

  return net::OK;
}

int MemBackendImpl::DoomEntriesSince(Time initial_time,
                                     const CompletionCallback& callback) {
  return DoomEntriesBetween(initial_time, Time::Max(), callback);
}

int MemBackendImpl::CalculateSizeOfAllEntries(
    const CompletionCallback& callback) {
  return current_size_;
}

int MemBackendImpl::CalculateSizeOfEntriesBetween(
    base::Time initial_time,
    base::Time end_time,
    const CompletionCallback& callback) {
  if (end_time.is_null())
    end_time = Time::Max();
  DCHECK_GE(end_time, initial_time);

  int size = 0;
  base::LinkNode<MemEntryImpl>* node = lru_list_.head();
  while (node != lru_list_.end() && node->value()->GetLastUsed() < initial_time)
    node = node->next();
  while (node != lru_list_.end() && node->value()->GetLastUsed() < end_time) {
    MemEntryImpl* entry = node->value();
    size += entry->GetStorageSize();
    node = node->next();
  }
  return size;
}

class MemBackendImpl::MemIterator final : public Backend::Iterator {
 public:
  explicit MemIterator(base::WeakPtr<MemBackendImpl> backend)
      : backend_(backend) {}

  int OpenNextEntry(Entry** next_entry,
                    const CompletionCallback& callback) override {
    if (!backend_)
      return net::ERR_FAILED;

    if (!backend_keys_) {
      backend_keys_ = std::make_unique<Strings>(backend_->entries_.size());
      for (const auto& iter : backend_->entries_)
        backend_keys_->push_back(iter.first);
      current_ = backend_keys_->begin();
    } else {
      current_++;
    }

    while (true) {
      if (current_ == backend_keys_->end()) {
        *next_entry = nullptr;
        backend_keys_.reset();
        return net::ERR_FAILED;
      }

      const auto& entry_iter = backend_->entries_.find(*current_);
      if (entry_iter == backend_->entries_.end()) {
        // The key is no longer in the cache, move on to the next key.
        current_++;
        continue;
      }

      entry_iter->second->Open();
      *next_entry = entry_iter->second;
      return net::OK;
    }
  }

 private:
  using Strings = std::vector<std::string>;

  base::WeakPtr<MemBackendImpl> backend_;
  std::unique_ptr<Strings> backend_keys_;
  Strings::iterator current_;
};

std::unique_ptr<Backend::Iterator> MemBackendImpl::CreateIterator() {
  return std::unique_ptr<Backend::Iterator>(
      new MemIterator(weak_factory_.GetWeakPtr()));
}

void MemBackendImpl::OnExternalCacheHit(const std::string& key) {
  EntryMap::iterator it = entries_.find(key);
  if (it != entries_.end())
    it->second->UpdateStateOnUse(MemEntryImpl::ENTRY_WAS_NOT_MODIFIED);
}

size_t MemBackendImpl::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_absolute_name) const {
  base::trace_event::MemoryAllocatorDump* dump =
      pmd->CreateAllocatorDump(parent_absolute_name + "/memory_backend");

  // Entries in lru_list_ will be counted by EMU but not in entries_ since
  // they're pointers.
  size_t size = base::trace_event::EstimateMemoryUsage(lru_list_) +
                base::trace_event::EstimateMemoryUsage(entries_);
  dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes, size);
  dump->AddScalar("mem_backend_size",
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                  current_size_);
  dump->AddScalar("mem_backend_max_size",
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                  max_size_);
  return size;
}

void MemBackendImpl::EvictIfNeeded() {
  if (current_size_ <= max_size_)
    return;

  int target_size = std::max(0, max_size_ - kDefaultEvictionSize);

  base::LinkNode<MemEntryImpl>* entry = lru_list_.head();
  while (current_size_ > target_size && entry != lru_list_.end()) {
    MemEntryImpl* to_doom = entry->value();
    entry = entry->next();
    if (!to_doom->InUse())
      to_doom->Doom();
  }
}

}  // namespace disk_cache
