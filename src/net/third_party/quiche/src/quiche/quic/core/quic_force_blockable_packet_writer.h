// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_FORCE_BLOCKABLE_PACKET_WRITER_H_
#define QUICHE_QUIC_CORE_QUIC_FORCE_BLOCKABLE_PACKET_WRITER_H_

#include "quiche/quic/core/quic_packet_writer_wrapper.h"

namespace quic {

// A QuicPacketWriterWrapper implementation that can be forced to be write
// blocked.
class QUICHE_EXPORT QuicForceBlockablePacketWriter
    : public QuicPacketWriterWrapper {
 public:
  // If `enforce_write_block` is true, IsWriteBlocked() will always return true
  // regardless of whether SetWritable() is called or not until
  // this method is called again with `enforce_write_block` false.
  // If `enforce_write_block` is false, SetWritable() may still be needed to
  // make IsWriteBlocked() to return true.
  void ForceWriteBlocked(bool enforce_write_block);

  bool IsWriteBlocked() const override;

 private:
  bool enforce_write_block_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_FORCE_BLOCKABLE_PACKET_WRITER_H_
