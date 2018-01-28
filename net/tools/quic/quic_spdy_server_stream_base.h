// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_BASE_H_
#define NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_BASE_H_

#include "net/quic/core/quic_spdy_stream.h"

namespace net {

class QuicSpdyServerStreamBase : public QuicSpdyStream {
 public:
  QuicSpdyServerStreamBase(QuicStreamId id, QuicSpdySession* session);

  // Override the base class to send QUIC_STREAM_NO_ERROR to the peer
  // when the stream has not received all the data.
  void CloseWriteSide() override;
  void StopReading() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicSpdyServerStreamBase);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_BASE_H_
