// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_
#define NET_TOOLS_QUIC_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_

#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/quic_crypto_server_stream.h"

namespace net {

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
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;

 private:
  QuicRandom* random_;  // Unowned.
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_
