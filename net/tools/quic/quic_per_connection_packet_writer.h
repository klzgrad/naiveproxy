// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_PER_CONNECTION_PACKET_WRITER_H_
#define NET_TOOLS_QUIC_QUIC_PER_CONNECTION_PACKET_WRITER_H_

#include <stddef.h>

#include "base/macros.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_packet_writer.h"

namespace net {

// A connection-specific packet writer that wraps a shared writer.
class QuicPerConnectionPacketWriter : public QuicPacketWriter {
 public:
  // Does not take ownership of |shared_writer|.
  explicit QuicPerConnectionPacketWriter(QuicPacketWriter* shared_writer);
  ~QuicPerConnectionPacketWriter() override;

  // Default implementation of the QuicPacketWriter interface: Passes everything
  // to |shared_writer_|.
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;
  bool IsWriteBlockedDataBuffered() const override;
  bool IsWriteBlocked() const override;
  void SetWritable() override;
  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const override;

 private:
  QuicPacketWriter* shared_writer_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(QuicPerConnectionPacketWriter);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_PER_CONNECTION_PACKET_WRITER_H_
