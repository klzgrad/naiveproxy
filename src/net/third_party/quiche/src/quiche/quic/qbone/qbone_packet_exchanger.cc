// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_packet_exchanger.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace quic {

bool QbonePacketExchanger::ReadAndDeliverPacket(
    QboneClientInterface* qbone_client) {
  std::string error;
  std::unique_ptr<QuicData> packet = ReadPacket(&error);
  if (packet == nullptr) {
    if (visitor_) {
      visitor_->OnReadError(error);
    }
    return false;
  }
  qbone_client->ProcessPacketFromNetwork(packet->AsStringPiece());
  return true;
}

void QbonePacketExchanger::WritePacketToNetwork(const char* packet,
                                                size_t size) {
  if (visitor_) {
    absl::Status status = visitor_->OnWrite(absl::string_view(packet, size));
    if (!status.ok()) {
      QUIC_LOG_EVERY_N_SEC(ERROR, 60) << status;
    }
  }

  std::string error;
  if (WritePacket(packet, size, &error)) {
    return;
  }
  QUIC_LOG_EVERY_N_SEC(ERROR, 60) << "Packet write failed: " << error;
  if (visitor_) {
    visitor_->OnWriteError(error);
  }
}
}  // namespace quic
