// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SIMPLE_PER_CONNECTION_PACKET_WRITER_H_
#define NET_TOOLS_QUIC_QUIC_SIMPLE_PER_CONNECTION_PACKET_WRITER_H_

#include <stddef.h>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_packet_writer.h"

namespace net {

class QuicSimpleServerPacketWriter;

// A connection-specific packet writer that notifies its connection when its
// writes to the shared QuicServerPacketWriter complete.
// This class is necessary because multiple connections can share the same
// QuicServerPacketWriter, so it has no way to know which connection to notify.
class QuicSimplePerConnectionPacketWriter : public QuicPacketWriter {
 public:
  // Does not take ownership of |shared_writer| or |connection|.
  QuicSimplePerConnectionPacketWriter(
      QuicSimpleServerPacketWriter* shared_writer);
  ~QuicSimplePerConnectionPacketWriter() override;

  QuicPacketWriter* shared_writer() const;
  void set_connection(QuicConnection* connection) { connection_ = connection; }
  QuicConnection* connection() const { return connection_; }

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
  void OnWriteComplete(WriteResult result);

  QuicSimpleServerPacketWriter* shared_writer_;  // Not owned.
  QuicConnection* connection_;                   // Not owned.

  base::WeakPtrFactory<QuicSimplePerConnectionPacketWriter> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(QuicSimplePerConnectionPacketWriter);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SIMPLE_PER_CONNECTION_PACKET_WRITER_H_
