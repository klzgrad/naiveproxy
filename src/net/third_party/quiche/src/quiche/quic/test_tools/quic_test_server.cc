// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_test_server.h"

#include <memory>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/tools/quic_simple_crypto_server_stream_helper.h"
#include "quiche/quic/tools/quic_simple_dispatcher.h"
#include "quiche/quic/tools/quic_simple_server_session.h"

namespace quic {

namespace test {

class CustomStreamSession : public QuicSimpleServerSession {
 public:
  CustomStreamSession(
      const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection, QuicSession::Visitor* visitor,
      QuicCryptoServerStreamBase::Helper* helper,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      QuicTestServer::StreamFactory* stream_factory,
      QuicTestServer::CryptoStreamFactory* crypto_stream_factory,
      QuicSimpleServerBackend* quic_simple_server_backend)
      : QuicSimpleServerSession(config, supported_versions, connection, visitor,
                                helper, crypto_config, compressed_certs_cache,
                                quic_simple_server_backend),
        stream_factory_(stream_factory),
        crypto_stream_factory_(crypto_stream_factory) {}

  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override {
    if (!ShouldCreateIncomingStream(id)) {
      return nullptr;
    }
    if (stream_factory_) {
      QuicSpdyStream* stream =
          stream_factory_->CreateStream(id, this, server_backend());
      ActivateStream(absl::WrapUnique(stream));
      return stream;
    }
    return QuicSimpleServerSession::CreateIncomingStream(id);
  }

  std::unique_ptr<QuicCryptoServerStreamBase> CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override {
    if (crypto_stream_factory_) {
      return crypto_stream_factory_->CreateCryptoStream(crypto_config, this);
    }
    return QuicSimpleServerSession::CreateQuicCryptoServerStream(
        crypto_config, compressed_certs_cache);
  }

 private:
  QuicTestServer::StreamFactory* stream_factory_;               // Not owned.
  QuicTestServer::CryptoStreamFactory* crypto_stream_factory_;  // Not owned.
};

class QuicTestDispatcher : public QuicSimpleDispatcher {
 public:
  QuicTestDispatcher(
      const QuicConfig* config, const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      QuicSimpleServerBackend* quic_simple_server_backend,
      uint8_t expected_server_connection_id_length,
      ConnectionIdGeneratorInterface& generator)
      : QuicSimpleDispatcher(config, crypto_config, version_manager,
                             std::move(helper), std::move(session_helper),
                             std::move(alarm_factory),
                             quic_simple_server_backend,
                             expected_server_connection_id_length, generator),
        session_factory_(nullptr),
        stream_factory_(nullptr),
        crypto_stream_factory_(nullptr) {}

  std::unique_ptr<QuicSession> CreateQuicSession(
      QuicConnectionId id, const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, absl::string_view alpn,
      const ParsedQuicVersion& version,
      const ParsedClientHello& /*parsed_chlo*/,
      ConnectionIdGeneratorInterface& connection_id_generator) override {
    QuicReaderMutexLock lock(&factory_lock_);
    // The QuicServerSessionBase takes ownership of |connection| below.
    QuicConnection* connection = new QuicConnection(
        id, self_address, peer_address, helper(), alarm_factory(), writer(),
        /* owns_writer= */ false, Perspective::IS_SERVER,
        ParsedQuicVersionVector{version}, connection_id_generator);

    std::unique_ptr<QuicServerSessionBase> session;
    if (session_factory_ == nullptr && stream_factory_ == nullptr &&
        crypto_stream_factory_ == nullptr) {
      session = std::make_unique<QuicSimpleServerSession>(
          config(), GetSupportedVersions(), connection, this, session_helper(),
          crypto_config(), compressed_certs_cache(), server_backend());
    } else if (stream_factory_ != nullptr ||
               crypto_stream_factory_ != nullptr) {
      session = std::make_unique<CustomStreamSession>(
          config(), GetSupportedVersions(), connection, this, session_helper(),
          crypto_config(), compressed_certs_cache(), stream_factory_,
          crypto_stream_factory_, server_backend());
    } else {
      session = session_factory_->CreateSession(
          config(), connection, this, session_helper(), crypto_config(),
          compressed_certs_cache(), server_backend(), alpn);
    }
    if (VersionUsesHttp3(version.transport_version)) {
      QUICHE_DCHECK(session->allow_extended_connect());
      // Do not allow extended CONNECT request if the backend doesn't support
      // it.
      session->set_allow_extended_connect(
          server_backend()->SupportsExtendedConnect());
    }
    session->Initialize();
    return session;
  }

  void SetSessionFactory(QuicTestServer::SessionFactory* factory) {
    QuicWriterMutexLock lock(&factory_lock_);
    QUICHE_DCHECK(session_factory_ == nullptr);
    QUICHE_DCHECK(stream_factory_ == nullptr);
    QUICHE_DCHECK(crypto_stream_factory_ == nullptr);
    session_factory_ = factory;
  }

  void SetStreamFactory(QuicTestServer::StreamFactory* factory) {
    QuicWriterMutexLock lock(&factory_lock_);
    QUICHE_DCHECK(session_factory_ == nullptr);
    QUICHE_DCHECK(stream_factory_ == nullptr);
    stream_factory_ = factory;
  }

