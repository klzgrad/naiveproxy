// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_client.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_epoll_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_epoll_connection_helper.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_exported_stats.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_stream.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace {
std::unique_ptr<QuicClientBase::NetworkHelper> CreateNetworkHelper(
    QuicEpollServer* epoll_server,
    QboneClient* client) {
  std::unique_ptr<QuicClientBase::NetworkHelper> helper =
      std::make_unique<QuicClientEpollNetworkHelper>(epoll_server, client);
  testing::testvalue::Adjust("QboneClient/network_helper", &helper);
  return helper;
}
}  // namespace

QboneClient::QboneClient(QuicSocketAddress server_address,
                         const QuicServerId& server_id,
                         const ParsedQuicVersionVector& supported_versions,
                         QuicSession::Visitor* session_owner,
                         const QuicConfig& config,
                         QuicEpollServer* epoll_server,
                         std::unique_ptr<ProofVerifier> proof_verifier,
                         QbonePacketWriter* qbone_writer,
                         QboneClientControlStream::Handler* qbone_handler)
    : QuicClientBase(
          server_id,
          supported_versions,
          config,
          new QuicEpollConnectionHelper(epoll_server, QuicAllocator::SIMPLE),
          new QuicEpollAlarmFactory(epoll_server),
          CreateNetworkHelper(epoll_server, this),
          std::move(proof_verifier),
          nullptr),
      qbone_writer_(qbone_writer),
      qbone_handler_(qbone_handler),
      session_owner_(session_owner) {
  set_server_address(server_address);
  crypto_config()->set_alpn("qbone");
}

QboneClient::~QboneClient() {
  ResetSession();
}

QboneClientSession* QboneClient::qbone_session() {
  return static_cast<QboneClientSession*>(QuicClientBase::session());
}

void QboneClient::ProcessPacketFromNetwork(quiche::QuicheStringPiece packet) {
  qbone_session()->ProcessPacketFromNetwork(packet);
}

bool QboneClient::EarlyDataAccepted() {
  return qbone_session()->EarlyDataAccepted();
}

bool QboneClient::ReceivedInchoateReject() {
  return qbone_session()->ReceivedInchoateReject();
}

int QboneClient::GetNumSentClientHellosFromSession() {
  return qbone_session()->GetNumSentClientHellos();
}

int QboneClient::GetNumReceivedServerConfigUpdatesFromSession() {
  return qbone_session()->GetNumReceivedServerConfigUpdates();
}

void QboneClient::ResendSavedData() {
  // no op.
}

void QboneClient::ClearDataToResend() {
  // no op.
}

bool QboneClient::HasActiveRequests() {
  return qbone_session()->HasActiveRequests();
}

class QboneClientSessionWithConnection : public QboneClientSession {
 public:
  using QboneClientSession::QboneClientSession;

  ~QboneClientSessionWithConnection() override { DeleteConnection(); }
};

// Takes ownership of |connection|.
std::unique_ptr<QuicSession> QboneClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  return std::make_unique<QboneClientSessionWithConnection>(
      connection, crypto_config(), session_owner(), *config(),
      supported_versions, server_id(), qbone_writer_, qbone_handler_);
}

}  // namespace quic
