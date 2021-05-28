// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_header_table.h"

#include "absl/strings/string_view.h"
#include "quic/core/qpack/qpack_static_table.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

QpackHeaderTable::QpackHeaderTable()
    : static_entries_(ObtainQpackStaticTable().GetStaticEntries()),
      static_index_(ObtainQpackStaticTable().GetStaticIndex()),
      static_name_index_(ObtainQpackStaticTable().GetStaticNameIndex()),
      dynamic_table_size_(0),
      dynamic_table_capacity_(0),
      maximum_dynamic_table_capacity_(0),
      max_entries_(0),
      dropped_entry_count_(0),
      dynamic_table_entry_referenced_(false) {}

QpackHeaderTable::~QpackHeaderTable() {
  for (auto& entry : observers_) {
    entry.second->Cancel();
  }
}

const QpackEntry* QpackHeaderTable::LookupEntry(bool is_static,
                                                uint64_t index) const {
  if (is_static) {
    if (index >= static_entries_.size()) {
      return nullptr;
    }

    return &static_entries_[index];
  }

  if (index < dropped_entry_count_) {
    return nullptr;
  }

  index -= dropped_entry_count_;

  if (index >= dynamic_entries_.size()) {
    return nullptr;
  }

  return &dynamic_entries_[index];
}

QpackHeaderTable::MatchType QpackHeaderTable::FindHeaderField(
    absl::string_view name,
    absl::string_view value,
    bool* is_static,
    uint64_t* index) const {
  QpackLookupEntry query{name, value};

  // Look for exact match in static table.
  auto index_it = static_index_.find(query);
  if (index_it != static_index_.end()) {
    *index = index_it->second;
    *is_static = true;
    return MatchType::kNameAndValue;
  }

  // Look for exact match in dynamic table.
  index_it = dynamic_index_.find(query);
  if (index_it != dynamic_index_.end()) {
    *index = index_it->second;
    *is_static = false;
    return MatchType::kNameAndValue;
  }

  // Look for name match in static table.
  auto name_index_it = static_name_index_.find(name);
  if (name_index_it != static_name_index_.end()) {
    *index = name_index_it->second;
    *is_static = true;
    return MatchType::kName;
  }

  // Look for name match in dynamic table.
  name_index_it = dynamic_name_index_.find(name);
  if (name_index_it != dynamic_name_index_.end()) {
    *index = name_index_it->second;
    *is_static = false;
    return MatchType::kName;
  }

  return MatchType::kNoMatch;
}

bool QpackHeaderTable::EntryFitsDynamicTableCapacity(
    absl::string_view name,
    absl::string_view value) const {
  return QpackEntry::Size(name, value) <= dynamic_table_capacity_;
}

uint64_t QpackHeaderTable::InsertEntry(absl::string_view name,
                                       absl::string_view value) {
  QUICHE_DCHECK(EntryFitsDynamicTableCapacity(name, value));

  const uint64_t index = dropped_entry_count_ + dynamic_entries_.size();

  // Copy name and value before modifying the container, because evicting
  // entries or even inserting a new one might invalidate |name| or |value| if
  // they point to an entry.
  QpackEntry new_entry((std::string(name)), (std::string(value)));
  const size_t entry_size = new_entry.Size();

  EvictDownToCapacity(dynamic_table_capacity_ - entry_size);

  dynamic_table_size_ += entry_size;
  dynamic_entries_.push_back(std::move(new_entry));

  // Make name and value point to the new entry.
  name = dynamic_entries_.back().name();
  value = dynamic_entries_.back().value();

  auto index_result = dynamic_index_.insert(
      std::make_pair(QpackLookupEntry{name, value}, index));
  if (!index_result.second) {
    // An entry with the same name and value already exists.  It needs to be
    // replaced, because |dynamic_index_| tracks the most recent entry for a
    // given name and value.
    QUICHE_DCHECK_GT(index, index_result.first->second);
    dynamic_index_.erase(index_result.first);
    auto result = dynamic_index_.insert(
        std::make_pair(QpackLookupEntry{name, value}, index));
    QUICHE_CHECK(result.second);
  }

  auto name_result = dynamic_name_index_.insert({name, index});
  if (!name_result.second) {
    // An entry with the same name already exists.  It needs to be replaced,
    // because |dynamic_name_index_| tracks the most recent entry for a given
    // name.
    QUICHE_DCHECK_GT(index, name_result.first->second);
    dynamic_name_index_.erase(name_result.first);
    auto result = dynamic_name_index_.insert({name, index});
    QUICHE_CHECK(result.second);
  }

  // Notify and deregister observers whose threshold is met, if any.
  while (!observers_.empty()) {
    auto it = observers_.begin();
    if (it->first > inserted_entry_count()) {
      break;
    }
    Observer* observer = it->second;
    observers_.erase(it);
    observer->OnInsertCountReachedThreshold();
  }

  return index;
}

