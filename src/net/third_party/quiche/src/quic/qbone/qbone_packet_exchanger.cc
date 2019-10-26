// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_packet_exchanger.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"

namespace quic {

bool QbonePacketExchanger::ReadAndDeliverPacket(
    QboneClientInterface* qbone_client) {
  bool blocked = false;
  string error;
  std::unique_ptr<QuicData> packet = ReadPacket(&blocked, &error);
  if (packet == nullptr) {
    if (!blocked) {
      visitor_->OnReadError(error);
    }
    return false;
  }
  qbone_client->ProcessPacketFromNetwork(packet->AsStringPiece());
  return true;
}

void QbonePacketExchanger::WritePacketToNetwork(const char* packet,
                                                size_t size) {
  bool blocked = false;
  string error;
  if (packet_queue_.empty() && !write_blocked_) {
    if (WritePacket(packet, size, &blocked, &error)) {
      return;
    }
    if (!blocked) {
      visitor_->OnWriteError(error);
      return;
    }
    write_blocked_ = true;
  }

  // Drop the packet on the floor if the queue if full.
  if (packet_queue_.size() >= max_pending_packets_) {
    return;
  }

  auto data_copy = new char[size];
  memcpy(data_copy, packet, size);
  packet_queue_.push_back(
      QuicMakeUnique<QuicData>(data_copy, size, /* owns_buffer = */ true));
}

void QbonePacketExchanger::SetWritable() {
  write_blocked_ = false;
  while (!packet_queue_.empty()) {
    bool blocked = false;
    string error;
    if (WritePacket(packet_queue_.front()->data(),
                    packet_queue_.front()->length(), &blocked, &error)) {
      packet_queue_.pop_front();
    } else {
      if (!blocked) {
        visitor_->OnWriteError(error);
      }
      write_blocked_ = blocked;
      return;
    }
  }
}

}  // namespace quic
