// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HPACK_DECODER_ADAPTER_H_
#define QUICHE_HTTP2_HPACK_HPACK_DECODER_ADAPTER_H_

// HpackDecoderAdapter uses http2::HpackDecoder to decode HPACK blocks into
// HTTP/2 header lists as outlined in http://tools.ietf.org/html/rfc7541.

#include <stddef.h>

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/no_op_headers_handler.h"
#include "quiche/http2/core/spdy_headers_handler_interface.h"
#include "quiche/http2/hpack/decoder/hpack_decoder.h"
#include "quiche/http2/hpack/decoder/hpack_decoder_listener.h"
#include "quiche/http2/hpack/decoder/hpack_decoding_error.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace spdy {
namespace test {
class HpackDecoderAdapterPeer;
}  // namespace test

class QUICHE_EXPORT HpackDecoderAdapter {
 public:
  friend test::HpackDecoderAdapterPeer;
  HpackDecoderAdapter();
  HpackDecoderAdapter(const HpackDecoderAdapter&) = delete;
  HpackDecoderAdapter& operator=(const HpackDecoderAdapter&) = delete;
  ~HpackDecoderAdapter();

  // Called upon acknowledgement of SETTINGS_HEADER_TABLE_SIZE.
  void ApplyHeaderTableSizeSetting(size_t size_setting);

  // Returns the most recently applied value of SETTINGS_HEADER_TABLE_SIZE.
  size_t GetCurrentHeaderTableSizeSetting() const;

  // The decoder will emit headers to the provided SpdyHeadersHandlerInterface.
  // Does not take ownership of the handler, but does use the pointer until
  // the current HPACK block is completely decoded.
  // `handler` must not be nullptr.
  void HandleControlFrameHeadersStart(SpdyHeadersHandlerInterface* handler);

  // Called as HPACK block fragments arrive. Returns false if an error occurred
  // while decoding the block. Does not take ownership of headers_data.
  bool HandleControlFrameHeadersData(const char* headers_data,
                                     size_t headers_data_length);

  // Called after a HPACK block has been completely delivered via
  // HandleControlFrameHeadersData(). Returns false if an error occurred.
  // |compressed_len| if non-null will be set to the size of the encoded
  // buffered block that was accumulated in HandleControlFrameHeadersData(),
  // to support subsequent calculation of compression percentage.
  // Discards the handler supplied at the start of decoding the block.
  bool HandleControlFrameHeadersComplete();

  // Returns the current dynamic table size, including the 32 bytes per entry
  // overhead mentioned in RFC 7541 section 4.1.
  size_t GetDynamicTableSize() const {
    return hpack_decoder_.GetDynamicTableSize();
  }

  // Set how much encoded data this decoder is willing to buffer.
  // TODO(jamessynge): Resolve definition of this value, as it is currently
  // too tied to a single implementation. We probably want to limit one or more
  // of these: individual name or value strings, header entries, the entire
  // header list, or the HPACK block; we probably shouldn't care about the size
  // of individual transport buffers.
  void set_max_decode_buffer_size_bytes(size_t max_decode_buffer_size_bytes);

  // Specifies the maximum size of an on-the-wire header block that will be
  // accepted.
  void set_max_header_block_bytes(size_t max_header_block_bytes);

  // Error code if an error has occurred, Error::kOk otherwise.
  http2::HpackDecodingError error() const { return error_; }

 private:
  class QUICHE_EXPORT ListenerAdapter : public http2::HpackDecoderListener {
   public:
    ListenerAdapter();
    ~ListenerAdapter() override;

    // Sets the SpdyHeadersHandlerInterface to which headers are emitted.
    // Does not take ownership of the handler, but does use the pointer until
    // the current HPACK block is completely decoded.
    // `handler` must not be nullptr.
    void set_handler(SpdyHeadersHandlerInterface* handler);

    // From HpackDecoderListener
    void OnHeaderListStart() override;
    void OnHeader(absl::string_view name, absl::string_view value) override;
    void OnHeaderListEnd() override;
    void OnHeaderErrorDetected(absl::string_view error_message) override;

    void AddToTotalHpackBytes(size_t delta) { total_hpack_bytes_ += delta; }
    size_t total_hpack_bytes() const { return total_hpack_bytes_; }

   private:
    NoOpHeadersHandler no_op_handler_;

    // Handles decoded headers.
    SpdyHeadersHandlerInterface* handler_;

    // Total bytes that have been received as input (i.e. HPACK encoded)
    // in the current HPACK block.
    size_t total_hpack_bytes_;

    // Total bytes of the name and value strings in the current HPACK block.
    size_t total_uncompressed_bytes_;
  };

  // Converts calls to HpackDecoderListener into calls to
  // SpdyHeadersHandlerInterface.
  ListenerAdapter listener_adapter_;

  // The actual decoder.
  http2::HpackDecoder hpack_decoder_;

  // How much encoded data this decoder is willing to buffer.
  size_t max_decode_buffer_size_bytes_;

  // How much encoded data this decoder is willing to process.
  size_t max_header_block_bytes_;

  // Flag to keep track of having seen the header block start. Needed at the
  // moment because HandleControlFrameHeadersStart won't be called if a handler
  // is not being provided by the caller.
  bool header_block_started_;

  // Error code if an error has occurred, Error::kOk otherwise.
  http2::HpackDecodingError error_;
};

}  // namespace spdy

#endif  // QUICHE_HTTP2_HPACK_HPACK_DECODER_ADAPTER_H_
