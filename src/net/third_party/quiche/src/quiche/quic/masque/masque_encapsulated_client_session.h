// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_SESSION_H_

#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// QUIC client session for QUIC encapsulated in MASQUE. This client session is
// maintained end-to-end between the client and the web-server (the MASQUE
// session does not have access to the cryptographic keys for the end-to-end
// session), but its packets are sent encapsulated inside DATAGRAM frames in a
// MASQUE session, as opposed to regular QUIC packets. Multiple encapsulated
// sessions can coexist inside a MASQUE session.
class QUIC_NO_EXPORT MasqueEncapsulatedClientSession
    : public QuicSpdyClientSession,
      public MasqueClientSession::EncapsulatedClientSession,
      public MasqueClientSession::EncapsulatedIpSession {
 public:
  // Takes ownership of |connection|, but not of |crypto_config| or
  // |masque_client_session|. All pointers must be non-null. Caller must ensure
  // that |masque_client_session| stays valid for the lifetime of the newly
  // created MasqueEncapsulatedClientSession.
  MasqueEncapsulatedClientSession(
      const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection, const QuicServerId& server_id,
      QuicCryptoClientConfig* crypto_config,
      MasqueClientSession* masque_client_session);

  // Disallow copy and assign.
  MasqueEncapsulatedClientSession(const MasqueEncapsulatedClientSession&) =
      delete;
  MasqueEncapsulatedClientSession& operator=(
      const MasqueEncapsulatedClientSession&) = delete;

  // From MasqueClientSession::EncapsulatedClientSession.
  void ProcessPacket(absl::string_view packet,
                     QuicSocketAddress server_address) override;
  void CloseConnection(
      QuicErrorCode error, const std::string& details,
      ConnectionCloseBehavior connection_close_behavior) override;

  // From MasqueClientSession::EncapsulatedIpSession.
  void ProcessIpPacket(absl::string_view packet) override;
  void CloseIpSession(const std::string& details) override;
  bool OnAddressAssignCapsule(
      const quiche::AddressAssignCapsule& capsule) override;
  bool OnAddressRequestCapsule(
      const quiche::AddressRequestCapsule& capsule) override;
  bool OnRouteAdvertisementCapsule(
      const quiche::RouteAdvertisementCapsule& capsule) override;

  // From QuicSession.
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;

  // For CONNECT-IP.
  QuicIpAddress local_v4_address() const { return local_v4_address_; }
  QuicIpAddress local_v6_address() const { return local_v6_address_; }

 private:
  MasqueClientSession* masque_client_session_;  // Unowned.
  // For CONNECT-IP.
  QuicIpAddress local_v4_address_;
  QuicIpAddress local_v6_address_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_SESSION_H_
