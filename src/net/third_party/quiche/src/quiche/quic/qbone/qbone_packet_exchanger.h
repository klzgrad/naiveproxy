// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_EXCHANGER_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_EXCHANGER_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/qbone/qbone_client_interface.h"
#include "quiche/quic/qbone/qbone_packet_writer.h"

namespace quic {

// Handles reading and writing on the local network and exchange packets between
// the local network with a QBONE connection.
class QbonePacketExchanger : public QbonePacketWriter {
 public:
  // The owner might want to receive notifications when read or write fails.
  class Visitor {
   public:
    virtual ~Visitor() {}
    virtual void OnReadError(const std::string& error) {}
    virtual void OnWriteError(const std::string& error) {}
    virtual absl::Status OnWrite(absl::string_view packet) {
      return absl::OkStatus();
    }
  };
  // Does not take ownership of visitor.
  QbonePacketExchanger(Visitor* visitor, size_t max_pending_packets)
      : visitor_(visitor), max_pending_packets_(max_pending_packets) {}

  QbonePacketExchanger(const QbonePacketExchanger&) = delete;
  QbonePacketExchanger& operator=(const QbonePacketExchanger&) = delete;

  QbonePacketExchanger(QbonePacketExchanger&&) = delete;
  QbonePacketExchanger& operator=(QbonePacketExchanger&&) = delete;

  ~QbonePacketExchanger() = default;

  // Returns true if there may be more packets to read.
  // Implementations handles the actual raw read and delivers the packet to
  // qbone_client.
  bool ReadAndDeliverPacket(QboneClientInterface* qbone_client);

  // From QbonePacketWriter.
  // Writes a packet to the local network. If the write would be blocked, the
  // packet will be queued if the queue is smaller than max_pending_packets_.
  void WritePacketToNetwork(const char* packet, size_t size) override;

  // The caller signifies that the local network is no longer blocked.
  void SetWritable();

 private:
  // The actual implementation that reads a packet from the local network.
  // Returns the packet if one is successfully read. This might nullptr when a)
  // there is no packet to read, b) the read failed. In the former case, blocked
  // is set to true. error contains the error message.
  virtual std::unique_ptr<QuicData> ReadPacket(bool* blocked,
                                               std::string* error) = 0;

  // The actual implementation that writes a packet to the local network.
  // Returns true if the write succeeds. blocked will be set to true if the
  // write failure is caused by the local network being blocked. error contains
  // the error message.
  virtual bool WritePacket(const char* packet, size_t size, bool* blocked,
                           std::string* error) = 0;

  std::list<std::unique_ptr<QuicData>> packet_queue_;

  Visitor* visitor_;

  // The maximum number of packets that could be queued up when writing to local
  // network is blocked.
  size_t max_pending_packets_;

  bool write_blocked_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_EXCHANGER_H_
