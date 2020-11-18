// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"

namespace quic {

CryptoBuffers::~CryptoBuffers() {
  for (size_t i = 0; i < value.size(); i++) {
    CRYPTO_BUFFER_free(value[i]);
  }
}

ProofSource::Chain::Chain(const std::vector<std::string>& certs)
    : certs(certs) {}

ProofSource::Chain::~Chain() {}

CryptoBuffers ProofSource::Chain::ToCryptoBuffers() const {
  CryptoBuffers crypto_buffers;
  crypto_buffers.value.reserve(certs.size());
  for (size_t i = 0; i < certs.size(); i++) {
    crypto_buffers.value.push_back(
        CRYPTO_BUFFER_new(reinterpret_cast<const uint8_t*>(certs[i].data()),
                          certs[i].length(), nullptr));
  }
  return crypto_buffers;
}

}  // namespace quic
