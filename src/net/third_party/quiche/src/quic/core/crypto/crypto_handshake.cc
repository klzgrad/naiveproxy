// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"

#include "net/third_party/quiche/src/quic/core/crypto/common_cert_set.h"
#include "net/third_party/quiche/src/quic/core/crypto/key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"

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

QuicCryptoConfig::QuicCryptoConfig()
    : common_cert_sets(CommonCertSets::GetInstanceQUIC()) {}

QuicCryptoConfig::~QuicCryptoConfig() {}

}  // namespace quic
