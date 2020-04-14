// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quiche/src/quic/masque/masque_compression_engine.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// QUIC client session for connection to MASQUE proxy. This session establishes
// a connection to a MASQUE proxy and handles sending and receiving DATAGRAM
// frames for operation of the MASQUE protocol. Multiple end-to-end encapsulated
// sessions can then coexist inside this session. Once these are created, they
// need to be registered with this session.
class QUIC_NO_EXPORT MasqueClientSession : public QuicSpdyClientSession {
 public:
  // Interface meant to be implemented by the owner of the
  // MasqueClientSession instance.
  class QUIC_NO_EXPORT Owner {
   public:
    virtual ~Owner() {}

    // Notifies the owner that the client connection ID is no longer in use.
    virtual void UnregisterClientConnectionId(
        QuicConnectionId client_connection_id) = 0;
  };
  // Interface meant to be implemented by encapsulated client sessions, i.e.
  // the end-to-end QUIC client sessions that run inside MASQUE encapsulation.
  class QUIC_NO_EXPORT EncapsulatedClientSession {
   public:
    virtual ~EncapsulatedClientSession() {}

    // Process packet that was just decapsulated.
    virtual void ProcessPacket(quiche::QuicheStringPiece packet,
                               QuicSocketAddress server_address) = 0;
  };

  // Takes ownership of |connection|, but not of |crypto_config| or
  // |push_promise_index| or |owner|. All pointers must be non-null. Caller
  // must ensure that |push_promise_index| and |owner| stay valid for the
  // lifetime of the newly created MasqueClientSession.
  MasqueClientSession(const QuicConfig& config,
                      const ParsedQuicVersionVector& supported_versions,
                      QuicConnection* connection,
                      const QuicServerId& server_id,
                      QuicCryptoClientConfig* crypto_config,
                      QuicClientPushPromiseIndex* push_promise_index,
                      Owner* owner);

  // Disallow copy and assign.
  MasqueClientSession(const MasqueClientSession&) = delete;
  MasqueClientSession& operator=(const MasqueClientSession&) = delete;

  // From QuicSession.
  void OnMessageReceived(quiche::QuicheStringPiece message) override;

  void OnMessageAcked(QuicMessageId message_id,
                      QuicTime receive_timestamp) override;

  void OnMessageLost(QuicMessageId message_id) override;

  // Send encapsulated packet.
  void SendPacket(QuicConnectionId client_connection_id,
                  QuicConnectionId server_connection_id,
                  quiche::QuicheStringPiece packet,
                  const QuicSocketAddress& server_address);

  // Register encapsulated client. This allows clients that are encapsulated
  // within this MASQUE session to indicate they own a given client connection
  // ID so incoming packets with that connection ID are routed back to them.
  // Callers must not register a second different |encapsulated_client_session|
  // with the same |client_connection_id|. Every call must be matched with a
  // call to UnregisterConnectionId.
  void RegisterConnectionId(
      QuicConnectionId client_connection_id,
      EncapsulatedClientSession* encapsulated_client_session);

  // Unregister encapsulated client. |client_connection_id| must match a
  // value previously passed to RegisterConnectionId.
  void UnregisterConnectionId(QuicConnectionId client_connection_id);

 private:
  QuicUnorderedMap<QuicConnectionId,
                   EncapsulatedClientSession*,
                   QuicConnectionIdHash>
      client_connection_id_registrations_;
  Owner* owner_;  // Unowned;
  MasqueCompressionEngine compression_engine_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_SESSION_H_
