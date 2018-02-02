// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_crypto_server_stream.h"

#include <memory>

#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/crypto/crypto_utils.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/proto/cached_network_parameters.pb.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_crypto_server_handshaker.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_session.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_string_piece.h"

using std::string;

namespace net {

QuicCryptoServerStreamBase::QuicCryptoServerStreamBase(QuicSession* session)
    : QuicCryptoStream(session) {}

// TODO(jokulik): Once stateless rejects support is inherent in the version
// number, this function will likely go away entirely.
// static
bool QuicCryptoServerStreamBase::DoesPeerSupportStatelessRejects(
    const CryptoHandshakeMessage& message) {
  QuicTagVector received_tags;
  QuicErrorCode error = message.GetTaglist(kCOPT, &received_tags);
  if (error != QUIC_NO_ERROR) {
    return false;
  }
  for (const QuicTag tag : received_tags) {
    if (tag == kSREJ) {
      return true;
    }
  }
  return false;
}

QuicCryptoServerStream::QuicCryptoServerStream(
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    bool use_stateless_rejects_if_peer_supported,
    QuicSession* session,
    Helper* helper)
    : QuicCryptoServerStreamBase(session) {
  DCHECK_EQ(Perspective::IS_SERVER, session->connection()->perspective());
  handshaker_.reset(new QuicCryptoServerHandshaker(
      crypto_config, this, compressed_certs_cache,
      use_stateless_rejects_if_peer_supported, session, helper));
}

QuicCryptoServerStream::~QuicCryptoServerStream() {}

void QuicCryptoServerStream::CancelOutstandingCallbacks() {
  handshaker()->CancelOutstandingCallbacks();
}

bool QuicCryptoServerStream::GetBase64SHA256ClientChannelID(
    string* output) const {
  return handshaker()->GetBase64SHA256ClientChannelID(output);
}

void QuicCryptoServerStream::SendServerConfigUpdate(
    const CachedNetworkParameters* cached_network_params) {
  handshaker()->SendServerConfigUpdate(cached_network_params);
}

uint8_t QuicCryptoServerStream::NumHandshakeMessages() const {
  return handshaker()->NumHandshakeMessages();
}

uint8_t QuicCryptoServerStream::NumHandshakeMessagesWithServerNonces() const {
  return handshaker()->NumHandshakeMessagesWithServerNonces();
}

int QuicCryptoServerStream::NumServerConfigUpdateMessagesSent() const {
  return handshaker()->NumServerConfigUpdateMessagesSent();
}

const CachedNetworkParameters*
QuicCryptoServerStream::PreviousCachedNetworkParams() const {
  return handshaker()->PreviousCachedNetworkParams();
}

bool QuicCryptoServerStream::UseStatelessRejectsIfPeerSupported() const {
  return handshaker()->UseStatelessRejectsIfPeerSupported();
}

bool QuicCryptoServerStream::PeerSupportsStatelessRejects() const {
  return handshaker()->PeerSupportsStatelessRejects();
}

bool QuicCryptoServerStream::ZeroRttAttempted() const {
  return handshaker()->ZeroRttAttempted();
}

void QuicCryptoServerStream::SetPeerSupportsStatelessRejects(
    bool peer_supports_stateless_rejects) {
  handshaker()->SetPeerSupportsStatelessRejects(
      peer_supports_stateless_rejects);
}

void QuicCryptoServerStream::SetPreviousCachedNetworkParams(
    CachedNetworkParameters cached_network_params) {
  handshaker()->SetPreviousCachedNetworkParams(cached_network_params);
}

bool QuicCryptoServerStream::ShouldSendExpectCTHeader() const {
  return handshaker()->ShouldSendExpectCTHeader();
}

bool QuicCryptoServerStream::encryption_established() const {
  return handshaker()->encryption_established();
}

bool QuicCryptoServerStream::handshake_confirmed() const {
  return handshaker()->handshake_confirmed();
}

const QuicCryptoNegotiatedParameters&
QuicCryptoServerStream::crypto_negotiated_params() const {
  return handshaker()->crypto_negotiated_params();
}

CryptoMessageParser* QuicCryptoServerStream::crypto_message_parser() {
  return handshaker()->crypto_message_parser();
}

QuicCryptoServerStream::HandshakerDelegate* QuicCryptoServerStream::handshaker()
    const {
  return handshaker_.get();
}

}  // namespace net
