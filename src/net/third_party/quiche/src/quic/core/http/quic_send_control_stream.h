// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QuicSpdySession;

// 3.2.1 Control Stream.
// The send control stream is self initiated and is write only.
class QUIC_EXPORT_PRIVATE QuicSendControlStream : public QuicStream {
 public:
  // |session| can't be nullptr, and the ownership is not passed. The stream can
  // only be accessed through the session.
  explicit QuicSendControlStream(QuicStreamId id, QuicSpdySession* session);
  QuicSendControlStream(const QuicSendControlStream&) = delete;
  QuicSendControlStream& operator=(const QuicSendControlStream&) = delete;
  ~QuicSendControlStream() override = default;

  // Overriding QuicStream::OnStreamReset to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Send |settings| on this stream.
  // Settings frame must be the first frame sent on this stream.
  void SendSettingsFrame(const SettingsFrame& settings);

  // The send control stream is write unidirectional, so this method should
  // never be called.
  void OnDataAvailable() override { QUIC_NOTREACHED(); }

 private:
  HttpEncoder encoder_;
  // Track if a settings frame is already sent.
  bool settings_sent_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
