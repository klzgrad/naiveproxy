// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

class QuicSpdySession;

// 6.2.1 Control Stream.
// The send control stream is self initiated and is write only.
class QUIC_EXPORT_PRIVATE QuicSendControlStream : public QuicStream {
 public:
  // |session| can't be nullptr, and the ownership is not passed. The stream can
  // only be accessed through the session.
  QuicSendControlStream(QuicStreamId id,
                        QuicSpdySession* session,
                        uint64_t qpack_maximum_dynamic_table_capacity,
                        uint64_t qpack_maximum_blocked_streams,
                        uint64_t max_inbound_header_list_size);
  QuicSendControlStream(const QuicSendControlStream&) = delete;
  QuicSendControlStream& operator=(const QuicSendControlStream&) = delete;
  ~QuicSendControlStream() override = default;

  // Overriding QuicStream::OnStopSending() to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;
  bool OnStopSending(uint16_t code) override;

  // Send SETTINGS frame if it hasn't been sent yet. Settings frame must be the
  // first frame sent on this stream.
  void MaybeSendSettingsFrame();

  // Send a MAX_PUSH_ID frame on this stream, and a SETTINGS frame beforehand if
  // one has not been already sent.  Must only be called for a client.
  void SendMaxPushIdFrame(PushId max_push_id);

  // Send a PRIORITY_UPDATE frame on this stream, and a SETTINGS frame
  // beforehand if one has not been already sent.
  void WritePriorityUpdate(const PriorityUpdateFrame& priority_update);

  // Send a GOAWAY frame on this stream, and a SETTINGS frame beforehand if one
  // has not been already sent.
  void SendGoAway(QuicStreamId stream_id);

  // The send control stream is write unidirectional, so this method should
  // never be called.
  void OnDataAvailable() override { QUIC_NOTREACHED(); }

 private:
  // Track if a settings frame is already sent.
  bool settings_sent_;

  // SETTINGS_QPACK_MAX_TABLE_CAPACITY value to send.
  const uint64_t qpack_maximum_dynamic_table_capacity_;
  // SETTINGS_QPACK_BLOCKED_STREAMS value to send.
  const uint64_t qpack_maximum_blocked_streams_;
  // SETTINGS_MAX_HEADER_LIST_SIZE value to send.
  const uint64_t max_inbound_header_list_size_;

  QuicSpdySession* const spdy_session_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
