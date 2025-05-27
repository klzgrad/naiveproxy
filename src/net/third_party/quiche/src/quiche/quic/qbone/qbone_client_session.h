// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_CLIENT_SESSION_H_
#define QUICHE_QUIC_QBONE_QBONE_CLIENT_SESSION_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_crypto_client_stream.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/qbone/qbone_control.pb.h"
#include "quiche/quic/qbone/qbone_control_stream.h"
#include "quiche/quic/qbone/qbone_packet_writer.h"
#include "quiche/quic/qbone/qbone_session_base.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QboneClientSession
    : public QboneSessionBase,
      public QuicCryptoClientStream::ProofHandler {
 public:
  QboneClientSession(QuicConnection* connection,
                     QuicCryptoClientConfig* quic_crypto_client_config,
                     QuicSession::Visitor* owner, const QuicConfig& config,
                     const ParsedQuicVersionVector& supported_versions,
                     const QuicServerId& server_id, QbonePacketWriter* writer,
                     QboneClientControlStream::Handler* handler);
  QboneClientSession(const QboneClientSession&) = delete;
  QboneClientSession& operator=(const QboneClientSession&) = delete;
  ~QboneClientSession() override;

  // QuicSession overrides. This will initiate the crypto stream.
  void Initialize() override;
  // Override to create control stream at FORWARD_SECURE encryption level.
  void SetDefaultEncryptionLevel(quic::EncryptionLevel level) override;

  // Returns the number of client hello messages that have been sent on the
  // crypto stream. If the handshake has completed then this is one greater
  // than the number of round-trips needed for the handshake.
  int GetNumSentClientHellos() const;

  // Returns true if early data (0-RTT data) was sent and the server accepted
  // it.
  bool EarlyDataAccepted() const;

  // Returns true if the handshake was delayed one round trip by the server
  // because the server wanted proof the client controls its source address
  // before progressing further. In Google QUIC, this would be due to an
  // inchoate REJ in the QUIC Crypto handshake; in IETF QUIC this would be due
  // to a Retry packet.
  // TODO(nharper): Consider a better name for this method.
  bool ReceivedInchoateReject() const;

  int GetNumReceivedServerConfigUpdates() const;

  bool SendServerRequest(const QboneServerRequest& request);

  void ProcessPacketFromNetwork(absl::string_view packet) override;
  void ProcessPacketFromPeer(absl::string_view packet) override;

  // Returns true if there are active requests on this session.
  bool HasActiveRequests() const;

 protected:
  // QboneSessionBase interface implementation.
  std::unique_ptr<QuicCryptoStream> CreateCryptoStream() override;

  // Instantiate QboneClientControlStream.
  void CreateControlStream();

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
  QboneClientControlStream* control_stream_ = nullptr;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_CLIENT_SESSION_H_
