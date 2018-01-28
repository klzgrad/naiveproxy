// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines HpackWholeEntryListener, the base class of listeners for decoded
// complete HPACK entries, as opposed to HpackEntryDecoderListener which
// receives multiple callbacks for some single entries.

#ifndef NET_HTTP2_HPACK_DECODER_HPACK_WHOLE_ENTRY_LISTENER_H_
#define NET_HTTP2_HPACK_DECODER_HPACK_WHOLE_ENTRY_LISTENER_H_

#include <stddef.h>

#include "net/http2/hpack/decoder/hpack_decoder_string_buffer.h"
#include "net/http2/hpack/http2_hpack_constants.h"
#include "net/http2/platform/api/http2_export.h"
#include "net/http2/platform/api/http2_string_piece.h"

namespace net {

class HTTP2_EXPORT_PRIVATE HpackWholeEntryListener {
 public:
  virtual ~HpackWholeEntryListener();

  // Called when an indexed header (i.e. one in the static or dynamic table) has
  // been decoded from an HPACK block. index is supposed to be non-zero, but
  // that has not been checked by the caller.
  virtual void OnIndexedHeader(size_t index) = 0;

  // Called when a header entry with a name index and literal value has
  // been fully decoded from an HPACK block. name_index is NOT zero.
  // entry_type will be kIndexedLiteralHeader, kUnindexedLiteralHeader, or
  // kNeverIndexedLiteralHeader.
  virtual void OnNameIndexAndLiteralValue(
      HpackEntryType entry_type,
      size_t name_index,
      HpackDecoderStringBuffer* value_buffer) = 0;

  // Called when a header entry with a literal name and literal value
  // has been fully decoded from an HPACK block. entry_type will be
  // kIndexedLiteralHeader, kUnindexedLiteralHeader, or
  // kNeverIndexedLiteralHeader.
  virtual void OnLiteralNameAndValue(
      HpackEntryType entry_type,
      HpackDecoderStringBuffer* name_buffer,
      HpackDecoderStringBuffer* value_buffer) = 0;

  // Called when an update to the size of the peer's dynamic table has been
  // decoded.
  virtual void OnDynamicTableSizeUpdate(size_t size) = 0;

  // OnHpackDecodeError is called if an error is detected while decoding.
  // error_message may be used in a GOAWAY frame as the Opaque Data.
  virtual void OnHpackDecodeError(Http2StringPiece error_message) = 0;
};

// A no-op implementation of HpackWholeEntryDecoderListener, useful for ignoring
// callbacks once an error is detected.
class HpackWholeEntryNoOpListener : public HpackWholeEntryListener {
 public:
  ~HpackWholeEntryNoOpListener() override;

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

  // Returns a listener that ignores all the calls.
  static HpackWholeEntryNoOpListener* NoOpListener();
};

}  // namespace net

#endif  // NET_HTTP2_HPACK_DECODER_HPACK_WHOLE_ENTRY_LISTENER_H_
