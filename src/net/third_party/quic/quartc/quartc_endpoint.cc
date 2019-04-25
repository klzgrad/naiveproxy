// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_endpoint.h"

#include "net/third_party/quic/core/quic_version_manager.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/quartc/quartc_connection_helper.h"
#include "net/third_party/quic/quartc/quartc_crypto_helpers.h"
#include "net/third_party/quic/quartc/quartc_dispatcher.h"

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

QuartcFactoryConfig CreateFactoryConfig(QuicAlarmFactory* alarm_factory,
                                        const QuicClock* clock) {
  QuartcFactoryConfig config;
  config.alarm_factory = alarm_factory;
  config.clock = clock;
  return config;
}

}  // namespace

QuartcClientEndpoint::QuartcClientEndpoint(
    QuicAlarmFactory* alarm_factory,
    const QuicClock* clock,
    QuartcEndpoint::Delegate* delegate,
    const QuartcSessionConfig& config,
    QuicStringPiece serialized_server_config,
    std::unique_ptr<QuicVersionManager> version_manager)
    : alarm_factory_(alarm_factory),
      clock_(clock),
      delegate_(delegate),
      serialized_server_config_(serialized_server_config),
      version_manager_(version_manager ? std::move(version_manager)
                                       : QuicMakeUnique<QuicVersionManager>(
                                             AllSupportedVersions())),
      create_session_alarm_(QuicWrapUnique(
          alarm_factory_->CreateAlarm(new CreateSessionDelegate(this)))),
      factory_(QuicMakeUnique<QuartcFactory>(
          CreateFactoryConfig(alarm_factory, clock))),
      config_(config) {}

void QuartcClientEndpoint::Connect(QuartcPacketTransport* packet_transport) {
  packet_transport_ = packet_transport;
  create_session_alarm_->Set(clock_->Now());
}

void QuartcClientEndpoint::OnCreateSessionAlarm() {
  session_ = factory_->CreateQuartcClientSession(
      config_, version_manager_->GetSupportedVersions(),
      serialized_server_config_, packet_transport_);
  delegate_->OnSessionCreated(session_.get());
}

QuartcServerEndpoint::QuartcServerEndpoint(
    QuicAlarmFactory* alarm_factory,
    const QuicClock* clock,
    QuartcEndpoint::Delegate* delegate,
    const QuartcSessionConfig& config,
    std::unique_ptr<QuicVersionManager> version_manager)
    : alarm_factory_(alarm_factory),
      delegate_(delegate),
      config_(config),
      version_manager_(version_manager ? std::move(version_manager)
                                       : QuicMakeUnique<QuicVersionManager>(
                                             AllSupportedVersions())),
      pre_connection_helper_(QuicMakeUnique<QuartcConnectionHelper>(clock)),
      crypto_config_(
          CreateCryptoServerConfig(pre_connection_helper_->GetRandomGenerator(),
                                   clock,
                                   config.pre_shared_key)) {}

void QuartcServerEndpoint::Connect(QuartcPacketTransport* packet_transport) {
  DCHECK(pre_connection_helper_ != nullptr);
  dispatcher_ = QuicMakeUnique<QuartcDispatcher>(
      QuicMakeUnique<QuicConfig>(CreateQuicConfig(config_)),
      std::move(crypto_config_.config), crypto_config_.serialized_crypto_config,
      version_manager_.get(), std::move(pre_connection_helper_),
      QuicMakeUnique<QuartcCryptoServerStreamHelper>(),
      QuicMakeUnique<QuartcAlarmFactoryWrapper>(alarm_factory_),
      QuicMakeUnique<QuartcPacketWriter>(packet_transport,
                                         config_.max_packet_size),
      this);
  // The dispatcher requires at least one call to |ProcessBufferedChlos| to
  // set the number of connections it is allowed to create.
  dispatcher_->ProcessBufferedChlos(/*max_connections_to_create=*/1);
}

void QuartcServerEndpoint::OnSessionCreated(QuartcSession* session) {
  delegate_->OnSessionCreated(session);
}

}  // namespace quic
