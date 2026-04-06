// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_header_table.h"

#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_static_table.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

QpackEncoderHeaderTable::QpackEncoderHeaderTable()
    : static_index_(ObtainQpackStaticTable().GetStaticIndex()),
      static_name_index_(ObtainQpackStaticTable().GetStaticNameIndex()) {}

uint64_t QpackEncoderHeaderTable::InsertEntry(absl::string_view name,
                                              absl::string_view value) {
  const uint64_t index =
      QpackHeaderTableBase<QpackEncoderDynamicTable>::InsertEntry(name, value);

  // Make name and value point to the new entry.
  name = dynamic_entries().back()->name();
  value = dynamic_entries().back()->value();

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

  return index;
}

QpackEncoderHeaderTable::MatchResult QpackEncoderHeaderTable::FindHeaderField(
    absl::string_view name, absl::string_view value) const {
  QpackLookupEntry query{name, value};

  // Look for exact match in static table.
  auto index_it = static_index_.find(query);
  if (index_it != static_index_.end()) {
    return {/* match_type = */ MatchType::kNameAndValue,
            /* is_static = */ true,
            /* index = */ index_it->second};
  }

  // Look for exact match in dynamic table.
  index_it = dynamic_index_.find(query);
  if (index_it != dynamic_index_.end()) {
    return {/* match_type = */ MatchType::kNameAndValue,
            /* is_static = */ false,
            /* index = */ index_it->second};
  }

  return FindHeaderName(name);
}

QpackEncoderHeaderTable::MatchResult QpackEncoderHeaderTable::FindHeaderName(
    absl::string_view name) const {
  // Look for name match in static table.
  auto name_index_it = static_name_index_.find(name);
  if (name_index_it != static_name_index_.end()) {
    return {/* match_type = */ MatchType::kName,
            /* is_static = */ true,
            /* index = */ name_index_it->second};
  }

  // Look for name match in dynamic table.
  name_index_it = dynamic_name_index_.find(name);
  if (name_index_it != dynamic_name_index_.end()) {
    return {/* match_type = */ MatchType::kName,
            /* is_static = */ false,
            /* index = */ name_index_it->second};
  }

  return {/* match_type = */ MatchType::kNoMatch,
          /* is_static = */ false,
          /* index = */ 0};
}

uint64_t QpackEncoderHeaderTable::MaxInsertSizeWithoutEvictingGivenEntry(
    uint64_t index) const {
  QUICHE_DCHECK_LE(dropped_entry_count(), index);

  if (index > inserted_entry_count()) {
    // All entries are allowed to be evicted.
    return dynamic_table_capacity();
  }

  // Initialize to current available capacity.
  uint64_t max_insert_size = dynamic_table_capacity() - dynamic_table_size();

  uint64_t entry_index = dropped_entry_count();
  for (const auto& entry : dynamic_entries()) {
    if (entry_index >= index) {
      break;
    }
    ++entry_index;
    max_insert_size += entry->Size();
  }

  return max_insert_size;
}

uint64_t QpackEncoderHeaderTable::draining_index(
    float draining_fraction) const {
  QUICHE_DCHECK_LE(0.0, draining_fraction);
  QUICHE_DCHECK_LE(draining_fraction, 1.0);

  const uint64_t required_space = draining_fraction * dynamic_table_capacity();
  uint64_t space_above_draining_index =
      dynamic_table_capacity() - dynamic_table_size();

  if (dynamic_entries().empty() ||
      space_above_draining_index >= required_space) {
    return dropped_entry_count();
  }

  auto it = dynamic_entries().begin();
  uint64_t entry_index = dropped_entry_count();
  while (space_above_draining_index < required_space) {
    space_above_draining_index += (*it)->Size();
    ++it;
    ++entry_index;
    if (it == dynamic_entries().end()) {
      return inserted_entry_count();
    }
  }

  return entry_index;
}

void QpackEncoderHeaderTable::RemoveEntryFromEnd() {
  const QpackEntry* const entry = dynamic_entries().front().get();
  const uint64_t index = dropped_entry_count();

  auto index_it = dynamic_index_.find({entry->name(), entry->value()});
  // Remove |dynamic_index_| entry only if it points to the same
  // QpackEntry in dynamic_entries().
  if (index_it != dynamic_index_.end() && index_it->second == index) {
    dynamic_index_.erase(index_it);
  }

  auto name_it = dynamic_name_index_.find(entry->name());
  // Remove |dynamic_name_index_| entry only if it points to the same
  // QpackEntry in dynamic_entries().
  if (name_it != dynamic_name_index_.end() && name_it->second == index) {
    dynamic_name_index_.erase(name_it);
  }

  QpackHeaderTableBase<QpackEncoderDynamicTable>::RemoveEntryFromEnd();
}

QpackDecoderHeaderTable::QpackDecoderHeaderTable()
    : static_entries_(ObtainQpackStaticTable().GetStaticEntries()) {}

QpackDecoderHeaderTable::~QpackDecoderHeaderTable() {
  for (auto& entry : observers_) {
    entry.second->Cancel();
  }
}

uint64_t QpackDecoderHeaderTable::InsertEntry(absl::string_view name,
                                              absl::string_view value) {
  const uint64_t index =
      QpackHeaderTableBase<QpackDecoderDynamicTable>::InsertEntry(name, value);

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

const QpackEntry* QpackDecoderHeaderTable::LookupEntry(bool is_static,
                                                       uint64_t index) const {
  if (is_static) {
    if (index >= static_entries_.size()) {
      return nullptr;
    }

    return &static_entries_[index];
  }

  if (index < dropped_entry_count()) {
    return nullptr;
  }

  index -= dropped_entry_count();

  if (index >= dynamic_entries().size()) {
    return nullptr;
  }

  return &dynamic_entries()[index];
}

void QpackDecoderHeaderTable::RegisterObserver(uint64_t required_insert_count,
                                               Observer* observer) {
  QUICHE_DCHECK_GT(required_insert_count, 0u);
  observers_.insert({required_insert_count, observer});
}

void QpackDecoderHeaderTable::UnregisterObserver(uint64_t required_insert_count,
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
  QUICHE_NOTREACHED();
}

}  // namespace quic
