// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_client.h"

#include <utility>


#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/platform/api/quic_testvalue.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"

namespace quic {
namespace {
std::unique_ptr<QuicClientBase::NetworkHelper> CreateNetworkHelper(
    QuicEventLoop* event_loop, QboneClient* client) {
  std::unique_ptr<QuicClientBase::NetworkHelper> helper =
      std::make_unique<QuicClientDefaultNetworkHelper>(event_loop, client);
  quic::AdjustTestValue("QboneClient/network_helper", &helper);
  return helper;
}
}  // namespace

QboneClient::QboneClient(QuicSocketAddress server_address,
                         const QuicServerId& server_id,
                         const ParsedQuicVersionVector& supported_versions,
                         QuicSession::Visitor* session_owner,
                         const QuicConfig& config, QuicEventLoop* event_loop,
                         std::unique_ptr<ProofVerifier> proof_verifier,
                         QbonePacketWriter* qbone_writer,
                         QboneClientControlStream::Handler* qbone_handler)
    : QuicClientBase(server_id, supported_versions, config,
                     new QuicDefaultConnectionHelper(),
                     event_loop->CreateAlarmFactory().release(),
                     CreateNetworkHelper(event_loop, this),
                     std::move(proof_verifier), nullptr),
      qbone_writer_(qbone_writer),
      qbone_handler_(qbone_handler),
      session_owner_(session_owner),
      max_pacing_rate_(QuicBandwidth::Zero()) {
  set_server_address(server_address);
  crypto_config()->set_alpn("qbone");
}

QboneClient::~QboneClient() { ResetSession(); }

QboneClientSession* QboneClient::qbone_session() {
  return static_cast<QboneClientSession*>(QuicClientBase::session());
}

void QboneClient::ProcessPacketFromNetwork(absl::string_view packet) {
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
  if (max_pacing_rate() > quic::QuicBandwidth::Zero()) {
    QUIC_LOG(INFO) << "Setting max pacing rate to " << max_pacing_rate();
    connection->SetMaxPacingRate(max_pacing_rate());
  }
  return std::make_unique<QboneClientSessionWithConnection>(
      connection, crypto_config(), session_owner(), *config(),
      supported_versions, server_id(), qbone_writer_, qbone_handler_);
}

bool QboneClient::use_quarantine_mode() const { return use_quarantine_mode_; }
void QboneClient::set_use_quarantine_mode(bool use_quarantine_mode) {
  use_quarantine_mode_ = use_quarantine_mode;
}
}  // namespace quic
