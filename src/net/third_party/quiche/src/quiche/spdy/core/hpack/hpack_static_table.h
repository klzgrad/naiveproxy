// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HPACK_HPACK_STATIC_TABLE_H_
#define QUICHE_SPDY_CORE_HPACK_HPACK_STATIC_TABLE_H_

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/spdy/core/hpack/hpack_header_table.h"

namespace spdy {

struct HpackStaticEntry;

// Number of entries in the HPACK static table.
constexpr size_t kStaticTableSize = 61;

// HpackStaticTable provides |static_entries_| and |static_index_| for HPACK
// encoding and decoding contexts.  Once initialized, an instance is read only
// and may be accessed only through its const interface.  Such an instance may
// be shared accross multiple HPACK contexts.
class QUICHE_EXPORT HpackStaticTable {
 public:
  HpackStaticTable();
  ~HpackStaticTable();

  // Prepares HpackStaticTable by filling up static_entries_ and static_index_
  // from an array of struct HpackStaticEntry.  Must be called exactly once.
  void Initialize(const HpackStaticEntry* static_entry_table,
                  size_t static_entry_count);

  // Returns whether Initialize() has been called.
  bool IsInitialized() const;

  // Accessors.
  const HpackHeaderTable::StaticEntryTable& GetStaticEntries() const {
    return static_entries_;
  }
  const HpackHeaderTable::NameValueToEntryMap& GetStaticIndex() const {
    return static_index_;
  }
  const HpackHeaderTable::NameToEntryMap& GetStaticNameIndex() const {
    return static_name_index_;
  }

 private:
  HpackHeaderTable::StaticEntryTable static_entries_;
  // The following two members have string_views that point to strings stored in
  // |static_entries_|.
  HpackHeaderTable::NameValueToEntryMap static_index_;
  HpackHeaderTable::NameToEntryMap static_name_index_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HPACK_HPACK_STATIC_TABLE_H_
