// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_force_blockable_packet_writer.h"

namespace quic {

void QuicForceBlockablePacketWriter::ForceWriteBlocked(
    bool enforce_write_block) {
  enforce_write_block_ = enforce_write_block;
}

bool QuicForceBlockablePacketWriter::IsWriteBlocked() const {
  return enforce_write_block_ || QuicPacketWriterWrapper::IsWriteBlocked();
}

}  // namespace quic
