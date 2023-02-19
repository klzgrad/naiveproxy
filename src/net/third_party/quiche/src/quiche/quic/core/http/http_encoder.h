// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_ENCODER_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_ENCODER_H_

#include <memory>

#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quic {

class QuicDataWriter;

// A class for encoding the HTTP frames that are exchanged in an HTTP over QUIC
// session.
class QUIC_EXPORT_PRIVATE HttpEncoder {
 public:
  HttpEncoder() = delete;

  // Returns the length of the header for a DATA frame.
  static QuicByteCount GetDataFrameHeaderLength(QuicByteCount payload_length);

  // Serializes a DATA frame header into a QuicheBuffer; returns said
  // QuicheBuffer on success, empty buffer otherwise.
  static quiche::QuicheBuffer SerializeDataFrameHeader(
      QuicByteCount payload_length, quiche::QuicheBufferAllocator* allocator);

  // Serializes a HEADERS frame header.
  static std::string SerializeHeadersFrameHeader(QuicByteCount payload_length);

  // Serializes a SETTINGS frame.
  static std::string SerializeSettingsFrame(const SettingsFrame& settings);

  // Serializes a GOAWAY frame.
  static std::string SerializeGoAwayFrame(const GoAwayFrame& goaway);

  // Serializes a PRIORITY_UPDATE frame.
  static std::string SerializePriorityUpdateFrame(
      const PriorityUpdateFrame& priority_update);

  // Serializes an ACCEPT_CH frame.
  static std::string SerializeAcceptChFrame(const AcceptChFrame& accept_ch);

  // Serializes a frame with reserved frame type specified in
  // https://tools.ietf.org/html/draft-ietf-quic-http-25#section-7.2.9.
  static std::string SerializeGreasingFrame();

  // Serializes a WEBTRANSPORT_STREAM frame header as specified in
  // https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-00.html#name-client-initiated-bidirectio
  static std::string SerializeWebTransportStreamFrameHeader(
      WebTransportSessionId session_id);

  // Serializes a METADATA frame header.
  static std::string SerializeMetadataFrameHeader(QuicByteCount payload_length);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_ENCODER_H_
