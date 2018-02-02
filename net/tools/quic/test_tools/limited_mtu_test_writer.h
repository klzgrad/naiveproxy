// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_TEST_TOOLS_LIMITED_MTU_TEST_WRITER_H_
#define NET_TOOLS_QUIC_TEST_TOOLS_LIMITED_MTU_TEST_WRITER_H_

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/tools/quic/quic_packet_writer_wrapper.h"

namespace net {
namespace test {

// Simulates a connection over a link with fixed MTU.  Drops packets which
// exceed the MTU and passes the rest of them as-is.
class LimitedMtuTestWriter : public QuicPacketWriterWrapper {
 public:
  explicit LimitedMtuTestWriter(QuicByteCount mtu);
  ~LimitedMtuTestWriter() override;

  // Inherited from QuicPacketWriterWrapper.
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;

 private:
  QuicByteCount mtu_;

  DISALLOW_COPY_AND_ASSIGN(LimitedMtuTestWriter);
};

}  // namespace test
}  // namespace net

#endif  // NET_TOOLS_QUIC_TEST_TOOLS_LIMITED_MTU_TEST_WRITER_H_
