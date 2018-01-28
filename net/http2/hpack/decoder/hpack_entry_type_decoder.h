// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_HPACK_DECODER_HPACK_ENTRY_TYPE_DECODER_H_
#define NET_HTTP2_HPACK_DECODER_HPACK_ENTRY_TYPE_DECODER_H_

// Decodes the type of an HPACK entry, and the variable length integer whose
// prefix is in the low-order bits of the same byte, "below" the type bits.
// The integer represents an index into static or dynamic table, which may be
// zero, or is the new size limit of the dynamic table.

#include "base/logging.h"
#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/hpack/decoder/hpack_varint_decoder.h"
#include "net/http2/hpack/http2_hpack_constants.h"
#include "net/http2/platform/api/http2_export.h"
#include "net/http2/platform/api/http2_string.h"

namespace net {

class HTTP2_EXPORT_PRIVATE HpackEntryTypeDecoder {
 public:
  // Only call when the decode buffer has data (i.e. HpackEntryDecoder must
  // not call until there is data).
  DecodeStatus Start(DecodeBuffer* db);

  // Only call Resume if the previous call (Start or Resume) returned
  // DecodeStatus::kDecodeInProgress.
  DecodeStatus Resume(DecodeBuffer* db) { return varint_decoder_.Resume(db); }

  // Returns the decoded entry type. Only call if the preceding call to Start
  // or Resume returned kDecodeDone.
  HpackEntryType entry_type() const { return entry_type_; }

  // Returns the decoded variable length integer. Only call if the
  // preceding call to Start or Resume returned kDecodeDone.
  uint32_t varint() const { return varint_decoder_.value(); }

  Http2String DebugString() const;

 private:
  HpackVarintDecoder varint_decoder_;

  // This field is initialized just to keep ASAN happy about reading it
  // from DebugString().
  HpackEntryType entry_type_ = HpackEntryType::kIndexedHeader;
};

HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const HpackEntryTypeDecoder& v);

}  // namespace net
#endif  // NET_HTTP2_HPACK_DECODER_HPACK_ENTRY_TYPE_DECODER_H_
