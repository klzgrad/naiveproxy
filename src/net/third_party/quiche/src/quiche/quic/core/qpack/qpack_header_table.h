// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_HEADER_TABLE_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_HEADER_TABLE_H_

#include <cstdint>
#include <deque>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/spdy/core/hpack/hpack_entry.h"
#include "quiche/spdy/core/hpack/hpack_header_table.h"

namespace quic {

using QpackEntry = spdy::HpackEntry;
using QpackLookupEntry = spdy::HpackLookupEntry;
constexpr size_t kQpackEntrySizeOverhead = spdy::kHpackEntrySizeOverhead;

// Encoder needs pointer stability for |dynamic_index_| and
// |dynamic_name_index_|.  However, it does not need random access.
using QpackEncoderDynamicTable =
    quiche::QuicheCircularDeque<std::unique_ptr<QpackEntry>>;

// Decoder needs random access for LookupEntry().
// However, it does not need pointer stability.
using QpackDecoderDynamicTable = quiche::QuicheCircularDeque<QpackEntry>;

// This is a base class for encoder and decoder classes that manage the QPACK
// static and dynamic tables.  For dynamic entries, it only has a concept of
// absolute indices.  The caller needs to perform the necessary transformations
// to and from relative indices and post-base indices.
template <typename DynamicEntryTable>
class QUICHE_EXPORT QpackHeaderTableBase {
 public:
  QpackHeaderTableBase();
  QpackHeaderTableBase(const QpackHeaderTableBase&) = delete;
  QpackHeaderTableBase& operator=(const QpackHeaderTableBase&) = delete;

  virtual ~QpackHeaderTableBase() = default;

  // Returns whether an entry with |name| and |value| has a size (including
  // overhead) that is smaller than or equal to the capacity of the dynamic
  // table.
  bool EntryFitsDynamicTableCapacity(absl::string_view name,
                                     absl::string_view value) const;

  // Inserts (name, value) into the dynamic table.  Entry must not be larger
  // than the capacity of the dynamic table.  May evict entries.  |name| and
  // |value| are copied first, therefore it is safe for them to point to an
  // entry in the dynamic table, even if it is about to be evicted, or even if
  // the underlying container might move entries around when resizing for
  // insertion.
  // Returns the absolute index of the inserted dynamic table entry.
  virtual uint64_t InsertEntry(absl::string_view name, absl::string_view value);

  // Change dynamic table capacity to |capacity|.  Returns true on success.
  // Returns false is |capacity| exceeds maximum dynamic table capacity.
  bool SetDynamicTableCapacity(uint64_t capacity);

  // Set |maximum_dynamic_table_capacity_|.  The initial value is zero.  The
  // final value is determined by the decoder and is sent to the encoder as
  // SETTINGS_HEADER_TABLE_SIZE.  Therefore in the decoding context the final
  // value can be set upon connection establishment, whereas in the encoding
  // context it can be set when the SETTINGS frame is received.
  // This method must only be called at most once.
  // Returns true if |maximum_dynamic_table_capacity| is set for the first time
  // or if it doesn't change current value. The setting is not changed when
  // returning false.
  bool SetMaximumDynamicTableCapacity(uint64_t maximum_dynamic_table_capacity);

  uint64_t dynamic_table_size() const { return dynamic_table_size_; }
  uint64_t dynamic_table_capacity() const { return dynamic_table_capacity_; }
  uint64_t maximum_dynamic_table_capacity() const {
    return maximum_dynamic_table_capacity_;
  }
  uint64_t max_entries() const { return max_entries_; }

  // The number of entries inserted to the dynamic table (including ones that
  // were dropped since).  Used for relative indexing on the encoder stream.
  uint64_t inserted_entry_count() const {
    return dynamic_entries_.size() + dropped_entry_count_;
  }

  // The number of entries dropped from the dynamic table.
  uint64_t dropped_entry_count() const { return dropped_entry_count_; }

  void set_dynamic_table_entry_referenced() {
    dynamic_table_entry_referenced_ = true;
  }
  bool dynamic_table_entry_referenced() const {
    return dynamic_table_entry_referenced_;
  }

 protected:
  // Removes a single entry from the end of the dynamic table, updates
  // |dynamic_table_size_| and |dropped_entry_count_|.
  virtual void RemoveEntryFromEnd();

  const DynamicEntryTable& dynamic_entries() const { return dynamic_entries_; }

 private:
  // Evict entries from the dynamic table until table size is less than or equal
  // to |capacity|.
  void EvictDownToCapacity(uint64_t capacity);

  // Dynamic Table entries.
  DynamicEntryTable dynamic_entries_;

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
  // |maximum_dynamic_table_capacity_|.  Used on request streams to encode and
  // decode Required Insert Count.
  uint64_t max_entries_;

  // The number of entries dropped from the dynamic table.
  uint64_t dropped_entry_count_;