  void SetCryptoStreamFactory(QuicTestServer::CryptoStreamFactory* factory) {
    QuicWriterMutexLock lock(&factory_lock_);
    QUICHE_DCHECK(session_factory_ == nullptr);
    QUICHE_DCHECK(crypto_stream_factory_ == nullptr);
    crypto_stream_factory_ = factory;
  }

 private:
  QuicMutex factory_lock_;
  QuicTestServer::SessionFactory* session_factory_;             // Not owned.
  QuicTestServer::StreamFactory* stream_factory_;               // Not owned.
  QuicTestServer::CryptoStreamFactory* crypto_stream_factory_;  // Not owned.
};

QuicTestServer::QuicTestServer(
    std::unique_ptr<ProofSource> proof_source,
    QuicSimpleServerBackend* quic_simple_server_backend)
    : QuicServer(std::move(proof_source), quic_simple_server_backend) {}

QuicTestServer::QuicTestServer(
    std::unique_ptr<ProofSource> proof_source, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicSimpleServerBackend* quic_simple_server_backend)
    : QuicTestServer(std::move(proof_source), config, supported_versions,
                     quic_simple_server_backend,
                     kQuicDefaultConnectionIdLength) {}

QuicTestServer::QuicTestServer(
    std::unique_ptr<ProofSource> proof_source, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicSimpleServerBackend* quic_simple_server_backend,
    uint8_t expected_server_connection_id_length)
    : QuicServer(std::move(proof_source), config,
                 QuicCryptoServerConfig::ConfigOptions(), supported_versions,
                 quic_simple_server_backend,
                 expected_server_connection_id_length) {}

QuicDispatcher* QuicTestServer::CreateQuicDispatcher() {
  return new QuicTestDispatcher(
      &config(), &crypto_config(), version_manager(),
      std::make_unique<QuicDefaultConnectionHelper>(),
      std::unique_ptr<QuicCryptoServerStreamBase::Helper>(
          new QuicSimpleCryptoServerStreamHelper()),
      event_loop()->CreateAlarmFactory(), server_backend(),
      expected_server_connection_id_length(), connection_id_generator());
}

void QuicTestServer::SetSessionFactory(SessionFactory* factory) {
  QUICHE_DCHECK(dispatcher());
  static_cast<QuicTestDispatcher*>(dispatcher())->SetSessionFactory(factory);
}

void QuicTestServer::SetSpdyStreamFactory(StreamFactory* factory) {
  static_cast<QuicTestDispatcher*>(dispatcher())->SetStreamFactory(factory);
}

void QuicTestServer::SetCryptoStreamFactory(CryptoStreamFactory* factory) {
  static_cast<QuicTestDispatcher*>(dispatcher())
      ->SetCryptoStreamFactory(factory);
}

void QuicTestServer::SetEventLoopFactory(QuicEventLoopFactory* factory) {
  event_loop_factory_ = factory;
}

std::unique_ptr<QuicEventLoop> QuicTestServer::CreateEventLoop() {
  QuicEventLoopFactory* factory = event_loop_factory_;
  if (factory == nullptr) {
    factory = GetDefaultEventLoop();
  }
  return factory->Create(QuicDefaultClock::Get());
}

///////////////////////////   TEST SESSIONS ///////////////////////////////

ImmediateGoAwaySession::ImmediateGoAwaySession(
    const QuicConfig& config, QuicConnection* connection,
    QuicSession::Visitor* visitor, QuicCryptoServerStreamBase::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    QuicSimpleServerBackend* quic_simple_server_backend)
    : QuicSimpleServerSession(
          config, CurrentSupportedVersions(), connection, visitor, helper,
          crypto_config, compressed_certs_cache, quic_simple_server_backend) {}

void ImmediateGoAwaySession::OnStreamFrame(const QuicStreamFrame& frame) {
  if (VersionUsesHttp3(transport_version())) {
    SendHttp3GoAway(QUIC_PEER_GOING_AWAY, "");
  } else {
    SendGoAway(QUIC_PEER_GOING_AWAY, "");
  }
  QuicSimpleServerSession::OnStreamFrame(frame);
}

void ImmediateGoAwaySession::OnCryptoFrame(const QuicCryptoFrame& frame) {
  // In IETF QUIC, GOAWAY lives up in HTTP/3 layer. It's sent in a QUIC stream
  // and requires encryption. Thus the sending is done in
  // OnNewEncryptionKeyAvailable().
  if (!VersionUsesHttp3(transport_version())) {
    SendGoAway(QUIC_PEER_GOING_AWAY, "");
  }
  QuicSimpleServerSession::OnCryptoFrame(frame);
}

void ImmediateGoAwaySession::OnNewEncryptionKeyAvailable(
    EncryptionLevel level, std::unique_ptr<QuicEncrypter> encrypter) {
  QuicSimpleServerSession::OnNewEncryptionKeyAvailable(level,
                                                       std::move(encrypter));
  if (VersionUsesHttp3(transport_version())) {
    if (IsEncryptionEstablished() && !goaway_sent()) {
      SendHttp3GoAway(QUIC_PEER_GOING_AWAY, "");
    }
  }
}

}  // namespace test

}  // namespace quic
