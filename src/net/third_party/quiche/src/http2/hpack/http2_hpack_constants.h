// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HTTP2_HPACK_CONSTANTS_H_
#define QUICHE_HTTP2_HPACK_HTTP2_HPACK_CONSTANTS_H_

// Enum HpackEntryType identifies the 5 basic types of HPACK Block Entries.
//
// See the spec for details:
// https://http2.github.io/http2-spec/compression.html#rfc.section.6

#include <ostream>
#include <string>

#include "net/third_party/quiche/src/http2/platform/api/http2_export.h"

namespace http2 {

const size_t kFirstDynamicTableIndex = 62;

enum class HpackEntryType {
  // Entry is an index into the static or dynamic table. Decoding it has no
  // effect on the dynamic table.
  kIndexedHeader,

  // The entry contains a literal value. The name may be either a literal or a
  // reference to an entry in the static or dynamic table.
  // The entry is added to the dynamic table after decoding.
  kIndexedLiteralHeader,

  // The entry contains a literal value. The name may be either a literal or a
  // reference to an entry in the static or dynamic table.
  // The entry is not added to the dynamic table after decoding, but a proxy
  // may choose to insert the entry into its dynamic table when forwarding
  // to another endpoint.
  kUnindexedLiteralHeader,

  // The entry contains a literal value. The name may be either a literal or a
  // reference to an entry in the static or dynamic table.
  // The entry is not added to the dynamic table after decoding, and a proxy
  // must NOT insert the entry into its dynamic table when forwarding to another
  // endpoint.
  kNeverIndexedLiteralHeader,

  // Entry conveys the size limit of the dynamic table of the encoder to
  // the decoder. May be used to flush the table by sending a zero and then
  // resetting the size back up to the maximum that the encoder will use
  // (within the limits of SETTINGS_HEADER_TABLE_SIZE sent by the
  // decoder to the encoder, with the default of 4096 assumed).
  kDynamicTableSizeUpdate,
};

// Returns the name of the enum member.
HTTP2_EXPORT_PRIVATE std::string HpackEntryTypeToString(HpackEntryType v);

// Inserts the name of the enum member into |out|.
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              HpackEntryType v);

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_HTTP2_HPACK_CONSTANTS_H_
