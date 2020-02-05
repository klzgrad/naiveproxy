// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_simple_crypto_server_stream_helper.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {

QuicSimpleCryptoServerStreamHelper::QuicSimpleCryptoServerStreamHelper() =
    default;

QuicSimpleCryptoServerStreamHelper::~QuicSimpleCryptoServerStreamHelper() =
    default;

bool QuicSimpleCryptoServerStreamHelper::CanAcceptClientHello(
    const CryptoHandshakeMessage& /*message*/,
    const QuicSocketAddress& /*client_address*/,
    const QuicSocketAddress& /*peer_address*/,
    const QuicSocketAddress& /*self_address*/,
    std::string* /*error_details*/) const {
  return true;
}

}  // namespace quic
