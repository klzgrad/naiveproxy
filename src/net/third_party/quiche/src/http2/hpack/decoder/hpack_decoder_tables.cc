// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_tables.h"

#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {
namespace {

std::vector<HpackStringPair>* MakeStaticTable() {
  auto* ptr = new std::vector<HpackStringPair>();
  ptr->reserve(kFirstDynamicTableIndex);
  ptr->emplace_back("", "");

#define STATIC_TABLE_ENTRY(name, value, index)        \
  DCHECK_EQ(ptr->size(), static_cast<size_t>(index)); \
  ptr->emplace_back(name, value)

#include "net/third_party/quiche/src/http2/hpack/hpack_static_table_entries.inc"

#undef STATIC_TABLE_ENTRY

  return ptr;
}

const std::vector<HpackStringPair>* GetStaticTable() {
  static const std::vector<HpackStringPair>* const g_static_table =
      MakeStaticTable();
  return g_static_table;
}

}  // namespace

HpackDecoderTablesDebugListener::HpackDecoderTablesDebugListener() = default;
HpackDecoderTablesDebugListener::~HpackDecoderTablesDebugListener() = default;

HpackDecoderStaticTable::HpackDecoderStaticTable(
    const std::vector<HpackStringPair>* table)
    : table_(table) {}

HpackDecoderStaticTable::HpackDecoderStaticTable() : table_(GetStaticTable()) {}

const HpackStringPair* HpackDecoderStaticTable::Lookup(size_t index) const {
  if (0 < index && index < kFirstDynamicTableIndex) {
    return &((*table_)[index]);
  }
  return nullptr;
}

HpackDecoderDynamicTable::HpackDecoderTableEntry::HpackDecoderTableEntry(
    const HpackString& name,
    const HpackString& value)
    : HpackStringPair(name, value) {}

HpackDecoderDynamicTable::HpackDecoderDynamicTable()
    : insert_count_(kFirstDynamicTableIndex - 1), debug_listener_(nullptr) {}
HpackDecoderDynamicTable::~HpackDecoderDynamicTable() = default;

void HpackDecoderDynamicTable::DynamicTableSizeUpdate(size_t size_limit) {
  HTTP2_DVLOG(3) << "HpackDecoderDynamicTable::DynamicTableSizeUpdate "
                 << size_limit;
  EnsureSizeNoMoreThan(size_limit);
  DCHECK_LE(current_size_, size_limit);
  size_limit_ = size_limit;
}

// TODO(jamessynge): Check somewhere before here that names received from the
// peer are valid (e.g. are lower-case, no whitespace, etc.).
void HpackDecoderDynamicTable::Insert(const HpackString& name,
                                      const HpackString& value) {
  HpackDecoderTableEntry entry(name, value);
  size_t entry_size = entry.size();
  HTTP2_DVLOG(2) << "InsertEntry of size=" << entry_size
                 << "\n     name: " << name << "\n    value: " << value;
  if (entry_size > size_limit_) {
    HTTP2_DVLOG(2) << "InsertEntry: entry larger than table, removing "
                   << table_.size() << " entries, of total size "
                   << current_size_ << " bytes.";
    table_.clear();
    current_size_ = 0;
    return;
  }
  ++insert_count_;
  if (debug_listener_ != nullptr) {
    entry.time_added = debug_listener_->OnEntryInserted(entry, insert_count_);
    HTTP2_DVLOG(2) << "OnEntryInserted returned time_added=" << entry.time_added
                   << " for insert_count_=" << insert_count_;
  }
  size_t insert_limit = size_limit_ - entry_size;
  EnsureSizeNoMoreThan(insert_limit);
  table_.push_front(entry);
  current_size_ += entry_size;
  HTTP2_DVLOG(2) << "InsertEntry: current_size_=" << current_size_;
  DCHECK_GE(current_size_, entry_size);
  DCHECK_LE(current_size_, size_limit_);
}

const HpackStringPair* HpackDecoderDynamicTable::Lookup(size_t index) const {
  if (index < table_.size()) {
    const HpackDecoderTableEntry& entry = table_[index];
    if (debug_listener_ != nullptr) {
      size_t insert_count_of_index = insert_count_ + table_.size() - index;
      debug_listener_->OnUseEntry(entry, insert_count_of_index,
                                  entry.time_added);
    }
    return &entry;
  }
  return nullptr;
}

void HpackDecoderDynamicTable::EnsureSizeNoMoreThan(size_t limit) {
  HTTP2_DVLOG(2) << "EnsureSizeNoMoreThan limit=" << limit
                 << ", current_size_=" << current_size_;
  // Not the most efficient choice, but any easy way to start.
  while (current_size_ > limit) {
    RemoveLastEntry();
  }
  DCHECK_LE(current_size_, limit);
}

void HpackDecoderDynamicTable::RemoveLastEntry() {
  DCHECK(!table_.empty());
  if (!table_.empty()) {
    HTTP2_DVLOG(2) << "RemoveLastEntry current_size_=" << current_size_
                   << ", last entry size=" << table_.back().size();
    DCHECK_GE(current_size_, table_.back().size());
    current_size_ -= table_.back().size();
    table_.pop_back();
    // Empty IFF current_size_ == 0.
    DCHECK_EQ(table_.empty(), current_size_ == 0);
  }
}

HpackDecoderTables::HpackDecoderTables() = default;
HpackDecoderTables::~HpackDecoderTables() = default;

void HpackDecoderTables::set_debug_listener(
    HpackDecoderTablesDebugListener* debug_listener) {
  dynamic_table_.set_debug_listener(debug_listener);
}

const HpackStringPair* HpackDecoderTables::Lookup(size_t index) const {
  if (index < kFirstDynamicTableIndex) {
    return static_table_.Lookup(index);
  } else {
    return dynamic_table_.Lookup(index - kFirstDynamicTableIndex);
  }
}

}  // namespace http2
