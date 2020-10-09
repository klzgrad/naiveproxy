// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_HANDSHAKER_H_

#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicCryptoHandshaker
    : public CryptoFramerVisitorInterface {
 public:
  QuicCryptoHandshaker(QuicCryptoStream* stream, QuicSession* session);
  QuicCryptoHandshaker(const QuicCryptoHandshaker&) = delete;
  QuicCryptoHandshaker& operator=(const QuicCryptoHandshaker&) = delete;

  ~QuicCryptoHandshaker() override;

  // Sends |message| to the peer.
  // TODO(wtc): return a success/failure status.
  void SendHandshakeMessage(const CryptoHandshakeMessage& message);

  void OnError(CryptoFramer* framer) override;
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

  CryptoMessageParser* crypto_message_parser();
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const;

 protected:
  QuicTag last_sent_handshake_message_tag() const {
    return last_sent_handshake_message_tag_;
  }

 private:
  QuicSession* session() { return session_; }

  QuicCryptoStream* stream_;
  QuicSession* session_;

  CryptoFramer crypto_framer_;

  // Records last sent crypto handshake message tag.
  QuicTag last_sent_handshake_message_tag_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_HANDSHAKER_H_
