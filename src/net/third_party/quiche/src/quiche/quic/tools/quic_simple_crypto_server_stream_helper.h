// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"

namespace quic {

// Simple helper for server crypto streams which generates a new random
// connection ID for rejects.
class QuicSimpleCryptoServerStreamHelper
    : public QuicCryptoServerStreamBase::Helper {
 public:
  QuicSimpleCryptoServerStreamHelper();

  ~QuicSimpleCryptoServerStreamHelper() override;

  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& client_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CRYPTO_SERVER_STREAM_HELPER_H_
