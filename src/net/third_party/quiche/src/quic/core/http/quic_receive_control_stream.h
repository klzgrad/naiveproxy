// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QuicSpdySession;

// 3.2.1 Control Stream.
// The receive control stream is peer initiated and is read only.
class QUIC_EXPORT_PRIVATE QuicReceiveControlStream : public QuicStream {
 public:
  // |session| can't be nullptr, and the ownership is not passed. The stream can
  // only be accessed through the session.
  explicit QuicReceiveControlStream(QuicStreamId id, QuicSpdySession* session);
  // Construct control stream from pending stream, the |pending| object will no
  // longer exist after the construction.
  explicit QuicReceiveControlStream(PendingStream pending);
  QuicReceiveControlStream(const QuicReceiveControlStream&) = delete;
  QuicReceiveControlStream& operator=(const QuicReceiveControlStream&) = delete;
  ~QuicReceiveControlStream() override;

  // Overriding QuicStream::OnStreamReset to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Implementation of QuicStream.
  void OnDataAvailable() override;

 protected:
  // Called from HttpDecoderVisitor.
  void OnSettingsFrameStart(Http3FrameLengths frame_lengths);
  void OnSettingsFrame(const SettingsFrame& settings);

 private:
  class HttpDecoderVisitor;

  HttpDecoder decoder_;

  // Track the number of settings bytes received.
  size_t received_settings_length_;

  // HttpDecoder's visitor.
  std::unique_ptr<HttpDecoderVisitor> http_decoder_visitor_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_RECEIVE_CONTROL_STREAM_H_