uint64_t QpackHeaderTable::MaxInsertSizeWithoutEvictingGivenEntry(
    uint64_t index) const {
  QUICHE_DCHECK_LE(dropped_entry_count_, index);

  if (index > inserted_entry_count()) {
    // All entries are allowed to be evicted.
    return dynamic_table_capacity_;
  }

  // Initialize to current available capacity.
  uint64_t max_insert_size = dynamic_table_capacity_ - dynamic_table_size_;

  uint64_t entry_index = dropped_entry_count_;
  for (const auto& entry : dynamic_entries_) {
    if (entry_index >= index) {
      break;
    }
    ++entry_index;
    max_insert_size += entry.Size();
  }

  return max_insert_size;
}

bool QpackHeaderTable::SetDynamicTableCapacity(uint64_t capacity) {
  if (capacity > maximum_dynamic_table_capacity_) {
    return false;
  }

  dynamic_table_capacity_ = capacity;
  EvictDownToCapacity(capacity);

  QUICHE_DCHECK_LE(dynamic_table_size_, dynamic_table_capacity_);

  return true;
}

bool QpackHeaderTable::SetMaximumDynamicTableCapacity(
    uint64_t maximum_dynamic_table_capacity) {
  if (maximum_dynamic_table_capacity_ == 0) {
    maximum_dynamic_table_capacity_ = maximum_dynamic_table_capacity;
    max_entries_ = maximum_dynamic_table_capacity / 32;
    return true;
  }
  // If the value is already set, it should not be changed.
  return maximum_dynamic_table_capacity == maximum_dynamic_table_capacity_;
}

void QpackHeaderTable::RegisterObserver(uint64_t required_insert_count,
                                        Observer* observer) {
  QUICHE_DCHECK_GT(required_insert_count, 0u);
  observers_.insert({required_insert_count, observer});
}

void QpackHeaderTable::UnregisterObserver(uint64_t required_insert_count,
                                          Observer* observer) {
  auto it = observers_.lower_bound(required_insert_count);
  while (it != observers_.end() && it->first == required_insert_count) {
    if (it->second == observer) {
      observers_.erase(it);
      return;
    }
    ++it;
  }

  // |observer| must have been registered.
  QUIC_NOTREACHED();
}

uint64_t QpackHeaderTable::draining_index(float draining_fraction) const {
  QUICHE_DCHECK_LE(0.0, draining_fraction);
  QUICHE_DCHECK_LE(draining_fraction, 1.0);

  const uint64_t required_space = draining_fraction * dynamic_table_capacity_;
  uint64_t space_above_draining_index =
      dynamic_table_capacity_ - dynamic_table_size_;

  if (dynamic_entries_.empty() ||
      space_above_draining_index >= required_space) {
    return dropped_entry_count_;
  }

  auto it = dynamic_entries_.begin();
  uint64_t entry_index = dropped_entry_count_;
  while (space_above_draining_index < required_space) {
    space_above_draining_index += it->Size();
    ++it;
    ++entry_index;
    if (it == dynamic_entries_.end()) {
      return inserted_entry_count();
    }
  }

  return entry_index;
}

void QpackHeaderTable::EvictDownToCapacity(uint64_t capacity) {
  while (dynamic_table_size_ > capacity) {
    QUICHE_DCHECK(!dynamic_entries_.empty());

    QpackEntry* const entry = &dynamic_entries_.front();

    const uint64_t entry_size = entry->Size();
    QUICHE_DCHECK_GE(dynamic_table_size_, entry_size);
    dynamic_table_size_ -= entry_size;

    const uint64_t index = dropped_entry_count_;

    auto index_it = dynamic_index_.find({entry->name(), entry->value()});
    // Remove |dynamic_index_| entry only if it points to the same
    // QpackEntry in |dynamic_entries_|.
    if (index_it != dynamic_index_.end() && index_it->second == index) {
      dynamic_index_.erase(index_it);
    }

    auto name_it = dynamic_name_index_.find(entry->name());
    // Remove |dynamic_name_index_| entry only if it points to the same
    // QpackEntry in |dynamic_entries_|.
    if (name_it != dynamic_name_index_.end() && name_it->second == index) {
      dynamic_name_index_.erase(name_it);
    }

    dynamic_entries_.pop_front();
    ++dropped_entry_count_;
  }
}

}  // namespace quic
