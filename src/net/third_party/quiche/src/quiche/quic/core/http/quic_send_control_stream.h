// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_

#include "quiche/quic/core/http/http_encoder.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

class QuicSpdySession;

// 6.2.1 Control Stream.
// The send control stream is self initiated and is write only.
class QUICHE_EXPORT QuicSendControlStream : public QuicStream {
 public:
  // |session| can't be nullptr, and the ownership is not passed. The stream can
  // only be accessed through the session.
  QuicSendControlStream(QuicStreamId id, QuicSpdySession* session,
                        const SettingsFrame& settings);
  QuicSendControlStream(const QuicSendControlStream&) = delete;
  QuicSendControlStream& operator=(const QuicSendControlStream&) = delete;
  ~QuicSendControlStream() override = default;

  // Overriding QuicStream::OnStopSending() to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;
  bool OnStopSending(QuicResetStreamError code) override;

  // Send SETTINGS frame if it hasn't been sent yet. Settings frame must be the
  // first frame sent on this stream.
  void MaybeSendSettingsFrame();

  // Send ORIGIN frame if |origins| is not empty.
  void MaybeSendOriginFrame(std::vector<std::string> origins);

  // Send a PRIORITY_UPDATE frame on this stream, and a SETTINGS frame
  // beforehand if one has not been already sent.
  void WritePriorityUpdate(QuicStreamId stream_id, HttpStreamPriority priority);

  // Send a GOAWAY frame on this stream, and a SETTINGS frame beforehand if one
  // has not been already sent.
  void SendGoAway(QuicStreamId id);

  // The send control stream is write unidirectional, so this method should
  // never be called.
  void OnDataAvailable() override { QUICHE_NOTREACHED(); }

 private:
  // Track if a settings frame is already sent.
  bool settings_sent_;

  // Track if an origin frame is already sent.
  bool origin_frame_sent_;

  // SETTINGS values to send.
  const SettingsFrame settings_;

  QuicSpdySession* const spdy_session_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
