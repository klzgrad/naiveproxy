// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_CLIENT_H_
#define QUICHE_QUIC_QBONE_QBONE_CLIENT_H_

#include "net/third_party/quiche/src/quic/qbone/qbone_client_interface.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_client_session.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_writer.h"
#include "net/third_party/quiche/src/quic/tools/quic_client_base.h"
#include "net/third_party/quiche/src/quic/tools/quic_client_epoll_network_helper.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
// A QboneClient encapsulates connecting to a server via an epoll server
// and setting up a QBONE tunnel. See the QboneTestClient in qbone_client_test
// for usage.
class QboneClient : public QuicClientBase, public QboneClientInterface {
 public:
  // Note that the epoll server, QBONE writer, and handler are owned
  // by the caller.
  QboneClient(QuicSocketAddress server_address,
              const QuicServerId& server_id,
              const ParsedQuicVersionVector& supported_versions,
              QuicSession::Visitor* session_owner,
              const QuicConfig& config,
              QuicEpollServer* epoll_server,
              std::unique_ptr<ProofVerifier> proof_verifier,
              QbonePacketWriter* qbone_writer,
              QboneClientControlStream::Handler* qbone_handler);
  ~QboneClient() override;
  QboneClientSession* qbone_session();

  // From QboneClientInterface. Accepts a given packet from the network and
  // sends the packet down to the QBONE connection.
  void ProcessPacketFromNetwork(quiche::QuicheStringPiece packet) override;

  bool EarlyDataAccepted() override;
  bool ReceivedInchoateReject() override;

 protected:
  int GetNumSentClientHellosFromSession() override;
  int GetNumReceivedServerConfigUpdatesFromSession() override;

  // This client does not resend saved data. This will be a no-op.
  void ResendSavedData() override;

  // This client does not resend saved data. This will be a no-op.
  void ClearDataToResend() override;

  // Takes ownership of |connection|.
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  QbonePacketWriter* qbone_writer() { return qbone_writer_; }

  QboneClientControlStream::Handler* qbone_control_handler() {
    return qbone_handler_;
  }

  QuicSession::Visitor* session_owner() {
    return session_owner_;
  }

  bool HasActiveRequests() override;

 private:
  QbonePacketWriter* qbone_writer_;
  QboneClientControlStream::Handler* qbone_handler_;

  QuicSession::Visitor* session_owner_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_CLIENT_H_
