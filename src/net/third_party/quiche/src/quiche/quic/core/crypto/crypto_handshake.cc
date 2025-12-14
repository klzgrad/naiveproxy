// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/crypto_handshake.h"

#include "quiche/quic/core/crypto/key_exchange.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"

namespace quic {

QuicCryptoNegotiatedParameters::QuicCryptoNegotiatedParameters()
    : key_exchange(0),
      aead(0),
      token_binding_key_param(0),
      sct_supported_by_client(false) {}

QuicCryptoNegotiatedParameters::~QuicCryptoNegotiatedParameters() {}

CrypterPair::CrypterPair() {}

CrypterPair::~CrypterPair() {}

// static
const char QuicCryptoConfig::kInitialLabel[] = "QUIC key expansion";

// static
const char QuicCryptoConfig::kCETVLabel[] = "QUIC CETV block";

// static
const char QuicCryptoConfig::kForwardSecureLabel[] =
    "QUIC forward secure key expansion";

QuicCryptoConfig::QuicCryptoConfig() = default;

QuicCryptoConfig::~QuicCryptoConfig() = default;

}  // namespace quic
