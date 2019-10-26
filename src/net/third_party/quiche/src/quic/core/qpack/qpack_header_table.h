// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_HEADER_TABLE_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_HEADER_TABLE_H_

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_entry.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_header_table.h"

namespace quic {

namespace test {

class QpackHeaderTablePeer;

}  // namespace test

using QpackEntry = spdy::HpackEntry;

// This class manages the QPACK static and dynamic tables.  For dynamic entries,
// it only has a concept of absolute indices.  The caller needs to perform the
// necessary transformations to and from relative indices and post-base indices.
class QUIC_EXPORT_PRIVATE QpackHeaderTable {
 public:
  using EntryTable = spdy::HpackHeaderTable::EntryTable;
  using EntryHasher = spdy::HpackHeaderTable::EntryHasher;
  using EntriesEq = spdy::HpackHeaderTable::EntriesEq;
  using UnorderedEntrySet = spdy::HpackHeaderTable::UnorderedEntrySet;
  using NameToEntryMap = spdy::HpackHeaderTable::NameToEntryMap;

  // Result of header table lookup.
  enum class MatchType { kNameAndValue, kName, kNoMatch };

  // Observer interface for dynamic table insertion.
  class Observer {
   public:
    virtual ~Observer() = default;

    // Called when inserted_entry_count() reaches the threshold the Observer was
    // registered with.  After this call the Observer automatically gets
    // deregistered.
    virtual void OnInsertCountReachedThreshold() = 0;
  };

  QpackHeaderTable();
  QpackHeaderTable(const QpackHeaderTable&) = delete;
  QpackHeaderTable& operator=(const QpackHeaderTable&) = delete;

  ~QpackHeaderTable();

  // Returns the entry at absolute index |index| from the static or dynamic
  // table according to |is_static|.  |index| is zero based for both the static
  // and the dynamic table.  The returned pointer is valid until the entry is
  // evicted, even if other entries are inserted into the dynamic table.
  // Returns nullptr if entry does not exist.
  const QpackEntry* LookupEntry(bool is_static, uint64_t index) const;

  // Returns the absolute index of an entry with matching name and value if such
  // exists, otherwise one with matching name is such exists.  |index| is zero
  // based for both the static and the dynamic table.
  MatchType FindHeaderField(QuicStringPiece name,
                            QuicStringPiece value,
                            bool* is_static,
                            uint64_t* index) const;

  // Insert (name, value) into the dynamic table.  May evict entries.  Returns a
  // pointer to the inserted owned entry on success.  Returns nullptr if entry
  // is larger than the capacity of the dynamic table.
  const QpackEntry* InsertEntry(QuicStringPiece name, QuicStringPiece value);

  // Returns the size of the largest entry that could be inserted into the
  // dynamic table without evicting entry |index|.  |index| might be larger than
  // inserted_entry_count(), in which case the capacity of the table is
  // returned.  |index| must not be smaller than dropped_entry_count().
  uint64_t MaxInsertSizeWithoutEvictingGivenEntry(uint64_t index) const;

  // Change dynamic table capacity to |capacity|.  Returns true on success.
  // Returns false is |capacity| exceeds maximum dynamic table capacity.
  bool SetDynamicTableCapacity(uint64_t capacity);

  // Set |maximum_dynamic_table_capacity_|.  The initial value is zero.  The
  // final value is determined by the decoder and is sent to the encoder as
  // SETTINGS_HEADER_TABLE_SIZE.  Therefore in the decoding context the final
  // value can be set upon connection establishment, whereas in the encoding
  // context it can be set when the SETTINGS frame is received.
  // This method must only be called at most once.
  void SetMaximumDynamicTableCapacity(uint64_t maximum_dynamic_table_capacity);

  // Register an observer to be notified when inserted_entry_count() reaches
  // |required_insert_count|.  After the notification, |observer| automatically
  // gets unregistered.
  void RegisterObserver(Observer* observer, uint64_t required_insert_count);

  // Used on request streams to encode and decode Required Insert Count.
  uint64_t max_entries() const { return max_entries_; }

  // The number of entries inserted to the dynamic table (including ones that
  // were dropped since).  Used for relative indexing on the encoder stream.
  uint64_t inserted_entry_count() const {
    return dynamic_entries_.size() + dropped_entry_count_;
  }

  // The number of entries dropped from the dynamic table.
  uint64_t dropped_entry_count() const { return dropped_entry_count_; }

  // Returns the draining index described at
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#avoiding-blocked-insertions.
  // Entries with an index larger than or equal to the draining index take up
  // approximately |1.0 - draining_fraction| of dynamic table capacity.  The
  // remaining capacity is taken up by draining entries and unused space.
  // The returned index might not be the index of a valid entry.
  uint64_t draining_index(float draining_fraction) const;

 private:
  friend class test::QpackHeaderTablePeer;

  // Evict entries from the dynamic table until table size is less than or equal
  // to current value of |dynamic_table_capacity_|.
  void EvictDownToCurrentCapacity();

  // Static Table

  // |static_entries_|, |static_index_|, |static_name_index_| are owned by
  // QpackStaticTable singleton.

  // Tracks QpackEntries by index.
  const EntryTable& static_entries_;

  // Tracks the unique static entry for a given header name and value.
  const UnorderedEntrySet& static_index_;

  // Tracks the first static entry for a given header name.
  const NameToEntryMap& static_name_index_;

  // Dynamic Table

  // Queue of dynamic table entries, for lookup by index.
  // |dynamic_entries_| owns the entries in the dynamic table.
  EntryTable dynamic_entries_;

  // An unordered set of QpackEntry pointers with a comparison operator that
  // only cares about name and value.  This allows fast lookup of the most
  // recently inserted dynamic entry for a given header name and value pair.
  // Entries point to entries owned by |dynamic_entries_|.
  UnorderedEntrySet dynamic_index_;

  // An unordered map of QpackEntry pointers keyed off header name.  This allows
  // fast lookup of the most recently inserted dynamic entry for a given header
  // name.  Entries point to entries owned by |dynamic_entries_|.
  NameToEntryMap dynamic_name_index_;

  // Size of the dynamic table.  This is the sum of the size of its entries.
  uint64_t dynamic_table_size_;

  // Dynamic Table Capacity is the maximum allowed value of
  // |dynamic_table_size_|.  Entries are evicted if necessary before inserting a
  // new entry to ensure that dynamic table size never exceeds capacity.
  // Initial value is |maximum_dynamic_table_capacity_|.  Capacity can be
  // changed by the encoder, as long as it does not exceed
  // |maximum_dynamic_table_capacity_|.
  uint64_t dynamic_table_capacity_;

  // Maximum allowed value of |dynamic_table_capacity|.  The initial value is
  // zero.  Can be changed by SetMaximumDynamicTableCapacity().
  uint64_t maximum_dynamic_table_capacity_;

  // MaxEntries, see Section 3.2.2.  Calculated based on
  // |maximum_dynamic_table_capacity_|.
  uint64_t max_entries_;

  // The number of entries dropped from the dynamic table.
  uint64_t dropped_entry_count_;

  // Data structure to hold an Observer and its threshold.
  struct ObserverWithThreshold {
    Observer* observer;
    uint64_t required_insert_count;
    bool operator>(const ObserverWithThreshold& other) const;
  };

  // Use std::greater so that entry with smallest |required_insert_count|
  // is on top.
  using ObserverHeap = std::priority_queue<ObserverWithThreshold,
                                           std::vector<ObserverWithThreshold>,
                                           std::greater<ObserverWithThreshold>>;

  // Observers waiting to be notified.
  ObserverHeap observers_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_HEADER_TABLE_H_
