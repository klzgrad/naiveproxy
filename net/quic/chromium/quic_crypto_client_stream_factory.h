// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_QUIC_CRYPTO_CLIENT_STREAM_FACTORY_H_
#define NET_QUIC_CHROMIUM_QUIC_CRYPTO_CLIENT_STREAM_FACTORY_H_

#include <memory>
#include <string>

#include "net/base/net_export.h"
#include "net/quic/core/quic_server_id.h"

namespace net {

class ProofVerifyContext;
class QuicChromiumClientSession;
class QuicCryptoClientConfig;
class QuicCryptoClientStream;

// An interface used to instantiate QuicCryptoClientStream objects. Used to
// facilitate testing code with mock implementations.
class NET_EXPORT QuicCryptoClientStreamFactory {
 public:
  virtual ~QuicCryptoClientStreamFactory() {}

  virtual QuicCryptoClientStream* CreateQuicCryptoClientStream(
      const QuicServerId& server_id,
      QuicChromiumClientSession* session,
      std::unique_ptr<ProofVerifyContext> proof_verify_context,
      QuicCryptoClientConfig* crypto_config) = 0;

  static QuicCryptoClientStreamFactory* GetDefaultFactory();
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_CRYPTO_CLIENT_STREAM_FACTORY_H_
