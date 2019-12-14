// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_
#define QUICHE_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_

#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

// Send and receive packets, like a virtual UDP socket. For example, this
// could be implemented by WebRTC's IceTransport.
class QuartcPacketTransport {
 public:
  // Additional metadata provided for each packet written.
  struct PacketInfo {
    QuicPacketNumber packet_number;
  };

  // Delegate for packet transport callbacks.  Note that the delegate is not
  // thread-safe.  Packet transport implementations must ensure that callbacks
  // are synchronized with all other work done by QUIC.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called whenever the transport can write.
    virtual void OnTransportCanWrite() = 0;

    // Called when the transport receives a packet.
    virtual void OnTransportReceived(const char* data, size_t data_len) = 0;
  };

  virtual ~QuartcPacketTransport() {}

  // Called by the QuartcPacketWriter when writing packets to the network.
  // Return the number of written bytes. Return 0 if the write is blocked.
  virtual int Write(const char* buffer,
                    size_t buf_len,
                    const PacketInfo& info) = 0;

  // Sets the delegate which must be called when the transport can write or
  // a packet is received.  QUIC sets |delegate| to a nonnull pointer when it
  // is ready to process incoming packets and sets |delegate| to nullptr before
  // QUIC is deleted.  Implementations may assume |delegate| remains valid until
  // it is set to nullptr.
  virtual void SetDelegate(Delegate* delegate) = 0;
};

struct QuartcPerPacketOptions : public PerPacketOptions {
  std::unique_ptr<PerPacketOptions> Clone() const override;

  // The connection which is sending this packet.
  QuicConnection* connection = nullptr;
};

// Implements a QuicPacketWriter using a QuartcPacketTransport, which allows a
// QuicConnection to use (for example), a WebRTC IceTransport.
class QuartcPacketWriter : public QuicPacketWriter {
 public:
  QuartcPacketWriter(QuartcPacketTransport* packet_transport,
                     QuicByteCount max_packet_size);
  ~QuartcPacketWriter() override {}

  // The QuicConnection calls WritePacket and the QuicPacketWriter writes them
  // to the QuartcSession::PacketTransport.
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;

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

  bool SupportsReleaseTime() const override;

  bool IsBatchMode() const override;

  char* GetNextWriteLocation(const QuicIpAddress& self_address,
                             const QuicSocketAddress& peer_address) override;

  WriteResult Flush() override;

  void SetPacketTransportDelegate(QuartcPacketTransport::Delegate* delegate);

 private:
  // QuartcPacketWriter will not own the transport.
  QuartcPacketTransport* packet_transport_;
  // The maximum size of the packet can be written by this writer.
  QuicByteCount max_packet_size_;

  // Whether packets can be written.
  bool writable_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_PACKET_WRITER_H_
