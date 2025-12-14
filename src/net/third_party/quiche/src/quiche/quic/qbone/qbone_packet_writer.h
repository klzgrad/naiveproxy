// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_WRITER_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_WRITER_H_

#include <cstring>

namespace quic {

// QbonePacketWriter expects only one function to be defined,
// WritePacketToNetwork, which is called when a packet is received via QUIC
// and should be sent out on the network.  This is the complete packet,
// and not just a fragment.
class QbonePacketWriter {
 public:
  virtual ~QbonePacketWriter() {}
  virtual void WritePacketToNetwork(const char* packet, size_t size) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_WRITER_H_
