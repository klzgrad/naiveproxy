// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_TABLES_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_TABLES_H_

// Static and dynamic tables for the HPACK decoder. See:
// http://httpwg.org/specs/rfc7541.html#indexing.tables

// Note that the Lookup methods return nullptr if the requested index was not
// found. This should be treated as a COMPRESSION error according to the HTTP/2
// spec, which is a connection level protocol error (i.e. the connection must
// be terminated). See these sections in the two RFCs:
// http://httpwg.org/specs/rfc7541.html#indexed.header.representation
// http://httpwg.org/specs/rfc7541.html#index.address.space
// http://httpwg.org/specs/rfc7540.html#HeaderBlock

#include <stddef.h>

#include <cstdint>
#include <vector>

#include "net/third_party/quiche/src/http2/hpack/hpack_string.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_containers.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class HpackDecoderTablesPeer;
}  // namespace test

// HpackDecoderTablesDebugListener supports a QUIC experiment, enabling
// the gathering of information about the time-line of use of HPACK
// dynamic table entries.
class QUICHE_EXPORT_PRIVATE HpackDecoderTablesDebugListener {
 public:
  HpackDecoderTablesDebugListener();
  virtual ~HpackDecoderTablesDebugListener();

  HpackDecoderTablesDebugListener(const HpackDecoderTablesDebugListener&) =
      delete;
  HpackDecoderTablesDebugListener& operator=(
      const HpackDecoderTablesDebugListener&) = delete;

  // The entry has been inserted into the dynamic table. insert_count starts at
  // 62 because 61 is the last index in the static table; insert_count increases
  // by 1 with each insert into the dynamic table; it is not incremented when
  // when a entry is too large to fit into the dynamic table at all (which has
  // the effect of emptying the dynamic table).
  // Returns a value that can be used as time_added in OnUseEntry.
  virtual int64_t OnEntryInserted(const HpackStringPair& entry,
                                  size_t insert_count) = 0;

  // The entry has been used, either for the name or for the name and value.
  // insert_count is the same as passed to OnEntryInserted when entry was
  // inserted to the dynamic table, and time_added is the value that was
  // returned by OnEntryInserted.
  virtual void OnUseEntry(const HpackStringPair& entry,
                          size_t insert_count,
                          int64_t time_added) = 0;
};

// See http://httpwg.org/specs/rfc7541.html#static.table.definition for the
// contents, and http://httpwg.org/specs/rfc7541.html#index.address.space for
// info about accessing the static table.
class QUICHE_EXPORT_PRIVATE HpackDecoderStaticTable {
 public:
  explicit HpackDecoderStaticTable(const std::vector<HpackStringPair>* table);
  // Uses a global table shared by all threads.
  HpackDecoderStaticTable();

  // If index is valid, returns a pointer to the entry, otherwise returns
  // nullptr.
  const HpackStringPair* Lookup(size_t index) const;

 private:
  friend class test::HpackDecoderTablesPeer;
  const std::vector<HpackStringPair>* const table_;
};

// HpackDecoderDynamicTable implements HPACK compression feature "indexed
// headers"; previously sent headers may be referenced later by their index
// in the dynamic table. See these sections of the RFC:
//   http://httpwg.org/specs/rfc7541.html#dynamic.table
//   http://httpwg.org/specs/rfc7541.html#dynamic.table.management
class QUICHE_EXPORT_PRIVATE HpackDecoderDynamicTable {
 public:
  HpackDecoderDynamicTable();
  ~HpackDecoderDynamicTable();

  HpackDecoderDynamicTable(const HpackDecoderDynamicTable&) = delete;
  HpackDecoderDynamicTable& operator=(const HpackDecoderDynamicTable&) = delete;

  // Set the listener to be notified of insertions into this table, and later
  // uses of those entries. Added for evaluation of changes to QUIC's use
  // of HPACK.
  void set_debug_listener(HpackDecoderTablesDebugListener* debug_listener) {
    debug_listener_ = debug_listener;
  }

  // Sets a new size limit, received from the peer; performs evictions if
  // necessary to ensure that the current size does not exceed the new limit.
  // The caller needs to have validated that size_limit does not
  // exceed the acknowledged value of SETTINGS_HEADER_TABLE_SIZE.
  void DynamicTableSizeUpdate(size_t size_limit);

  // Insert entry if possible.
  // If entry is too large to insert, then dynamic table will be empty.
  void Insert(const HpackString& name, const HpackString& value);

  // If index is valid, returns a pointer to the entry, otherwise returns
  // nullptr.
  const HpackStringPair* Lookup(size_t index) const;

  size_t size_limit() const { return size_limit_; }
  size_t current_size() const { return current_size_; }

 private:
  friend class test::HpackDecoderTablesPeer;
  struct HpackDecoderTableEntry : public HpackStringPair {
    HpackDecoderTableEntry(const HpackString& name, const HpackString& value);
    int64_t time_added;
  };

  // Drop older entries to ensure the size is not greater than limit.
  void EnsureSizeNoMoreThan(size_t limit);

  // Removes the oldest dynamic table entry.
  void RemoveLastEntry();

  Http2Deque<HpackDecoderTableEntry> table_;

  // The last received DynamicTableSizeUpdate value, initialized to
  // SETTINGS_HEADER_TABLE_SIZE.
  size_t size_limit_ = Http2SettingsInfo::DefaultHeaderTableSize();

  size_t current_size_ = 0;

  // insert_count_ and debug_listener_ are used by a QUIC experiment; remove
  // when the experiment is done.
  size_t insert_count_;
  HpackDecoderTablesDebugListener* debug_listener_;
};

class QUICHE_EXPORT_PRIVATE HpackDecoderTables {
 public:
  HpackDecoderTables();
  ~HpackDecoderTables();

  HpackDecoderTables(const HpackDecoderTables&) = delete;
  HpackDecoderTables& operator=(const HpackDecoderTables&) = delete;

  // Set the listener to be notified of insertions into the dynamic table, and
  // later uses of those entries. Added for evaluation of changes to QUIC's use
  // of HPACK.
  void set_debug_listener(HpackDecoderTablesDebugListener* debug_listener);

  // Sets a new size limit, received from the peer; performs evictions if
  // necessary to ensure that the current size does not exceed the new limit.
  // The caller needs to have validated that size_limit does not
  // exceed the acknowledged value of SETTINGS_HEADER_TABLE_SIZE.
  void DynamicTableSizeUpdate(size_t size_limit) {
    dynamic_table_.DynamicTableSizeUpdate(size_limit);
  }

  // Insert entry if possible.
  // If entry is too large to insert, then dynamic table will be empty.
  // TODO(jamessynge): Add methods for moving the string(s) into the table,
  // or for otherwise avoiding unnecessary copies.
  void Insert(const HpackString& name, const HpackString& value) {
    dynamic_table_.Insert(name, value);
  }

  // If index is valid, returns a pointer to the entry, otherwise returns
  // nullptr.
  const HpackStringPair* Lookup(size_t index) const;

  // The size limit that the peer (the HPACK encoder) has told the decoder it is
  // currently operating with. Defaults to SETTINGS_HEADER_TABLE_SIZE, 4096.
  size_t header_table_size_limit() const { return dynamic_table_.size_limit(); }

  // Sum of the sizes of the dynamic table entries.
  size_t current_header_table_size() const {
    return dynamic_table_.current_size();
  }

 private:
  friend class test::HpackDecoderTablesPeer;
  HpackDecoderStaticTable static_table_;
  HpackDecoderDynamicTable dynamic_table_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_TABLES_H_
