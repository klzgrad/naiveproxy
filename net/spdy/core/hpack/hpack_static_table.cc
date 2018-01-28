// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/core/hpack/hpack_static_table.h"

#include "base/logging.h"
#include "net/spdy/core/hpack/hpack_constants.h"
#include "net/spdy/core/hpack/hpack_entry.h"
#include "net/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

namespace net {

HpackStaticTable::HpackStaticTable() {}

HpackStaticTable::~HpackStaticTable() {}

void HpackStaticTable::Initialize(const HpackStaticEntry* static_entry_table,
                                  size_t static_entry_count) {
  CHECK(!IsInitialized());

  int total_insertions = 0;
  for (const HpackStaticEntry* it = static_entry_table;
       it != static_entry_table + static_entry_count; ++it) {
    static_entries_.push_back(
        HpackEntry(SpdyStringPiece(it->name, it->name_len),
                   SpdyStringPiece(it->value, it->value_len),
                   true,  // is_static
                   total_insertions));
    HpackEntry* entry = &static_entries_.back();
    CHECK(static_index_.insert(entry).second);
    // Multiple static entries may have the same name, so inserts may fail.
    static_name_index_.insert(std::make_pair(entry->name(), entry));

    ++total_insertions;
  }
}

bool HpackStaticTable::IsInitialized() const {
  return !static_entries_.empty();
}

size_t HpackStaticTable::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(static_entries_) +
         SpdyEstimateMemoryUsage(static_index_) +
         SpdyEstimateMemoryUsage(static_name_index_);
}

}  // namespace net
