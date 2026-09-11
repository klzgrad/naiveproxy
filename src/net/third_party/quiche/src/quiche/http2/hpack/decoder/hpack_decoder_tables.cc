// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_decoder_tables.h"

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "quiche/http2/hpack/hpack_constants.h"
#include "quiche/http2/hpack/hpack_static_table.h"
#include "quiche/http2/hpack/http2_hpack_constants.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

void HpackDecoderDynamicTable::DynamicTableSizeUpdate(size_t size_limit) {
  QUICHE_DVLOG(3) << "HpackDecoderDynamicTable::DynamicTableSizeUpdate "
                  << size_limit;
  EnsureSizeNoMoreThan(size_limit);
  QUICHE_DCHECK_LE(current_size_, size_limit);
  size_limit_ = size_limit;
}

// TODO(jamessynge): Check somewhere before here that names received from the
// peer are valid (e.g. are lower-case, no whitespace, etc.).
void HpackDecoderDynamicTable::Insert(std::string name, std::string value) {
  HpackEntry entry(std::move(name), std::move(value));
  size_t entry_size = entry.Size();
  QUICHE_DVLOG(2) << "InsertEntry of size=" << entry_size
                  << "\n     name: " << entry.name()
                  << "\n    value: " << entry.value();
  if (entry_size > size_limit_) {
    QUICHE_DVLOG(2) << "InsertEntry: entry larger than table, removing "
                    << table_.size() << " entries, of total size "
                    << current_size_ << " bytes.";
    table_.clear();
    current_size_ = 0;
    return;
  }
  size_t insert_limit = size_limit_ - entry_size;
  EnsureSizeNoMoreThan(insert_limit);
  table_.push_front(std::move(entry));
  current_size_ += entry_size;
  QUICHE_DVLOG(2) << "InsertEntry: current_size_=" << current_size_;
  QUICHE_DCHECK_GE(current_size_, entry_size);
  QUICHE_DCHECK_LE(current_size_, size_limit_);
}

const HpackEntry* HpackDecoderDynamicTable::Lookup(size_t index) const {
  if (index < table_.size()) {
    return &table_[index];
  }
  return nullptr;
}

void HpackDecoderDynamicTable::EnsureSizeNoMoreThan(size_t limit) {
  QUICHE_DVLOG(2) << "EnsureSizeNoMoreThan limit=" << limit
                  << ", current_size_=" << current_size_;
  // Not the most efficient choice, but any easy way to start.
  while (current_size_ > limit) {
    RemoveLastEntry();
  }
  QUICHE_DCHECK_LE(current_size_, limit);
}

void HpackDecoderDynamicTable::RemoveLastEntry() {
  QUICHE_DCHECK(!table_.empty());
  if (!table_.empty()) {
    QUICHE_DVLOG(2) << "RemoveLastEntry current_size_=" << current_size_
                    << ", last entry size=" << table_.back().Size();
    QUICHE_DCHECK_GE(current_size_, table_.back().Size());
    current_size_ -= table_.back().Size();
    table_.pop_back();
    // Empty IFF current_size_ == 0.
    QUICHE_DCHECK_EQ(table_.empty(), current_size_ == 0);
  }
}

HpackDecoderTables::HpackDecoderTables()
    : static_entries_(spdy::ObtainHpackStaticTable().GetStaticEntries()) {}

const HpackEntry* HpackDecoderTables::Lookup(size_t index) const {
  if (index == 0) {
    return nullptr;
  } else if (index < kFirstDynamicTableIndex) {
    return &static_entries_[index - 1];
  } else {
    return dynamic_table_.Lookup(index - kFirstDynamicTableIndex);
  }
}

}  // namespace http2
