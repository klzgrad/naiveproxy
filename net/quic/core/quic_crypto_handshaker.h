// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_CRYPTO_HANDSHAKER_H_
#define NET_QUIC_CORE_QUIC_CRYPTO_HANDSHAKER_H_

#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicCryptoHandshaker
    : public CryptoFramerVisitorInterface {
 public:
  QuicCryptoHandshaker(QuicCryptoStream* stream, QuicSession* session);

  ~QuicCryptoHandshaker() override;

  // Sends |message| to the peer.
  // TODO(wtc): return a success/failure status.
  void SendHandshakeMessage(const CryptoHandshakeMessage& message);

  void OnError(CryptoFramer* framer) override;
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

  CryptoMessageParser* crypto_message_parser();

 private:
  QuicSession* session() { return session_; }

  QuicCryptoStream* stream_;
  QuicSession* session_;

  CryptoFramer crypto_framer_;

  DISALLOW_COPY_AND_ASSIGN(QuicCryptoHandshaker);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_CRYPTO_HANDSHAKER_H_
