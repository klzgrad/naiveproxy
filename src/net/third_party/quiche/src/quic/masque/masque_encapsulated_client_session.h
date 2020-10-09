// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_SESSION_H_

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quiche/src/quic/masque/masque_client_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// QUIC client session for QUIC encapsulated in MASQUE. This client session is
// maintained end-to-end between the client and the web-server (the MASQUE
// session does not have access to the cryptographic keys for the end-to-end
// session), but its packets are sent encapsulated inside DATAGRAM frames in a
// MASQUE session, as opposed to regular QUIC packets. Multiple encapsulated
// sessions can coexist inside a MASQUE session.
class QUIC_NO_EXPORT MasqueEncapsulatedClientSession
    : public QuicSpdyClientSession,
      public MasqueClientSession::EncapsulatedClientSession {
 public:
  // Takes ownership of |connection|, but not of |crypto_config| or
  // |push_promise_index| or |masque_client_session|. All pointers must be
  // non-null. Caller must ensure that |push_promise_index| and
  // |masque_client_session| stay valid for the lifetime of the newly created
  // MasqueEncapsulatedClientSession.
  MasqueEncapsulatedClientSession(
      const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection,
      const QuicServerId& server_id,
      QuicCryptoClientConfig* crypto_config,
      QuicClientPushPromiseIndex* push_promise_index,
      MasqueClientSession* masque_client_session);

  // Disallow copy and assign.
  MasqueEncapsulatedClientSession(const MasqueEncapsulatedClientSession&) =
      delete;
  MasqueEncapsulatedClientSession& operator=(
      const MasqueEncapsulatedClientSession&) = delete;

  // From MasqueClientSession::EncapsulatedClientSession.
  void ProcessPacket(quiche::QuicheStringPiece packet,
                     QuicSocketAddress server_address) override;

  // From QuicSession.
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;

 private:
  MasqueClientSession* masque_client_session_;  // Unowned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_SESSION_H_
