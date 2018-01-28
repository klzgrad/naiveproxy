// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// HpackDecoderState maintains the HPACK decompressor state; i.e. updates the
// HPACK dynamic table according to RFC 7541 as the entries in an HPACK block
// are decoded, and reads from the static and dynamic tables in order to build
// complete header entries. Calls an HpackDecoderListener with the completely
// decoded headers (i.e. after resolving table indices into names or values),
// thus translating the decoded HPACK entries into HTTP/2 headers.

#ifndef NET_HTTP2_HPACK_DECODER_HPACK_DECODER_STATE_H_
#define NET_HTTP2_HPACK_DECODER_HPACK_DECODER_STATE_H_

#include <stddef.h>
#include <stdint.h>

#include "base/macros.h"
#include "net/http2/hpack/decoder/hpack_decoder_listener.h"
#include "net/http2/hpack/decoder/hpack_decoder_string_buffer.h"
#include "net/http2/hpack/decoder/hpack_decoder_tables.h"
#include "net/http2/hpack/decoder/hpack_whole_entry_listener.h"
#include "net/http2/hpack/http2_hpack_constants.h"
#include "net/http2/platform/api/http2_export.h"
#include "net/http2/platform/api/http2_string_piece.h"

namespace net {
namespace test {
class HpackDecoderStatePeer;
}  // namespace test

class HTTP2_EXPORT_PRIVATE HpackDecoderState : public HpackWholeEntryListener {
 public:
  explicit HpackDecoderState(HpackDecoderListener* listener);
  ~HpackDecoderState() override;

  // Set the listener to be notified when a whole entry has been decoded,
  // including resolving name or name and value references.
  // The listener may be changed at any time.
  HpackDecoderListener* listener() const { return listener_; }

  // Set listener to be notified of insertions into the HPACK dynamic table,
  // and uses of those entries.
  void set_tables_debug_listener(
      HpackDecoderTablesDebugListener* debug_listener);

  // ApplyHeaderTableSizeSetting notifies this object that this endpoint has
  // received a SETTINGS ACK frame acknowledging an earlier SETTINGS frame from
  // this endpoint specifying a new value for SETTINGS_HEADER_TABLE_SIZE (the
  // maximum size of the dynamic table that this endpoint will use to decode
  // HPACK blocks).
  // Because a SETTINGS frame can contain SETTINGS_HEADER_TABLE_SIZE values,
  // the caller must keep track of those multiple changes, and make
  // corresponding calls to this method. In particular, a call must be made
  // with the lowest value acknowledged by the peer, and a call must be made
  // with the final value acknowledged, in that order; additional calls may
  // be made if additional values were sent. These calls must be made between
  // decoding the SETTINGS ACK, and before the next HPACK block is decoded.
  void ApplyHeaderTableSizeSetting(uint32_t max_header_table_size);

  // OnHeaderBlockStart notifies this object that we're starting to decode the
  // HPACK payload of a HEADERS or PUSH_PROMISE frame.
  void OnHeaderBlockStart();

  // Implement the HpackWholeEntryListener methods, each of which notifies this
  // object when an entire entry has been decoded.
  void OnIndexedHeader(size_t index) override;
  void OnNameIndexAndLiteralValue(
      HpackEntryType entry_type,
      size_t name_index,
      HpackDecoderStringBuffer* value_buffer) override;
  void OnLiteralNameAndValue(HpackEntryType entry_type,
                             HpackDecoderStringBuffer* name_buffer,
                             HpackDecoderStringBuffer* value_buffer) override;
  void OnDynamicTableSizeUpdate(size_t size) override;
  void OnHpackDecodeError(Http2StringPiece error_message) override;

  // OnHeaderBlockEnd notifies this object that an entire HPACK block has been
  // decoded, which might have extended into CONTINUATION blocks.
  void OnHeaderBlockEnd();

  // Was an error detected? After an error has been detected and reported,
  // no further callbacks will be made to the listener.
  bool error_detected() const { return error_detected_; }

  const HpackDecoderTables& decoder_tables_for_test() const {
    return decoder_tables_;
  }

 private:
  friend class test::HpackDecoderStatePeer;

  // Reports an error to the listener IF this is the first error detected.
  void ReportError(Http2StringPiece error_message);

  // The static and dynamic HPACK tables.
  HpackDecoderTables decoder_tables_;

  // The listener to be notified of headers, the start and end of header
  // lists, and of errors.
  HpackDecoderListener* listener_;

  // The most recent HEADER_TABLE_SIZE setting acknowledged by the peer.
  uint32_t final_header_table_size_;

  // The lowest HEADER_TABLE_SIZE setting acknowledged by the peer; valid until
  // the next HPACK block is decoded.
  // TODO(jamessynge): Test raising the HEADER_TABLE_SIZE.
  uint32_t lowest_header_table_size_;

  // Must the next (first) HPACK entry be a dynamic table size update?
  bool require_dynamic_table_size_update_;

  // May the next (first or second) HPACK entry be a dynamic table size update?
  bool allow_dynamic_table_size_update_;

  // Have we already seen a dynamic table size update in this HPACK block?
  bool saw_dynamic_table_size_update_;

  // Has an error already been detected and reported to the listener?
  bool error_detected_;

  DISALLOW_COPY_AND_ASSIGN(HpackDecoderState);
};

}  // namespace net

#endif  // NET_HTTP2_HPACK_DECODER_HPACK_DECODER_STATE_H_
