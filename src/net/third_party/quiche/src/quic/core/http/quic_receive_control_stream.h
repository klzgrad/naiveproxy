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
class QUIC_EXPORT_PRIVATE QuicReceiveControlStream : public QuicStream {
 public:
  explicit QuicReceiveControlStream(PendingStream* pending);
  QuicReceiveControlStream(const QuicReceiveControlStream&) = delete;
  QuicReceiveControlStream& operator=(const QuicReceiveControlStream&) = delete;
  ~QuicReceiveControlStream() override;

  // Overriding QuicStream::OnStreamReset to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Implementation of QuicStream.
  void OnDataAvailable() override;

  void SetUnblocked() { sequencer()->SetUnblocked(); }

 private:
  class HttpDecoderVisitor;

  // Called from HttpDecoderVisitor.
  bool OnSettingsFrameStart(QuicByteCount header_length);
  bool OnSettingsFrame(const SettingsFrame& settings);
  bool OnPriorityFrameStart(QuicByteCount header_length);
  // TODO(renjietang): Decode Priority in HTTP/3 style.
  bool OnPriorityFrame(const PriorityFrame& priority);

  // False until a SETTINGS frame is received.
  bool settings_frame_received_;

  // HttpDecoder and its visitor.
  std::unique_ptr<HttpDecoderVisitor> http_decoder_visitor_;
  HttpDecoder decoder_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
