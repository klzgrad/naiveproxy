// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_CHLO_EXTRACTOR_H_
#define NET_TOOLS_QUIC_CHLO_EXTRACTOR_H_

#include "net/quic/core/crypto/crypto_handshake_message.h"
#include "net/quic/core/quic_packets.h"

namespace net {

// A utility for extracting QUIC Client Hello messages from packets,
// without needs to spin up a full QuicSession.
class ChloExtractor {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when a CHLO message is found in the packets.
    virtual void OnChlo(QuicTransportVersion version,
                        QuicConnectionId connection_id,
                        const CryptoHandshakeMessage& chlo) = 0;
  };

  // Extracts a CHLO message from |packet| and invokes the OnChlo method
  // of |delegate|. Return true if a CHLO message was found, and false
  // otherwise.
  static bool Extract(const QuicEncryptedPacket& packet,
                      const QuicTransportVersionVector& versions,
                      Delegate* delegate);

  ChloExtractor(const ChloExtractor&) = delete;
  ChloExtractor operator=(const ChloExtractor&) = delete;
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_CHLO_EXTRACTOR_H_
