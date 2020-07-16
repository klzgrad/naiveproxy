// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_version_manager.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_connection_helper.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_crypto_helpers.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_dispatcher.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

// Wrapper around a QuicAlarmFactory which delegates to the wrapped factory.
// Usee to convert an unowned pointer into an owned pointer, so that the new
// "owner" does not delete the underlying factory.  Note that this is only valid
// when the unowned pointer is already guaranteed to outlive the new "owner".
class QuartcAlarmFactoryWrapper : public QuicAlarmFactory {
 public:
  explicit QuartcAlarmFactoryWrapper(QuicAlarmFactory* impl) : impl_(impl) {}

  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

 private:
  QuicAlarmFactory* impl_;
};

QuicAlarm* QuartcAlarmFactoryWrapper::CreateAlarm(
    QuicAlarm::Delegate* delegate) {
  return impl_->CreateAlarm(delegate);
}

QuicArenaScopedPtr<QuicAlarm> QuartcAlarmFactoryWrapper::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  return impl_->CreateAlarm(std::move(delegate), arena);
}

}  // namespace

QuartcClientEndpoint::QuartcClientEndpoint(
    QuicAlarmFactory* alarm_factory,
    const QuicClock* clock,
    QuicRandom* random,
    QuartcEndpoint::Delegate* delegate,
    const QuartcSessionConfig& config,
    quiche::QuicheStringPiece serialized_server_config,
    std::unique_ptr<QuicVersionManager> version_manager)
    : alarm_factory_(alarm_factory),
      clock_(clock),
      delegate_(delegate),
      serialized_server_config_(serialized_server_config),
      version_manager_(version_manager ? std::move(version_manager)
                                       : std::make_unique<QuicVersionManager>(
                                             AllSupportedVersions())),
      create_session_alarm_(QuicWrapUnique(
          alarm_factory_->CreateAlarm(new CreateSessionDelegate(this)))),
      connection_helper_(
          std::make_unique<QuartcConnectionHelper>(clock_, random)),
      config_(config) {}

void QuartcClientEndpoint::Connect(QuartcPacketTransport* packet_transport) {
  packet_transport_ = packet_transport;
  // For the first attempt to connect, use any version that the client supports.
  current_versions_ = version_manager_->GetSupportedVersions();
  create_session_alarm_->Set(clock_->Now());
}

void QuartcClientEndpoint::OnCreateSessionAlarm() {
  session_ = CreateQuartcClientSession(
      config_, clock_, alarm_factory_, connection_helper_.get(),
      current_versions_, serialized_server_config_, packet_transport_);
  session_->SetDelegate(this);
  delegate_->OnSessionCreated(session_.get());
}

void QuartcClientEndpoint::OnCryptoHandshakeComplete() {
  delegate_->OnCryptoHandshakeComplete();
}

void QuartcClientEndpoint::OnConnectionWritable() {
  delegate_->OnConnectionWritable();
}

void QuartcClientEndpoint::OnIncomingStream(QuartcStream* stream) {
  delegate_->OnIncomingStream(stream);
}

void QuartcClientEndpoint::OnCongestionControlChange(
    QuicBandwidth bandwidth_estimate,
    QuicBandwidth pacing_rate,
    QuicTime::Delta latest_rtt) {
  delegate_->OnCongestionControlChange(bandwidth_estimate, pacing_rate,
                                       latest_rtt);
}

void QuartcClientEndpoint::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame,
    ConnectionCloseSource source) {
  // First, see if we can restart the session with a mutually-supported version.
  if (frame.quic_error_code == QUIC_INVALID_VERSION && session_ &&
      session_->connection() &&
      !session_->connection()->server_supported_versions().empty()) {
    for (const auto& client_version :
         version_manager_->GetSupportedVersions()) {
      if (QuicContainsValue(session_->connection()->server_supported_versions(),
                            client_version)) {
        // Found a mutually-supported version.  Reconnect using that version.
        current_versions_.clear();
        current_versions_.push_back(client_version);
        create_session_alarm_->Set(clock_->Now());
        return;
      }
    }
  }

  // Permanent version negotiation errors are forwarded to the |delegate_|,
  // along with all other errors.
  delegate_->OnConnectionClosed(frame, source);
}

void QuartcClientEndpoint::OnMessageReceived(
    quiche::QuicheStringPiece message) {
  delegate_->OnMessageReceived(message);
}

void QuartcClientEndpoint::OnMessageSent(int64_t datagram_id) {
  delegate_->OnMessageSent(datagram_id);
}

void QuartcClientEndpoint::OnMessageAcked(int64_t datagram_id,
                                          QuicTime receive_timestamp) {
  delegate_->OnMessageAcked(datagram_id, receive_timestamp);
}

void QuartcClientEndpoint::OnMessageLost(int64_t datagram_id) {
  delegate_->OnMessageLost(datagram_id);
}

QuartcServerEndpoint::QuartcServerEndpoint(
    QuicAlarmFactory* alarm_factory,
    const QuicClock* clock,
    QuicRandom* random,
    QuartcEndpoint::Delegate* delegate,
    const QuartcSessionConfig& config,
    std::unique_ptr<QuicVersionManager> version_manager)
    : alarm_factory_(alarm_factory),
      delegate_(delegate),
      config_(config),
      version_manager_(version_manager ? std::move(version_manager)
                                       : std::make_unique<QuicVersionManager>(
                                             AllSupportedVersions())),
      pre_connection_helper_(
          std::make_unique<QuartcConnectionHelper>(clock, random)),
      crypto_config_(
          CreateCryptoServerConfig(pre_connection_helper_->GetRandomGenerator(),
                                   clock,
                                   config.pre_shared_key)) {}

void QuartcServerEndpoint::Connect(QuartcPacketTransport* packet_transport) {
  DCHECK(pre_connection_helper_ != nullptr);
  dispatcher_ = std::make_unique<QuartcDispatcher>(
      std::make_unique<QuicConfig>(CreateQuicConfig(config_)),
      std::move(crypto_config_.config), version_manager_.get(),
      std::move(pre_connection_helper_),
      std::make_unique<QuartcCryptoServerStreamHelper>(),
      std::make_unique<QuartcAlarmFactoryWrapper>(alarm_factory_),
      std::make_unique<QuartcPacketWriter>(packet_transport,
                                           config_.max_packet_size),
      this);
  // The dispatcher requires at least one call to |ProcessBufferedChlos| to
  // set the number of connections it is allowed to create.
  dispatcher_->ProcessBufferedChlos(/*max_connections_to_create=*/1);
}

void QuartcServerEndpoint::OnSessionCreated(QuartcSession* session) {
  session->SetDelegate(delegate_);
  delegate_->OnSessionCreated(session);
}

}  // namespace quic
