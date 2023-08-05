// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/hpack/hpack_header_table.h"

#include <algorithm>

#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/hpack/hpack_constants.h"
#include "quiche/spdy/core/hpack/hpack_static_table.h"

namespace spdy {

HpackHeaderTable::HpackHeaderTable()
    : static_entries_(ObtainHpackStaticTable().GetStaticEntries()),
      static_index_(ObtainHpackStaticTable().GetStaticIndex()),
      static_name_index_(ObtainHpackStaticTable().GetStaticNameIndex()),
      settings_size_bound_(kDefaultHeaderTableSizeSetting),
      size_(0),
      max_size_(kDefaultHeaderTableSizeSetting),
      dynamic_table_insertions_(0) {}

HpackHeaderTable::~HpackHeaderTable() = default;

size_t HpackHeaderTable::GetByName(absl::string_view name) {
  {
    auto it = static_name_index_.find(name);
    if (it != static_name_index_.end()) {
      return 1 + it->second;
    }
  }
  {
    NameToEntryMap::const_iterator it = dynamic_name_index_.find(name);
    if (it != dynamic_name_index_.end()) {
      return dynamic_table_insertions_ - it->second + kStaticTableSize;
    }
  }
  return kHpackEntryNotFound;
}

size_t HpackHeaderTable::GetByNameAndValue(absl::string_view name,
                                           absl::string_view value) {
  HpackLookupEntry query{name, value};
  {
    auto it = static_index_.find(query);
    if (it != static_index_.end()) {
      return 1 + it->second;
    }
  }
  {
    auto it = dynamic_index_.find(query);
    if (it != dynamic_index_.end()) {
      return dynamic_table_insertions_ - it->second + kStaticTableSize;
    }
  }
  return kHpackEntryNotFound;
}

void HpackHeaderTable::SetMaxSize(size_t max_size) {
  QUICHE_CHECK_LE(max_size, settings_size_bound_);

  max_size_ = max_size;
  if (size_ > max_size_) {
    Evict(EvictionCountToReclaim(size_ - max_size_));
    QUICHE_CHECK_LE(size_, max_size_);
  }
}

void HpackHeaderTable::SetSettingsHeaderTableSize(size_t settings_size) {
  settings_size_bound_ = settings_size;
  SetMaxSize(settings_size_bound_);
}

void HpackHeaderTable::EvictionSet(absl::string_view name,
                                   absl::string_view value,
                                   DynamicEntryTable::iterator* begin_out,
                                   DynamicEntryTable::iterator* end_out) {
  size_t eviction_count = EvictionCountForEntry(name, value);
  *begin_out = dynamic_entries_.end() - eviction_count;
  *end_out = dynamic_entries_.end();
}

size_t HpackHeaderTable::EvictionCountForEntry(absl::string_view name,
                                               absl::string_view value) const {
  size_t available_size = max_size_ - size_;
  size_t entry_size = HpackEntry::Size(name, value);

  if (entry_size <= available_size) {
    // No evictions are required.
    return 0;
  }
  return EvictionCountToReclaim(entry_size - available_size);
}

size_t HpackHeaderTable::EvictionCountToReclaim(size_t reclaim_size) const {
  size_t count = 0;
  for (auto it = dynamic_entries_.rbegin();
       it != dynamic_entries_.rend() && reclaim_size != 0; ++it, ++count) {
    reclaim_size -= std::min(reclaim_size, (*it)->Size());
  }
  return count;
}

void HpackHeaderTable::Evict(size_t count) {
  for (size_t i = 0; i != count; ++i) {
    QUICHE_CHECK(!dynamic_entries_.empty());

    HpackEntry* entry = dynamic_entries_.back().get();
    const size_t index = dynamic_table_insertions_ - dynamic_entries_.size();

    size_ -= entry->Size();
    auto it = dynamic_index_.find({entry->name(), entry->value()});
    QUICHE_DCHECK(it != dynamic_index_.end());
    // Only remove an entry from the index if its insertion index matches;
    // otherwise, the index refers to another entry with the same name and
    // value.
    if (it->second == index) {
      dynamic_index_.erase(it);
    }
    auto name_it = dynamic_name_index_.find(entry->name());
    QUICHE_DCHECK(name_it != dynamic_name_index_.end());
    // Only remove an entry from the literal index if its insertion index
    /// matches; otherwise, the index refers to another entry with the same
    // name.
    if (name_it->second == index) {
      dynamic_name_index_.erase(name_it);
    }
    dynamic_entries_.pop_back();
  }
}

const HpackEntry* HpackHeaderTable::TryAddEntry(absl::string_view name,
                                                absl::string_view value) {
  // Since |dynamic_entries_| has iterator stability, |name| and |value| are
  // valid even after evicting other entries and push_front() making room for
  // the new one.
  Evict(EvictionCountForEntry(name, value));

  size_t entry_size = HpackEntry::Size(name, value);
  if (entry_size > (max_size_ - size_)) {
    // Entire table has been emptied, but there's still insufficient room.
    QUICHE_DCHECK(dynamic_entries_.empty());
    QUICHE_DCHECK_EQ(0u, size_);
    return nullptr;
  }

  const size_t index = dynamic_table_insertions_;
  dynamic_entries_.push_front(
      std::make_unique<HpackEntry>(std::string(name), std::string(value)));
  HpackEntry* new_entry = dynamic_entries_.front().get();
  auto index_result = dynamic_index_.insert(std::make_pair(
      HpackLookupEntry{new_entry->name(), new_entry->value()}, index));
  if (!index_result.second) {
    // An entry with the same name and value already exists in the dynamic
    // index. We should replace it with the newly added entry.
    QUICHE_DVLOG(1) << "Found existing entry at: " << index_result.first->second
                    << " replacing with: " << new_entry->GetDebugString()
                    << " at: " << index;
    QUICHE_DCHECK_GT(index, index_result.first->second);
    dynamic_index_.erase(index_result.first);
    auto insert_result = dynamic_index_.insert(std::make_pair(
        HpackLookupEntry{new_entry->name(), new_entry->value()}, index));
    QUICHE_CHECK(insert_result.second);
  }

  auto name_result =
      dynamic_name_index_.insert(std::make_pair(new_entry->name(), index));
  if (!name_result.second) {
    // An entry with the same name already exists in the dynamic index. We
    // should replace it with the newly added entry.
    QUICHE_DVLOG(1) << "Found existing entry at: " << name_result.first->second
                    << " replacing with: " << new_entry->GetDebugString()
                    << " at: " << index;
    QUICHE_DCHECK_GT(index, name_result.first->second);
    dynamic_name_index_.erase(name_result.first);
    auto insert_result =
        dynamic_name_index_.insert(std::make_pair(new_entry->name(), index));
    QUICHE_CHECK(insert_result.second);
  }

  size_ += entry_size;
  ++dynamic_table_insertions_;

  return dynamic_entries_.front().get();
}

}  // namespace spdy
