// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_

#include "net/third_party/quic/core/quic_packet_writer.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/quartc/quartc_session_interface.h"

namespace net {

// Implements a QuicPacketWriter using a QuartcPacketTransport, which allows a
// QuicConnection to use (for example), a WebRTC IceTransport.
class QUIC_EXPORT_PRIVATE QuartcPacketWriter : public QuicPacketWriter {
 public:
  QuartcPacketWriter(QuartcPacketTransport* packet_transport,
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
  // outgoing QUIC packets are queued by QuicConnection until SetWritable() is
  // called.
  bool IsWriteBlocked() const override;

  // Maximum size of the QUIC packet which can be written. Users such as WebRTC
  // can set the value through the QuartcFactoryConfig without updating the QUIC
  // code.
  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const override;

  // Sets the packet writer to a writable (non-blocked) state.
  void SetWritable() override;

  // Sets the connection which sends packets using this writer.  Connection must
  // be set in order to attach packet info (eg. packet numbers) to writes.
  void set_connection(QuicConnection* connection) { connection_ = connection; }

 private:
  // QuartcPacketWriter will not own the transport.
  QuartcPacketTransport* packet_transport_;
  // The maximum size of the packet can be written by this writer.
  QuicByteCount max_packet_size_;

  // The current connection sending packets using this writer.
  QuicConnection* connection_;

  // Whether packets can be written.
  bool writable_ = false;
};

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_
