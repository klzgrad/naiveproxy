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
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

#include "quiche/http2/http2_constants.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_circular_deque.h"

namespace http2 {
namespace test {
class HpackDecoderTablesPeer;
}  // namespace test

struct QUICHE_EXPORT HpackStringPair {
  HpackStringPair(std::string name, std::string value);
  ~HpackStringPair();

  // Returns the size of a header entry with this name and value, per the RFC:
  // http://httpwg.org/specs/rfc7541.html#calculating.table.size
  size_t size() const { return 32 + name.size() + value.size(); }

  std::string DebugString() const;

  const std::string name;
  const std::string value;
};

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const HpackStringPair& p);

// See http://httpwg.org/specs/rfc7541.html#static.table.definition for the
// contents, and http://httpwg.org/specs/rfc7541.html#index.address.space for
// info about accessing the static table.
class QUICHE_EXPORT HpackDecoderStaticTable {
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
class QUICHE_EXPORT HpackDecoderDynamicTable {
 public:
  HpackDecoderDynamicTable();
  ~HpackDecoderDynamicTable();

  HpackDecoderDynamicTable(const HpackDecoderDynamicTable&) = delete;
  HpackDecoderDynamicTable& operator=(const HpackDecoderDynamicTable&) = delete;

  // Sets a new size limit, received from the peer; performs evictions if
  // necessary to ensure that the current size does not exceed the new limit.
  // The caller needs to have validated that size_limit does not
  // exceed the acknowledged value of SETTINGS_HEADER_TABLE_SIZE.
  void DynamicTableSizeUpdate(size_t size_limit);

  // Insert entry if possible.
  // If entry is too large to insert, then dynamic table will be empty.
  void Insert(std::string name, std::string value);

  // If index is valid, returns a pointer to the entry, otherwise returns
  // nullptr.
  const HpackStringPair* Lookup(size_t index) const;

  size_t size_limit() const { return size_limit_; }
  size_t current_size() const { return current_size_; }

 private:
  friend class test::HpackDecoderTablesPeer;

  // Drop older entries to ensure the size is not greater than limit.
  void EnsureSizeNoMoreThan(size_t limit);

  // Removes the oldest dynamic table entry.
  void RemoveLastEntry();

  quiche::QuicheCircularDeque<HpackStringPair> table_;

  // The last received DynamicTableSizeUpdate value, initialized to
  // SETTINGS_HEADER_TABLE_SIZE.
  size_t size_limit_ = Http2SettingsInfo::DefaultHeaderTableSize();

  size_t current_size_ = 0;

  // insert_count_ and debug_listener_ are used by a QUIC experiment; remove
  // when the experiment is done.
  size_t insert_count_;
};

class QUICHE_EXPORT HpackDecoderTables {
 public:
  HpackDecoderTables();
  ~HpackDecoderTables();

  HpackDecoderTables(const HpackDecoderTables&) = delete;
  HpackDecoderTables& operator=(const HpackDecoderTables&) = delete;

  // Sets a new size limit, received from the peer; performs evictions if
  // necessary to ensure that the current size does not exceed the new limit.
  // The caller needs to have validated that size_limit does not
  // exceed the acknowledged value of SETTINGS_HEADER_TABLE_SIZE.
  void DynamicTableSizeUpdate(size_t size_limit) {
    dynamic_table_.DynamicTableSizeUpdate(size_limit);
  }

  // Insert entry if possible.
  // If entry is too large to insert, then dynamic table will be empty.
  void Insert(std::string name, std::string value) {
    dynamic_table_.Insert(std::move(name), std::move(value));
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
