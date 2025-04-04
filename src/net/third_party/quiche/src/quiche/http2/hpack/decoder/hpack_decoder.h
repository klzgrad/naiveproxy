// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_H_

// Decodes HPACK blocks, calls an HpackDecoderListener with the decoded header
// entries. Also notifies the listener of errors and of the boundaries of the
// HPACK blocks.

// TODO(jamessynge): Add feature allowing an HpackEntryDecoderListener
// sub-class (and possibly others) to be passed in for counting events,
// so that deciding whether to count is not done by having lots of if
// statements, but instead by inserting an indirection only when needed.

// TODO(jamessynge): Consider whether to return false from methods below
// when an error has been previously detected. It protects calling code
// from its failure to pay attention to previous errors, but should we
// spend time to do that?

#include <stddef.h>

#include <cstdint>

#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/hpack/decoder/hpack_block_decoder.h"
#include "quiche/http2/hpack/decoder/hpack_decoder_listener.h"
#include "quiche/http2/hpack/decoder/hpack_decoder_state.h"
#include "quiche/http2/hpack/decoder/hpack_decoder_tables.h"
#include "quiche/http2/hpack/decoder/hpack_decoding_error.h"
#include "quiche/http2/hpack/decoder/hpack_whole_entry_buffer.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class HpackDecoderPeer;
}  // namespace test

class QUICHE_EXPORT HpackDecoder {
 public:
  HpackDecoder(HpackDecoderListener* listener, size_t max_string_size);
  virtual ~HpackDecoder();

  HpackDecoder(const HpackDecoder&) = delete;
  HpackDecoder& operator=(const HpackDecoder&) = delete;

  // max_string_size specifies the maximum size of an on-the-wire string (name
  // or value, plain or Huffman encoded) that will be accepted. See sections
  // 5.1 and 5.2 of RFC 7541. This is a defense against OOM attacks; HTTP/2
  // allows a decoder to enforce any limit of the size of the header lists
  // that it is willing to decode, including less than the MAX_HEADER_LIST_SIZE
  // setting, a setting that is initially unlimited. For example, we might
  // choose to send a MAX_HEADER_LIST_SIZE of 64KB, and to use that same value
  // as the upper bound for individual strings.
  void set_max_string_size_bytes(size_t max_string_size_bytes);

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

  // Returns the most recently applied value of SETTINGS_HEADER_TABLE_SIZE.
  size_t GetCurrentHeaderTableSizeSetting() const {
    return decoder_state_.GetCurrentHeaderTableSizeSetting();
  }

  // Prepares the decoder for decoding a new HPACK block, and announces this to
  // its listener. Returns true if OK to continue with decoding, false if an
  // error has been detected, which for StartDecodingBlock means the error was
  // detected while decoding a previous HPACK block.
  bool StartDecodingBlock();

  // Decodes a fragment (some or all of the remainder) of an HPACK block,
  // reporting header entries (name & value pairs) that it completely decodes
  // in the process to the listener. Returns true successfully decoded, false if
  // an error has been detected, either during decoding of the fragment, or
  // prior to this call.
  bool DecodeFragment(DecodeBuffer* db);

  // Completes the process of decoding an HPACK block: if the HPACK block was
  // properly terminated, announces the end of the header list to the listener
  // and returns true; else returns false.
  bool EndDecodingBlock();

  // If no error has been detected so far, query |decoder_state_| for errors and
  // set |error_| if necessary.  Returns true if an error has ever been
  // detected.
  bool DetectError();

  size_t GetDynamicTableSize() const {
    return decoder_state_.GetDynamicTableSize();
  }

  // Error code if an error has occurred, HpackDecodingError::kOk otherwise.
  HpackDecodingError error() const { return error_; }

 private:
  friend class test::HpackDecoderPeer;

  // Reports an error to the listener IF this is the first error detected.
  void ReportError(HpackDecodingError error);

  // The decompressor state, as defined by HPACK (i.e. the static and dynamic
  // tables).
  HpackDecoderState decoder_state_;

  // Assembles the various parts of a header entry into whole entries.
  HpackWholeEntryBuffer entry_buffer_;

  // The decoder of HPACK blocks into entry parts, passed to entry_buffer_.
  HpackBlockDecoder block_decoder_;

  // Error code if an error has occurred, HpackDecodingError::kOk otherwise.
  HpackDecodingError error_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_H_
