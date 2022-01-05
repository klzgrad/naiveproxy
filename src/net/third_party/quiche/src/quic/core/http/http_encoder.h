// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_ENCODER_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_ENCODER_H_

#include <memory>
#include "quic/core/http/http_frames.h"
#include "quic/core/quic_buffer_allocator.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

class QuicDataWriter;

// A class for encoding the HTTP frames that are exchanged in an HTTP over QUIC
// session.
class QUIC_EXPORT_PRIVATE HttpEncoder {
 public:
  HttpEncoder() = delete;

  // Returns the length of the header for a DATA frame.
  static QuicByteCount GetDataFrameHeaderLength(QuicByteCount payload_length);

  // Serializes a DATA frame header into a QuicBuffer; returns said QuicBuffer
  // on success, empty buffer otherwise.
  static QuicBuffer SerializeDataFrameHeader(QuicByteCount payload_length,
                                             QuicBufferAllocator* allocator);

  // Serializes a HEADERS frame header into a new buffer stored in |output|.
  // Returns the length of the buffer on success, or 0 otherwise.
  static QuicByteCount SerializeHeadersFrameHeader(
      QuicByteCount payload_length,
      std::unique_ptr<char[]>* output);

  // Serializes a SETTINGS frame into a new buffer stored in |output|.
  // Returns the length of the buffer on success, or 0 otherwise.
  static QuicByteCount SerializeSettingsFrame(const SettingsFrame& settings,
                                              std::unique_ptr<char[]>* output);

  // Serializes a GOAWAY frame into a new buffer stored in |output|.
  // Returns the length of the buffer on success, or 0 otherwise.
  static QuicByteCount SerializeGoAwayFrame(const GoAwayFrame& goaway,
                                            std::unique_ptr<char[]>* output);

  // Serializes a PRIORITY_UPDATE frame into a new buffer stored in |output|.
  // Returns the length of the buffer on success, or 0 otherwise.
  static QuicByteCount SerializePriorityUpdateFrame(
      const PriorityUpdateFrame& priority_update,
      std::unique_ptr<char[]>* output);

  // Serializes an ACCEPT_CH frame into a new buffer stored in |output|.
  // Returns the length of the buffer on success, or 0 otherwise.
  static QuicByteCount SerializeAcceptChFrame(const AcceptChFrame& accept_ch,
                                              std::unique_ptr<char[]>* output);

  // Serializes a frame with reserved frame type specified in
  // https://tools.ietf.org/html/draft-ietf-quic-http-25#section-7.2.9.
  static QuicByteCount SerializeGreasingFrame(std::unique_ptr<char[]>* output);

  // Serializes a WEBTRANSPORT_STREAM frame header as specified in
  // https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-00.html#name-client-initiated-bidirectio
  static QuicByteCount SerializeWebTransportStreamFrameHeader(
      WebTransportSessionId session_id,
      std::unique_ptr<char[]>* output);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_ENCODER_H_
