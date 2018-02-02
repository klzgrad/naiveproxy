// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_
#define NET_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_

#include "net/quic/core/quic_packet_writer.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/quartc/quartc_session_interface.h"

namespace net {

// Implements a QuicPacketWriter using a
// QuartcSessionInterface::PacketTransport, which allows a QuicConnection to
// use(for example), a WebRTC IceTransport.
class QUIC_EXPORT_PRIVATE QuartcPacketWriter : public QuicPacketWriter {
 public:
  QuartcPacketWriter(QuartcSessionInterface::PacketTransport* packet_transport,
                     QuicByteCount max_packet_size);
  ~QuartcPacketWriter() override {}

  // The QuicConnection calls WritePacket and the QuicPacketWriter writes them
  // to the QuartcSessionInterface::PacketTransport.
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;

  // This is always set to false so that QuicConnection buffers unsent packets.
  bool IsWriteBlockedDataBuffered() const override;

  // Whether the underneath |transport_| is blocked. If this returns true,
  // outgoing QUIC packets are queued by QuicConnection until
  // Transport::Observer::OnCanWrite() is called.
  bool IsWriteBlocked() const override;

  // Maximum size of the QUIC packet which can be written. Users such as WebRTC
  // can set the value through the QuartcFactoryConfig without updating the QUIC
  // code.
  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const override;

  // This method is not used because the network layer in WebRTC will determine
  // the writing states.
  void SetWritable() override;

 private:
  // QuartcPacketWriter will not own the transport.
  QuartcSessionInterface::PacketTransport* packet_transport_;
  // The maximum size of the packet can be written by this writer.
  QuicByteCount max_packet_size_;
};

}  // namespace net

#endif  // NET_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_
