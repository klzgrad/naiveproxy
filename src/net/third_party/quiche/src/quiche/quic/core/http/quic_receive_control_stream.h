// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_

#include "quiche/quic/core/http/http_decoder.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QuicSpdySession;

// 3.2.1 Control Stream.
// The receive control stream is peer initiated and is read only.
class QUICHE_EXPORT QuicReceiveControlStream : public QuicStream,
                                               public HttpDecoder::Visitor {
 public:
  explicit QuicReceiveControlStream(PendingStream* pending,
                                    QuicSpdySession* spdy_session);
  QuicReceiveControlStream(const QuicReceiveControlStream&) = delete;
  QuicReceiveControlStream& operator=(const QuicReceiveControlStream&) = delete;
  ~QuicReceiveControlStream() override;

  // Overriding QuicStream::OnStreamReset to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Implementation of QuicStream.
  void OnDataAvailable() override;

  // HttpDecoder::Visitor implementation.
  void OnError(HttpDecoder* decoder) override;
  bool OnMaxPushIdFrame() override;
  bool OnGoAwayFrame(const GoAwayFrame& frame) override;
  bool OnSettingsFrameStart(QuicByteCount header_length) override;
  bool OnSettingsFrame(const SettingsFrame& frame) override;
  bool OnDataFrameStart(QuicByteCount header_length,
                        QuicByteCount payload_length) override;
  bool OnDataFramePayload(absl::string_view payload) override;
  bool OnDataFrameEnd() override;
  bool OnHeadersFrameStart(QuicByteCount header_length,
                           QuicByteCount payload_length) override;
  bool OnHeadersFramePayload(absl::string_view payload) override;
  bool OnHeadersFrameEnd() override;
  bool OnPriorityUpdateFrameStart(QuicByteCount header_length) override;
  bool OnPriorityUpdateFrame(const PriorityUpdateFrame& frame) override;
  bool OnAcceptChFrameStart(QuicByteCount header_length) override;
  bool OnAcceptChFrame(const AcceptChFrame& frame) override;
  void OnWebTransportStreamFrameType(QuicByteCount header_length,
                                     WebTransportSessionId session_id) override;
  bool OnUnknownFrameStart(uint64_t frame_type, QuicByteCount header_length,
                           QuicByteCount payload_length) override;
  bool OnUnknownFramePayload(absl::string_view payload) override;
  bool OnUnknownFrameEnd() override;

  QuicSpdySession* spdy_session() { return spdy_session_; }

 private:
  // Called when a frame of allowed type is received.  Returns true if the frame
  // is allowed in this position.  Returns false and resets the stream
  // otherwise.
  bool ValidateFrameType(HttpFrameType frame_type);

  // False until a SETTINGS frame is received.
  bool settings_frame_received_;

  HttpDecoder decoder_;
  QuicSpdySession* const spdy_session_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
