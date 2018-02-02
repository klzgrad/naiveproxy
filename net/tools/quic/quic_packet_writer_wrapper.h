// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_PACKET_WRITER_WRAPPER_H_
#define NET_TOOLS_QUIC_QUIC_PACKET_WRITER_WRAPPER_H_

#include <stddef.h>

#include <memory>

#include "base/macros.h"
#include "net/quic/core/quic_packet_writer.h"

namespace net {

// Wraps a writer object to allow dynamically extending functionality. Use
// cases: replace writer while dispatcher and connections hold on to the
// wrapper; mix in monitoring; mix in mocks in unit tests.
class QuicPacketWriterWrapper : public QuicPacketWriter {
 public:
  QuicPacketWriterWrapper();
  ~QuicPacketWriterWrapper() override;

  // Default implementation of the QuicPacketWriter interface. Passes everything
  // to |writer_|.
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

  // Takes ownership of |writer|.
  void set_writer(QuicPacketWriter* writer);

  virtual void set_peer_address(const QuicSocketAddress& peer_address) {}

  QuicPacketWriter* writer() { return writer_.get(); }

 private:
  std::unique_ptr<QuicPacketWriter> writer_;

  DISALLOW_COPY_AND_ASSIGN(QuicPacketWriterWrapper);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_PACKET_WRITER_WRAPPER_H_
