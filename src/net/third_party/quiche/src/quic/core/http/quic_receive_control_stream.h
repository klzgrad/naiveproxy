// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QuicSpdySession;

// 3.2.1 Control Stream.
// The receive control stream is peer initiated and is read only.
class QUIC_EXPORT_PRIVATE QuicReceiveControlStream
    : public QuicStream,
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

  // HttpDecoderVisitor implementation.
  void OnError(HttpDecoder* decoder) override;
  bool OnCancelPushFrame(const CancelPushFrame& frame) override;
  bool OnMaxPushIdFrame(const MaxPushIdFrame& frame) override;
  bool OnGoAwayFrame(const GoAwayFrame& frame) override;
  bool OnSettingsFrameStart(QuicByteCount header_length) override;
  bool OnSettingsFrame(const SettingsFrame& frame) override;
  bool OnDataFrameStart(QuicByteCount header_length,
                        QuicByteCount payload_length) override;
  bool OnDataFramePayload(quiche::QuicheStringPiece payload) override;
  bool OnDataFrameEnd() override;
  bool OnHeadersFrameStart(QuicByteCount header_length,
                           QuicByteCount payload_length) override;
  bool OnHeadersFramePayload(quiche::QuicheStringPiece payload) override;
  bool OnHeadersFrameEnd() override;
  bool OnPushPromiseFrameStart(QuicByteCount header_length) override;
  bool OnPushPromiseFramePushId(PushId push_id,
                                QuicByteCount push_id_length,
                                QuicByteCount header_block_length) override;
  bool OnPushPromiseFramePayload(quiche::QuicheStringPiece payload) override;
  bool OnPushPromiseFrameEnd() override;
  bool OnPriorityUpdateFrameStart(QuicByteCount header_length) override;
  bool OnPriorityUpdateFrame(const PriorityUpdateFrame& frame) override;
  bool OnUnknownFrameStart(uint64_t frame_type,
                           QuicByteCount header_length,
                           QuicByteCount payload_length) override;
  bool OnUnknownFramePayload(quiche::QuicheStringPiece payload) override;
  bool OnUnknownFrameEnd() override;

  void SetUnblocked() { sequencer()->SetUnblocked(); }

  QuicSpdySession* spdy_session() { return spdy_session_; }

 private:
  void OnWrongFrame(quiche::QuicheStringPiece frame_type);

  // False until a SETTINGS frame is received.
  bool settings_frame_received_;

  HttpDecoder decoder_;
  QuicSpdySession* const spdy_session_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
