// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_HEADERS_STREAM_H_
#define NET_QUIC_CORE_QUIC_HEADERS_STREAM_H_

#include <cstddef>
#include <memory>

#include "base/macros.h"
#include "net/quic/core/quic_header_list.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_stream.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/spdy/core/spdy_framer.h"

namespace net {

class QuicSpdySession;

namespace test {
class QuicHeadersStreamPeer;
}  // namespace test

// Headers in QUIC are sent as HTTP/2 HEADERS or PUSH_PROMISE frames over a
// reserved stream with the id 3.  Each endpoint (client and server) will
// allocate an instance of QuicHeadersStream to send and receive headers.
class QUIC_EXPORT_PRIVATE QuicHeadersStream : public QuicStream {
 public:
  explicit QuicHeadersStream(QuicSpdySession* session);
  ~QuicHeadersStream() override;

  // QuicStream implementation
  void OnDataAvailable() override;

  // Release underlying buffer if allowed.
  void MaybeReleaseSequencerBuffer();

  void OnStreamFrameAcked(const QuicStreamFrame& frame,
                          QuicTime::Delta ack_delay_time) override;

  void OnStreamFrameRetransmitted(const QuicStreamFrame& frame) override;

 private:
  friend class test::QuicHeadersStreamPeer;

  // CompressedHeaderInfo includes simple information of a header, including
  // offset in headers stream, unacked length and ack listener of this header.
  struct QUIC_EXPORT_PRIVATE CompressedHeaderInfo {
    CompressedHeaderInfo(
        QuicStreamOffset headers_stream_offset,
        QuicStreamOffset full_length,
        QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);
    CompressedHeaderInfo(const CompressedHeaderInfo& other);
    ~CompressedHeaderInfo();

    // Offset the header was sent on the headers stream.
    QuicStreamOffset headers_stream_offset;
    // The full length of the header.
    QuicByteCount full_length;
    // The remaining bytes to be acked.
    QuicByteCount unacked_length;
    // Ack listener of this header, and it is notified once any of the bytes has
    // been acked or retransmitted.
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener;
  };

  // Returns true if the session is still connected.
  bool IsConnected();

  // Override to store mapping from offset, length to ack_listener. This
  // ack_listener is notified once data within [offset, offset + length] is
  // acked or retransmitted.
  void OnDataBuffered(
      QuicStreamOffset offset,
      QuicByteCount data_length,
      const QuicReferenceCountedPointer<QuicAckListenerInterface>& ack_listener)
      override;

  QuicSpdySession* spdy_session_;

  // Headers that have not been fully acked.
  QuicDeque<CompressedHeaderInfo> unacked_headers_;

  DISALLOW_COPY_AND_ASSIGN(QuicHeadersStream);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_HEADERS_STREAM_H_
