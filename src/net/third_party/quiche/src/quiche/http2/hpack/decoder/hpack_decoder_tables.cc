// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_decoder_tables.h"

#include "absl/strings/str_cat.h"
#include "quiche/http2/hpack/http2_hpack_constants.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace {

std::vector<HpackStringPair>* MakeStaticTable() {
  auto* ptr = new std::vector<HpackStringPair>();
  ptr->reserve(kFirstDynamicTableIndex);
  ptr->emplace_back("", "");

#define STATIC_TABLE_ENTRY(name, value, index)               \
  QUICHE_DCHECK_EQ(ptr->size(), static_cast<size_t>(index)); \
  ptr->emplace_back(name, value)

#include "quiche/http2/hpack/hpack_static_table_entries.inc"

#undef STATIC_TABLE_ENTRY

  return ptr;
}

const std::vector<HpackStringPair>* GetStaticTable() {
  static const std::vector<HpackStringPair>* const g_static_table =
      MakeStaticTable();
  return g_static_table;
}

}  // namespace

HpackStringPair::HpackStringPair(std::string name, std::string value)
    : name(std::move(name)), value(std::move(value)) {
  QUICHE_DVLOG(3) << DebugString() << " ctor";
}

HpackStringPair::~HpackStringPair() {
  QUICHE_DVLOG(3) << DebugString() << " dtor";
}

std::string HpackStringPair::DebugString() const {
  return absl::StrCat("HpackStringPair(name=", name, ", value=", value, ")");
}

std::ostream& operator<<(std::ostream& os, const HpackStringPair& p) {
  os << p.DebugString();
  return os;
}

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

HpackDecoderDynamicTable::HpackDecoderDynamicTable()
    : insert_count_(kFirstDynamicTableIndex - 1) {}
HpackDecoderDynamicTable::~HpackDecoderDynamicTable() = default;

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
  HpackStringPair entry(std::move(name), std::move(value));
  size_t entry_size = entry.size();
  QUICHE_DVLOG(2) << "InsertEntry of size=" << entry_size
                  << "\n     name: " << entry.name
                  << "\n    value: " << entry.value;
  if (entry_size > size_limit_) {
    QUICHE_DVLOG(2) << "InsertEntry: entry larger than table, removing "
                    << table_.size() << " entries, of total size "
                    << current_size_ << " bytes.";
    table_.clear();
    current_size_ = 0;
    return;
  }
  ++insert_count_;
  size_t insert_limit = size_limit_ - entry_size;
  EnsureSizeNoMoreThan(insert_limit);
  table_.push_front(entry);
  current_size_ += entry_size;
  QUICHE_DVLOG(2) << "InsertEntry: current_size_=" << current_size_;
  QUICHE_DCHECK_GE(current_size_, entry_size);
  QUICHE_DCHECK_LE(current_size_, size_limit_);
}

const HpackStringPair* HpackDecoderDynamicTable::Lookup(size_t index) const {
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
                    << ", last entry size=" << table_.back().size();
    QUICHE_DCHECK_GE(current_size_, table_.back().size());
    current_size_ -= table_.back().size();
    table_.pop_back();
    // Empty IFF current_size_ == 0.
    QUICHE_DCHECK_EQ(table_.empty(), current_size_ == 0);
  }
}

HpackDecoderTables::HpackDecoderTables() = default;
HpackDecoderTables::~HpackDecoderTables() = default;

const HpackStringPair* HpackDecoderTables::Lookup(size_t index) const {
  if (index < kFirstDynamicTableIndex) {
    return static_table_.Lookup(index);
  } else {
    return dynamic_table_.Lookup(index - kFirstDynamicTableIndex);
  }
}

}  // namespace http2
