// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HPACK_HPACK_HEADER_TABLE_H_
#define QUICHE_SPDY_CORE_HPACK_HPACK_HEADER_TABLE_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/spdy/core/hpack/hpack_entry.h"

// All section references below are to http://tools.ietf.org/html/rfc7541.

namespace spdy {

namespace test {
class HpackHeaderTablePeer;
}  // namespace test

// Return value of GetByName() and GetByNameAndValue() if matching entry is not
// found.  This value is never used in HPACK for indexing entries, see
// https://httpwg.org/specs/rfc7541.html#index.address.space.
inline constexpr size_t kHpackEntryNotFound = 0;

// A data structure for the static table (2.3.1) and the dynamic table (2.3.2).
class QUICHE_EXPORT HpackHeaderTable {
 public:
  friend class test::HpackHeaderTablePeer;

  // Use a lightweight, memory efficient container for the static table, which
  // is initialized once and never changed after.
  using StaticEntryTable = std::vector<HpackEntry>;

  // HpackHeaderTable takes advantage of the deque property that references
  // remain valid, so long as insertions & deletions are at the head & tail.
  using DynamicEntryTable =
      quiche::QuicheCircularDeque<std::unique_ptr<HpackEntry>>;

  using NameValueToEntryMap = absl::flat_hash_map<HpackLookupEntry, size_t>;
  using NameToEntryMap = absl::flat_hash_map<absl::string_view, size_t>;

  HpackHeaderTable();
  HpackHeaderTable(const HpackHeaderTable&) = delete;
  HpackHeaderTable& operator=(const HpackHeaderTable&) = delete;

  ~HpackHeaderTable();

  // Last-acknowledged value of SETTINGS_HEADER_TABLE_SIZE.
  size_t settings_size_bound() const { return settings_size_bound_; }

  // Current and maximum estimated byte size of the table, as described in
  // 4.1. Notably, this is /not/ the number of entries in the table.
  size_t size() const { return size_; }
  size_t max_size() const { return max_size_; }

  // The HPACK indexing scheme used by GetByName() and GetByNameAndValue() is
  // defined at https://httpwg.org/specs/rfc7541.html#index.address.space.

  // Returns the index of the lowest-index entry matching |name|,
  // or kHpackEntryNotFound if no matching entry is found.
  size_t GetByName(absl::string_view name);

  // Returns the index of the lowest-index entry matching |name| and |value|,
  // or kHpackEntryNotFound if no matching entry is found.
  size_t GetByNameAndValue(absl::string_view name, absl::string_view value);

  // Sets the maximum size of the header table, evicting entries if
  // necessary as described in 5.2.
  void SetMaxSize(size_t max_size);

  // Sets the SETTINGS_HEADER_TABLE_SIZE bound of the table. Will call
  // SetMaxSize() as needed to preserve max_size() <= settings_size_bound().
  void SetSettingsHeaderTableSize(size_t settings_size);

  // Determine the set of entries which would be evicted by the insertion
  // of |name| & |value| into the table, as per section 4.4. No eviction
  // actually occurs. The set is returned via range [begin_out, end_out).
  void EvictionSet(absl::string_view name, absl::string_view value,
                   DynamicEntryTable::iterator* begin_out,
                   DynamicEntryTable::iterator* end_out);

  // Adds an entry for the representation, evicting entries as needed. |name|
  // and |value| must not point to an entry in |dynamic_entries_| which is about
  // to be evicted, but they may point to an entry which is not.
  // The added HpackEntry is returned, or NULL is returned if all entries were
  // evicted and the empty table is of insufficent size for the representation.
  const HpackEntry* TryAddEntry(absl::string_view name,
                                absl::string_view value);

 private:
  // Returns number of evictions required to enter |name| & |value|.
  size_t EvictionCountForEntry(absl::string_view name,
                               absl::string_view value) const;

  // Returns number of evictions required to reclaim |reclaim_size| table size.
  size_t EvictionCountToReclaim(size_t reclaim_size) const;

  // Evicts |count| oldest entries from the table.
  void Evict(size_t count);

  // |static_entries_|, |static_index_|, and |static_name_index_| are owned by
  // HpackStaticTable singleton.

  // Stores HpackEntries.
  const StaticEntryTable& static_entries_;
  DynamicEntryTable dynamic_entries_;

  // Tracks the index of the unique HpackEntry for a given header name and
  // value.  Keys consist of string_views that point to strings stored in
  // |static_entries_|.
  const NameValueToEntryMap& static_index_;

  // Tracks the index of the first static entry for each name in the static
  // table.  Each key is a string_view that points to a name string stored in
  // |static_entries_|.
  const NameToEntryMap& static_name_index_;

  // Tracks the index of the most recently inserted HpackEntry for a given
  // header name and value.  Keys consist of string_views that point to strings
  // stored in |dynamic_entries_|.
  NameValueToEntryMap dynamic_index_;

  // Tracks the index of the most recently inserted HpackEntry for a given
  // header name.  Each key is a string_view that points to a name string stored
  // in |dynamic_entries_|.
  NameToEntryMap dynamic_name_index_;

  // Last acknowledged value for SETTINGS_HEADER_TABLE_SIZE.
  size_t settings_size_bound_;

  // Estimated current and maximum byte size of the table.
  // |max_size_| <= |settings_size_bound_|
  size_t size_;
  size_t max_size_;

  // Total number of dynamic table insertions so far
  // (including entries that have been evicted).
  size_t dynamic_table_insertions_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HPACK_HPACK_HEADER_TABLE_H_
