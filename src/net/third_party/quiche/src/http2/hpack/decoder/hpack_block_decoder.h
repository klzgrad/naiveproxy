// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_BLOCK_DECODER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_BLOCK_DECODER_H_

// HpackBlockDecoder decodes an entire HPACK block (or the available portion
// thereof in the DecodeBuffer) into entries, but doesn't include HPACK static
// or dynamic table support, so table indices remain indices at this level.
// Reports the entries to an HpackEntryDecoderListener.

#include <string>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoding_error.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder_listener.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {

class QUICHE_EXPORT_PRIVATE HpackBlockDecoder {
 public:
  explicit HpackBlockDecoder(HpackEntryDecoderListener* listener)
      : listener_(listener) {
    DCHECK_NE(listener_, nullptr);
  }
  ~HpackBlockDecoder() {}

  HpackBlockDecoder(const HpackBlockDecoder&) = delete;
  HpackBlockDecoder& operator=(const HpackBlockDecoder&) = delete;

  // Prepares the decoder to start decoding a new HPACK block. Expected
  // to be called from an implementation of Http2FrameDecoderListener's
  // OnHeadersStart or OnPushPromiseStart methods.
  void Reset() {
    HTTP2_DVLOG(2) << "HpackBlockDecoder::Reset";
    before_entry_ = true;
  }

  // Decode the fragment of the HPACK block contained in the decode buffer.
  // Expected to be called from an implementation of Http2FrameDecoderListener's
  // OnHpackFragment method.
  DecodeStatus Decode(DecodeBuffer* db);

  // Is the decoding process between entries (i.e. would the next byte be the
  // first byte of a new HPACK entry)?
  bool before_entry() const { return before_entry_; }

  // Return error code after decoding error occurred in HpackEntryDecoder.
  HpackDecodingError error() const { return entry_decoder_.error(); }

  std::string DebugString() const;

 private:
  HpackEntryDecoder entry_decoder_;
  HpackEntryDecoderListener* const listener_;
  bool before_entry_ = true;
};

QUICHE_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                               const HpackBlockDecoder& v);

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_BLOCK_DECODER_H_
