// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_crypto_handshaker.h"

#include "net/third_party/quic/core/quic_session.h"

namespace quic {

#define ENDPOINT \
  (session()->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicCryptoHandshaker::QuicCryptoHandshaker(QuicCryptoStream* stream,
                                           QuicSession* session)
    : stream_(stream), session_(session), last_sent_handshake_message_tag_(0) {
  crypto_framer_.set_visitor(this);
}

QuicCryptoHandshaker::~QuicCryptoHandshaker() {}

void QuicCryptoHandshaker::SendHandshakeMessage(
    const CryptoHandshakeMessage& message) {
  QUIC_DVLOG(1) << ENDPOINT << "Sending " << message.DebugString();
  session()->NeuterUnencryptedData();
  session()->OnCryptoHandshakeMessageSent(message);
  last_sent_handshake_message_tag_ = message.tag();
  const QuicData& data = message.GetSerialized();
  stream_->WriteOrBufferData(QuicStringPiece(data.data(), data.length()), false,
                             nullptr);
}

void QuicCryptoHandshaker::OnError(CryptoFramer* framer) {
  QUIC_DLOG(WARNING) << "Error processing crypto data: "
                     << QuicErrorCodeToString(framer->error());
}

void QuicCryptoHandshaker::OnHandshakeMessage(
    const CryptoHandshakeMessage& message) {
  QUIC_DVLOG(1) << ENDPOINT << "Received " << message.DebugString();
  session()->OnCryptoHandshakeMessageReceived(message);
}

CryptoMessageParser* QuicCryptoHandshaker::crypto_message_parser() {
  return &crypto_framer_;
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
