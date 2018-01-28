// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/test_tools/quic_test_server.h"

#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/tools/quic/quic_epoll_alarm_factory.h"
#include "net/tools/quic/quic_epoll_connection_helper.h"
#include "net/tools/quic/quic_simple_crypto_server_stream_helper.h"
#include "net/tools/quic/quic_simple_dispatcher.h"
#include "net/tools/quic/quic_simple_server_session.h"

namespace net {

namespace test {

class CustomStreamSession : public QuicSimpleServerSession {
 public:
  CustomStreamSession(
      const QuicConfig& config,
      QuicConnection* connection,
      QuicSession::Visitor* visitor,
      QuicCryptoServerStream::Helper* helper,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      QuicTestServer::StreamFactory* stream_factory,
      QuicTestServer::CryptoStreamFactory* crypto_stream_factory,
      QuicHttpResponseCache* response_cache)
      : QuicSimpleServerSession(config,
                                connection,
                                visitor,
                                helper,
                                crypto_config,
                                compressed_certs_cache,
                                response_cache),
        stream_factory_(stream_factory),
        crypto_stream_factory_(crypto_stream_factory) {}

  QuicSpdyStream* CreateIncomingDynamicStream(QuicStreamId id) override {
    if (!ShouldCreateIncomingDynamicStream(id)) {
      return nullptr;
    }
    if (stream_factory_) {
      QuicSpdyStream* stream =
          stream_factory_->CreateStream(id, this, response_cache());
      ActivateStream(QuicWrapUnique(stream));
      return stream;
    }
    return QuicSimpleServerSession::CreateIncomingDynamicStream(id);
  }

  QuicCryptoServerStreamBase* CreateQuicCryptoServerStream(
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
      const QuicConfig& config,
      const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      QuicHttpResponseCache* response_cache)
      : QuicSimpleDispatcher(config,
                             crypto_config,
                             version_manager,
                             std::move(helper),
                             std::move(session_helper),
                             std::move(alarm_factory),
                             response_cache),
        session_factory_(nullptr),
        stream_factory_(nullptr),
        crypto_stream_factory_(nullptr) {}

  QuicServerSessionBase* CreateQuicSession(QuicConnectionId id,
                                           const QuicSocketAddress& client,
                                           QuicStringPiece alpn) override {
    QuicReaderMutexLock lock(&factory_lock_);
    if (session_factory_ == nullptr && stream_factory_ == nullptr &&
        crypto_stream_factory_ == nullptr) {
      return QuicSimpleDispatcher::CreateQuicSession(id, client, alpn);
    }
    QuicConnection* connection = new QuicConnection(
        id, client, helper(), alarm_factory(), CreatePerConnectionWriter(),
        /* owns_writer= */ true, Perspective::IS_SERVER,
        GetSupportedTransportVersions());

    QuicServerSessionBase* session = nullptr;
    if (stream_factory_ != nullptr || crypto_stream_factory_ != nullptr) {
      session = new CustomStreamSession(
          config(), connection, this, session_helper(), crypto_config(),
          compressed_certs_cache(), stream_factory_, crypto_stream_factory_,
          response_cache());
    } else {
      session = session_factory_->CreateSession(
          config(), connection, this, session_helper(), crypto_config(),
          compressed_certs_cache(), response_cache());
    }
    session->Initialize();
    return session;
  }

  void SetSessionFactory(QuicTestServer::SessionFactory* factory) {
    QuicWriterMutexLock lock(&factory_lock_);
    DCHECK(session_factory_ == nullptr);
    DCHECK(stream_factory_ == nullptr);
    DCHECK(crypto_stream_factory_ == nullptr);
    session_factory_ = factory;
  }

  void SetStreamFactory(QuicTestServer::StreamFactory* factory) {
    QuicWriterMutexLock lock(&factory_lock_);
    DCHECK(session_factory_ == nullptr);
    DCHECK(stream_factory_ == nullptr);
    stream_factory_ = factory;
  }

  void SetCryptoStreamFactory(QuicTestServer::CryptoStreamFactory* factory) {
    QuicWriterMutexLock lock(&factory_lock_);
    DCHECK(session_factory_ == nullptr);
    DCHECK(crypto_stream_factory_ == nullptr);
    crypto_stream_factory_ = factory;
  }

 private:
  QuicMutex factory_lock_;
  QuicTestServer::SessionFactory* session_factory_;             // Not owned.
  QuicTestServer::StreamFactory* stream_factory_;               // Not owned.
  QuicTestServer::CryptoStreamFactory* crypto_stream_factory_;  // Not owned.
};

QuicTestServer::QuicTestServer(std::unique_ptr<ProofSource> proof_source,
                               QuicHttpResponseCache* response_cache)
    : QuicServer(std::move(proof_source), response_cache) {}

QuicTestServer::QuicTestServer(
    std::unique_ptr<ProofSource> proof_source,
    const QuicConfig& config,
    const QuicTransportVersionVector& supported_versions,
    QuicHttpResponseCache* response_cache)
    : QuicServer(std::move(proof_source),
                 config,
                 QuicCryptoServerConfig::ConfigOptions(),
                 supported_versions,
                 response_cache) {}

QuicDispatcher* QuicTestServer::CreateQuicDispatcher() {
  return new QuicTestDispatcher(
      config(), &crypto_config(), version_manager(),
      std::unique_ptr<QuicEpollConnectionHelper>(new QuicEpollConnectionHelper(
          epoll_server(), QuicAllocator::BUFFER_POOL)),
      std::unique_ptr<QuicCryptoServerStream::Helper>(
          new QuicSimpleCryptoServerStreamHelper(QuicRandom::GetInstance())),
      std::unique_ptr<QuicEpollAlarmFactory>(
          new QuicEpollAlarmFactory(epoll_server())),
      response_cache());
}

void QuicTestServer::SetSessionFactory(SessionFactory* factory) {
  DCHECK(dispatcher());
  static_cast<QuicTestDispatcher*>(dispatcher())->SetSessionFactory(factory);
}

void QuicTestServer::SetSpdyStreamFactory(StreamFactory* factory) {
  static_cast<QuicTestDispatcher*>(dispatcher())->SetStreamFactory(factory);
}

void QuicTestServer::SetCryptoStreamFactory(CryptoStreamFactory* factory) {
  static_cast<QuicTestDispatcher*>(dispatcher())
      ->SetCryptoStreamFactory(factory);
}

///////////////////////////   TEST SESSIONS ///////////////////////////////

ImmediateGoAwaySession::ImmediateGoAwaySession(
    const QuicConfig& config,
    QuicConnection* connection,
    QuicSession::Visitor* visitor,
    QuicCryptoServerStream::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    QuicHttpResponseCache* response_cache)
    : QuicSimpleServerSession(config,
                              connection,
                              visitor,
                              helper,
                              crypto_config,
                              compressed_certs_cache,
                              response_cache) {}

void ImmediateGoAwaySession::OnStreamFrame(const QuicStreamFrame& frame) {
  SendGoAway(QUIC_PEER_GOING_AWAY, "");
  QuicSimpleServerSession::OnStreamFrame(frame);
}

}  // namespace test

}  // namespace net
