// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/hpack/hpack_static_table.h"

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/hpack/hpack_constants.h"
#include "quiche/spdy/core/hpack/hpack_entry.h"

namespace spdy {

HpackStaticTable::HpackStaticTable() = default;

HpackStaticTable::~HpackStaticTable() = default;

void HpackStaticTable::Initialize(const HpackStaticEntry* static_entry_table,
                                  size_t static_entry_count) {
  QUICHE_CHECK(!IsInitialized());

  static_entries_.reserve(static_entry_count);

  for (const HpackStaticEntry* it = static_entry_table;
       it != static_entry_table + static_entry_count; ++it) {
    std::string name(it->name, it->name_len);
    std::string value(it->value, it->value_len);
    static_entries_.push_back(HpackEntry(std::move(name), std::move(value)));
  }

  // |static_entries_| will not be mutated any more.  Therefore its entries will
  // remain stable even if the container does not have iterator stability.
  int insertion_count = 0;
  for (const auto& entry : static_entries_) {
    auto result = static_index_.insert(std::make_pair(
        HpackLookupEntry{entry.name(), entry.value()}, insertion_count));
    QUICHE_CHECK(result.second);

    // Multiple static entries may have the same name, so inserts may fail.
    static_name_index_.insert(std::make_pair(entry.name(), insertion_count));

    ++insertion_count;
  }
}

bool HpackStaticTable::IsInitialized() const {
  return !static_entries_.empty();
}

}  // namespace spdy
