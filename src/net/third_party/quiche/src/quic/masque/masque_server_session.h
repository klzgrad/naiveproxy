// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_

#include "net/third_party/quiche/src/quic/masque/masque_compression_engine.h"
#include "net/third_party/quiche/src/quic/masque/masque_server_backend.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_session.h"

namespace quic {

// QUIC server session for connection to MASQUE proxy.
class QUIC_NO_EXPORT MasqueServerSession
    : public QuicSimpleServerSession,
      public MasqueServerBackend::BackendClient {
 public:
  // Interface meant to be implemented by owner of this MasqueServerSession
  // instance.
  class QUIC_NO_EXPORT Visitor {
   public:
    virtual ~Visitor() {}
    // Register a client connection ID as being handled by this session.
    virtual void RegisterClientConnectionId(
        QuicConnectionId client_connection_id,
        MasqueServerSession* masque_server_session) = 0;

    // Unregister a client connection ID.
    virtual void UnregisterClientConnectionId(
        QuicConnectionId client_connection_id) = 0;
  };

  explicit MasqueServerSession(
      const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection,
      QuicSession::Visitor* visitor,
      Visitor* owner,
      QuicCryptoServerStreamBase::Helper* helper,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      MasqueServerBackend* masque_server_backend);

  // Disallow copy and assign.
  MasqueServerSession(const MasqueServerSession&) = delete;
  MasqueServerSession& operator=(const MasqueServerSession&) = delete;

  // From QuicSession.
  void OnMessageReceived(quiche::QuicheStringPiece message) override;
  void OnMessageAcked(QuicMessageId message_id,
                      QuicTime receive_timestamp) override;
  void OnMessageLost(QuicMessageId message_id) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;

  // From MasqueServerBackend::BackendClient.
  std::unique_ptr<QuicBackendResponse> HandleMasqueRequest(
      const std::string& masque_path,
      const spdy::SpdyHeaderBlock& request_headers,
      const std::string& request_body,
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

  // Handle packet for client, meant to be called by MasqueDispatcher.
  void HandlePacketFromServer(const ReceivedPacketInfo& packet_info);

 private:
  MasqueServerBackend* masque_server_backend_;  // Unowned.
  Visitor* owner_;                              // Unowned.
  MasqueCompressionEngine compression_engine_;
  bool masque_initialized_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_SESSION_H_
