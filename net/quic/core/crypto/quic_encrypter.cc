// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/quic_encrypter.h"

#include "net/quic/core/crypto/aes_128_gcm_12_encrypter.h"
#include "net/quic/core/crypto/chacha20_poly1305_encrypter.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/crypto/null_encrypter.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

// static
QuicEncrypter* QuicEncrypter::Create(QuicTag algorithm) {
  switch (algorithm) {
    case kAESG:
      return new Aes128Gcm12Encrypter();
    case kCC20:
      return new ChaCha20Poly1305Encrypter();
    default:
      QUIC_LOG(FATAL) << "Unsupported algorithm: " << algorithm;
      return nullptr;
  }
}

}  // namespace net