  // True if any dynamic table entries have been referenced from a header block.
  // Set directly by the encoder or decoder.  Used for stats.
  bool dynamic_table_entry_referenced_;
};

template <typename DynamicEntryTable>
QpackHeaderTableBase<DynamicEntryTable>::QpackHeaderTableBase()
    : dynamic_table_size_(0),
      dynamic_table_capacity_(0),
      maximum_dynamic_table_capacity_(0),
      max_entries_(0),
      dropped_entry_count_(0),
      dynamic_table_entry_referenced_(false) {}

template <typename DynamicEntryTable>
bool QpackHeaderTableBase<DynamicEntryTable>::EntryFitsDynamicTableCapacity(
    absl::string_view name, absl::string_view value) const {
  return QpackEntry::Size(name, value) <= dynamic_table_capacity_;
}

namespace internal {

QUICHE_EXPORT inline size_t GetSize(const QpackEntry& entry) {
  return entry.Size();
}

QUICHE_EXPORT inline size_t GetSize(const std::unique_ptr<QpackEntry>& entry) {
  return entry->Size();
}

QUICHE_EXPORT inline std::unique_ptr<QpackEntry> NewEntry(
    std::string name, std::string value, QpackEncoderDynamicTable& /*t*/) {
  return std::make_unique<QpackEntry>(std::move(name), std::move(value));
}

QUICHE_EXPORT inline QpackEntry NewEntry(std::string name, std::string value,
                                         QpackDecoderDynamicTable& /*t*/) {
  return QpackEntry{std::move(name), std::move(value)};
}

}  // namespace internal

template <typename DynamicEntryTable>
uint64_t QpackHeaderTableBase<DynamicEntryTable>::InsertEntry(
    absl::string_view name, absl::string_view value) {
  QUICHE_DCHECK(EntryFitsDynamicTableCapacity(name, value));

  const uint64_t index = dropped_entry_count_ + dynamic_entries_.size();

  // Copy name and value before modifying the container, because evicting
  // entries or even inserting a new one might invalidate |name| or |value| if
  // they point to an entry.
  auto new_entry = internal::NewEntry(std::string(name), std::string(value),
                                      dynamic_entries_);
  const size_t entry_size = internal::GetSize(new_entry);
  EvictDownToCapacity(dynamic_table_capacity_ - entry_size);

  dynamic_table_size_ += entry_size;
  dynamic_entries_.push_back(std::move(new_entry));

  return index;
}

template <typename DynamicEntryTable>
bool QpackHeaderTableBase<DynamicEntryTable>::SetDynamicTableCapacity(
    uint64_t capacity) {
  if (capacity > maximum_dynamic_table_capacity_) {
    return false;
  }

  dynamic_table_capacity_ = capacity;
  EvictDownToCapacity(capacity);

  QUICHE_DCHECK_LE(dynamic_table_size_, dynamic_table_capacity_);

  return true;
}

template <typename DynamicEntryTable>
bool QpackHeaderTableBase<DynamicEntryTable>::SetMaximumDynamicTableCapacity(
    uint64_t maximum_dynamic_table_capacity) {
  if (maximum_dynamic_table_capacity_ == 0) {
    maximum_dynamic_table_capacity_ = maximum_dynamic_table_capacity;
    max_entries_ = maximum_dynamic_table_capacity / 32;
    return true;
  }
  // If the value is already set, it should not be changed.
  return maximum_dynamic_table_capacity == maximum_dynamic_table_capacity_;
}

template <typename DynamicEntryTable>
void QpackHeaderTableBase<DynamicEntryTable>::RemoveEntryFromEnd() {
  const uint64_t entry_size = internal::GetSize(dynamic_entries_.front());
  QUICHE_DCHECK_GE(dynamic_table_size_, entry_size);
  dynamic_table_size_ -= entry_size;

  dynamic_entries_.pop_front();
  ++dropped_entry_count_;
}

template <typename DynamicEntryTable>
void QpackHeaderTableBase<DynamicEntryTable>::EvictDownToCapacity(
    uint64_t capacity) {
  while (dynamic_table_size_ > capacity) {
    QUICHE_DCHECK(!dynamic_entries_.empty());
    RemoveEntryFromEnd();
  }
}

class QUICHE_EXPORT QpackEncoderHeaderTable
    : public QpackHeaderTableBase<QpackEncoderDynamicTable> {
 public:
  // Result of header table lookup.
  enum class MatchType {
    kNameAndValue,  // Returned entry matches name and value.
    kName,          // Returned entry matches name only.
    kNoMatch        // No matching entry found.
  };

  // Return type of FindHeaderField() and FindHeaderName(), describing the
  // nature of the match, and the location and index of the matching entry.
  // The value of `is_static` and `index` is undefined if
  // `match_type == MatchType::kNoMatch`.
  struct MatchResult {
    MatchType match_type;
    bool is_static;
    // `index` is zero-based for both static and dynamic table entries.
    uint64_t index;
  };

  QpackEncoderHeaderTable();
  ~QpackEncoderHeaderTable() override = default;

  uint64_t InsertEntry(absl::string_view name,
                       absl::string_view value) override;

  // FindHeaderField() and FindHeaderName() both prefer static table entries to
  // dynamic ones. They both prefer lower index entries within the static table,
  // and higher index (more recent) entries within the dynamic table.

  // Returns `kNameAndValue` and an entry with matching name and value if such
  // exists.
  // Otherwise, returns `kName` and an entry with matching name is such exists.
  // Otherwise, returns `kNoMatch`.
  MatchResult FindHeaderField(absl::string_view name,
                              absl::string_view value) const;

  // Returns `kName` and an entry with matching name is such exists.
  // Otherwise, returns `kNoMatch`.
  MatchResult FindHeaderName(absl::string_view name) const;

  // Returns the size of the largest entry that could be inserted into the
  // dynamic table without evicting entry |index|.  |index| might be larger than
  // inserted_entry_count(), in which case the capacity of the table is
  // returned.  |index| must not be smaller than dropped_entry_count().
  uint64_t MaxInsertSizeWithoutEvictingGivenEntry(uint64_t index) const;

  // Returns the draining index described at
  // https://rfc-editor.org/rfc/rfc9204.html#section-2.1.1.1.
  // Entries with an index larger than or equal to the draining index take up
  // approximately |1.0 - draining_fraction| of dynamic table capacity.  The
  // remaining capacity is taken up by draining entries and unused space.
  // The returned index might not be the index of a valid entry.
  uint64_t draining_index(float draining_fraction) const;

 protected:
  void RemoveEntryFromEnd() override;

 private:
  using NameValueToEntryMap = spdy::HpackHeaderTable::NameValueToEntryMap;
  using NameToEntryMap = spdy::HpackHeaderTable::NameToEntryMap;

  // Static Table

  // |static_index_| and |static_name_index_| are owned by QpackStaticTable
  // singleton.

  // Tracks the unique static entry for a given header name and value.
  const NameValueToEntryMap& static_index_;

  // Tracks the first static entry for a given header name.
  const NameToEntryMap& static_name_index_;

  // Dynamic Table

  // An unordered set of QpackEntry pointers with a comparison operator that
  // only cares about name and value.  This allows fast lookup of the most
  // recently inserted dynamic entry for a given header name and value pair.
  // Entries point to entries owned by |QpackHeaderTableBase::dynamic_entries_|.
  NameValueToEntryMap dynamic_index_;

  // An unordered map of QpackEntry pointers keyed off header name.  This allows
  // fast lookup of the most recently inserted dynamic entry for a given header
  // name.  Entries point to entries owned by
  // |QpackHeaderTableBase::dynamic_entries_|.
  NameToEntryMap dynamic_name_index_;
};

class QUICHE_EXPORT QpackDecoderHeaderTable
    : public QpackHeaderTableBase<QpackDecoderDynamicTable> {
 public:
  // Observer interface for dynamic table insertion.
  class QUICHE_EXPORT Observer {
   public:
    virtual ~Observer() = default;

    // Called when inserted_entry_count() reaches the threshold the Observer was
    // registered with.  After this call the Observer automatically gets
    // deregistered.
    virtual void OnInsertCountReachedThreshold() = 0;

    // Called when QpackDecoderHeaderTable is destroyed to let the Observer know
    // that it must not call UnregisterObserver().
    virtual void Cancel() = 0;
  };

  QpackDecoderHeaderTable();
  ~QpackDecoderHeaderTable() override;

  uint64_t InsertEntry(absl::string_view name,
                       absl::string_view value) override;

  // Returns the entry at absolute index |index| from the static or dynamic
  // table according to |is_static|.  |index| is zero based for both the static
  // and the dynamic table.  The returned pointer is valid until the entry is
  // evicted, even if other entries are inserted into the dynamic table.
  // Returns nullptr if entry does not exist.
  const QpackEntry* LookupEntry(bool is_static, uint64_t index) const;

  // Register an observer to be notified when inserted_entry_count() reaches
  // |required_insert_count|.  After the notification, |observer| automatically
  // gets unregistered.  Each observer must only be registered at most once.
  void RegisterObserver(uint64_t required_insert_count, Observer* observer);

  // Unregister previously registered observer.  Must be called with the same
  // |required_insert_count| value that |observer| was registered with.  Must be
  // called before an observer still waiting for notification is destroyed,
  // unless QpackDecoderHeaderTable already called Observer::Cancel(), in which
  // case this method must not be called.
  void UnregisterObserver(uint64_t required_insert_count, Observer* observer);

 private:
  // Static Table entries.  Owned by QpackStaticTable singleton.
  using StaticEntryTable = spdy::HpackHeaderTable::StaticEntryTable;
  const StaticEntryTable& static_entries_;

  // Observers waiting to be notified, sorted by required insert count.
  std::multimap<uint64_t, Observer*> observers_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_HEADER_TABLE_H_
