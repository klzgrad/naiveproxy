// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The Chrome-specific helper for QuicConnection which uses
// a TaskRunner for alarms, and uses a DatagramClientSocket for writing data.

#ifndef NET_QUIC_CHROMIUM_QUIC_CHROMIUM_CONNECTION_HELPER_H_
#define NET_QUIC_CHROMIUM_QUIC_CHROMIUM_CONNECTION_HELPER_H_

#include "base/macros.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_export.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_simple_buffer_allocator.h"
#include "net/quic/core/quic_time.h"
#include "net/socket/datagram_client_socket.h"

namespace net {

class QuicClock;
class QuicRandom;

class NET_EXPORT_PRIVATE QuicChromiumConnectionHelper
    : public QuicConnectionHelperInterface {
 public:
  QuicChromiumConnectionHelper(const QuicClock* clock,
                               QuicRandom* random_generator);
  ~QuicChromiumConnectionHelper() override;

  // QuicConnectionHelperInterface
  const QuicClock* GetClock() const override;
  QuicRandom* GetRandomGenerator() override;
  QuicBufferAllocator* GetStreamSendBufferAllocator() override;

 private:
  const QuicClock* clock_;
  QuicRandom* random_generator_;
  SimpleBufferAllocator buffer_allocator_;

  DISALLOW_COPY_AND_ASSIGN(QuicChromiumConnectionHelper);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_CHROMIUM_CONNECTION_HELPER_H_
