// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QuicSession;

// 3.2.1 Control Stream.
// The send control stream is self initiated and is write only.
class QUIC_EXPORT_PRIVATE QuicSendControlStream : public QuicStream {
 public:
  // |session| can't be nullptr, and the ownership is not passed. The stream can
  // only be accessed through the session.
  QuicSendControlStream(QuicStreamId id,
                        QuicSession* session,
                        uint64_t qpack_maximum_dynamic_table_capacity,
                        uint64_t qpack_maximum_blocked_streams,
                        uint64_t max_inbound_header_list_size);
  QuicSendControlStream(const QuicSendControlStream&) = delete;
  QuicSendControlStream& operator=(const QuicSendControlStream&) = delete;
  ~QuicSendControlStream() override = default;

  // Overriding QuicStream::OnStreamReset to make sure control stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Send SETTINGS frame if it hasn't been sent yet. Settings frame must be the
  // first frame sent on this stream.
  void MaybeSendSettingsFrame();

  // Construct a MAX_PUSH_ID frame and send it on this stream.
  void SendMaxPushIdFrame(PushId max_push_id);

  // Send |Priority| on this stream. It must be sent after settings.
  void WritePriority(const PriorityFrame& priority);

  // The send control stream is write unidirectional, so this method should
  // never be called.
  void OnDataAvailable() override { QUIC_NOTREACHED(); }

 private:
  HttpEncoder encoder_;
  // Track if a settings frame is already sent.
  bool settings_sent_;

  // SETTINGS_QPACK_MAX_TABLE_CAPACITY value to send.
  const uint64_t qpack_maximum_dynamic_table_capacity_;
  // SETTINGS_QPACK_BLOCKED_STREAMS value to send.
  const uint64_t qpack_maximum_blocked_streams_;
  // SETTINGS_MAX_HEADER_LIST_SIZE value to send.
  const uint64_t max_inbound_header_list_size_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SEND_CONTROL_STREAM_H_
