// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_
#define NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_

#include "net/third_party/quic/core/crypto/quic_random.h"
#include "net/third_party/quic/core/quic_crypto_server_stream.h"

namespace quic {

// Simple helper for server crypto streams which generates a new random
// connection ID for stateless rejects.
class QuicSimpleCryptoServerStreamHelper
    : public QuicCryptoServerStream::Helper {
 public:
  explicit QuicSimpleCryptoServerStreamHelper(QuicRandom* random);

  ~QuicSimpleCryptoServerStreamHelper() override;

  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId /*connection_id*/) const override;

  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& client_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;

 private:
  QuicRandom* random_;  // Unowned.
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_
