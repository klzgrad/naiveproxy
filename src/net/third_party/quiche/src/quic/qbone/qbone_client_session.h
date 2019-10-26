// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_CLIENT_SESSION_H_
#define QUICHE_QUIC_QBONE_QBONE_CLIENT_SESSION_H_

#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_control.pb.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_control_stream.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_writer.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_session_base.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QboneClientSession
    : public QboneSessionBase,
      public QuicCryptoClientStream::ProofHandler {
 public:
  QboneClientSession(QuicConnection* connection,
                     QuicCryptoClientConfig* quic_crypto_client_config,
                     QuicSession::Visitor* owner,
                     const QuicConfig& config,
                     const ParsedQuicVersionVector& supported_versions,
                     const QuicServerId& server_id,
                     QbonePacketWriter* writer,
                     QboneClientControlStream::Handler* handler);
  QboneClientSession(const QboneClientSession&) = delete;
  QboneClientSession& operator=(const QboneClientSession&) = delete;
  ~QboneClientSession() override;

  // QuicSession overrides. This will initiate the crypto stream.
  void Initialize() override;

  // Returns the number of client hello messages that have been sent on the
  // crypto stream. If the handshake has completed then this is one greater
  // than the number of round-trips needed for the handshake.
  int GetNumSentClientHellos() const;
  int GetNumReceivedServerConfigUpdates() const;

  bool SendServerRequest(const QboneServerRequest& request);

  void ProcessPacketFromNetwork(QuicStringPiece packet) override;
  void ProcessPacketFromPeer(QuicStringPiece packet) override;

  // Returns true if there are active requests on this session.
  bool HasActiveRequests() const;

 protected:
  // QboneSessionBase interface implementation.
  std::unique_ptr<QuicCryptoStream> CreateCryptoStream() override;

  // ProofHandler interface implementation.
  void OnProofValid(const QuicCryptoClientConfig::CachedState& cached) override;
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) override;

  QuicServerId server_id() { return server_id_; }
  QuicCryptoClientConfig* crypto_client_config() {
    return quic_crypto_client_config_;
  }

 private:
  QuicServerId server_id_;
  // Config for QUIC crypto client stream, used by the client.
  QuicCryptoClientConfig* quic_crypto_client_config_;
  // Passed to the control stream.
  QboneClientControlStream::Handler* handler_;
  // The unowned control stream.
  QboneClientControlStream* control_stream_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_CLIENT_SESSION_H_
