// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CHLO_EXTRACTOR_H_
#define QUICHE_QUIC_CORE_CHLO_EXTRACTOR_H_

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"

namespace quic {

// A utility for extracting QUIC Client Hello messages from packets,
// without needs to spin up a full QuicSession.
class QUIC_NO_EXPORT ChloExtractor {
 public:
  class QUIC_NO_EXPORT Delegate {
   public:
    virtual ~Delegate() {}

    // Called when a CHLO message is found in the packets.
    virtual void OnChlo(QuicTransportVersion version,
                        QuicConnectionId connection_id,
                        const CryptoHandshakeMessage& chlo) = 0;
  };

  // Extracts a CHLO message from |packet| and invokes the OnChlo
  // method of |delegate|. Return true if a CHLO message was found,
  // and false otherwise. If non-empty,
  // |create_session_tag_indicators| contains a list of QUIC tags that
  // if found will result in the session being created early, to
  // enable support for multi-packet CHLOs.
  static bool Extract(const QuicEncryptedPacket& packet,
                      ParsedQuicVersion version,
                      const QuicTagVector& create_session_tag_indicators,
                      Delegate* delegate,
                      uint8_t connection_id_length);

  ChloExtractor(const ChloExtractor&) = delete;
  ChloExtractor operator=(const ChloExtractor&) = delete;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CHLO_EXTRACTOR_H_
