// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_SERVER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_SERVER_H_

#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/tools/quic_server.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_backend.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_session.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_stream.h"

namespace quic {

namespace test {

// A test server which enables easy creation of custom QuicServerSessions
//
// Eventually this may be extended to allow custom QuicConnections etc.
class QuicTestServer : public QuicServer {
 public:
  // Factory for creating QuicServerSessions.
  class SessionFactory {
   public:
    virtual ~SessionFactory() {}

    // Returns a new session owned by the caller.
    virtual std::unique_ptr<QuicServerSessionBase> CreateSession(
        const QuicConfig& config,
        QuicConnection* connection,
        QuicSession::Visitor* visitor,
        QuicCryptoServerStreamBase::Helper* helper,
        const QuicCryptoServerConfig* crypto_config,
        QuicCompressedCertsCache* compressed_certs_cache,
        QuicSimpleServerBackend* quic_simple_server_backend) = 0;
  };

  // Factory for creating QuicSimpleServerStreams.
  class StreamFactory {
   public:
    virtual ~StreamFactory() {}

    // Returns a new stream owned by the caller.
    virtual QuicSimpleServerStream* CreateStream(
        QuicStreamId id,
        QuicSpdySession* session,
        QuicSimpleServerBackend* quic_simple_server_backend) = 0;
  };

  class CryptoStreamFactory {
   public:
    virtual ~CryptoStreamFactory() {}

    // Returns a new QuicCryptoServerStreamBase owned by the caller
    virtual std::unique_ptr<QuicCryptoServerStreamBase> CreateCryptoStream(
        const QuicCryptoServerConfig* crypto_config,
        QuicServerSessionBase* session) = 0;
  };

  QuicTestServer(std::unique_ptr<ProofSource> proof_source,
                 QuicSimpleServerBackend* quic_simple_server_backend);
  QuicTestServer(std::unique_ptr<ProofSource> proof_source,
                 const QuicConfig& config,
                 const ParsedQuicVersionVector& supported_versions,
                 QuicSimpleServerBackend* quic_simple_server_backend);
  QuicTestServer(std::unique_ptr<ProofSource> proof_source,
                 const QuicConfig& config,
                 const ParsedQuicVersionVector& supported_versions,
                 QuicSimpleServerBackend* quic_simple_server_backend,
                 uint8_t expected_server_connection_id_length);

  // Create a custom dispatcher which creates custom sessions.
  QuicDispatcher* CreateQuicDispatcher() override;

  // Sets a custom session factory, owned by the caller, for easy custom
  // session logic. This is incompatible with setting a stream factory or a
  // crypto stream factory.
  void SetSessionFactory(SessionFactory* factory);

  // Sets a custom stream factory, owned by the caller, for easy custom
  // stream logic. This is incompatible with setting a session factory.
  void SetSpdyStreamFactory(StreamFactory* factory);

  // Sets a custom crypto stream factory, owned by the caller, for easy custom
  // crypto logic.  This is incompatible with setting a session factory.
  void SetCryptoStreamFactory(CryptoStreamFactory* factory);
};

// Useful test sessions for the QuicTestServer.

// Test session which sends a GOAWAY immedaitely on creation, before crypto
// credentials have even been established.
class ImmediateGoAwaySession : public QuicSimpleServerSession {
 public:
  ImmediateGoAwaySession(const QuicConfig& config,
                         QuicConnection* connection,
                         QuicSession::Visitor* visitor,
                         QuicCryptoServerStreamBase::Helper* helper,
                         const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicSimpleServerBackend* quic_simple_server_backend);

  // Override to send GoAway.
  void OnStreamFrame(const QuicStreamFrame& frame) override;
  void OnCryptoFrame(const QuicCryptoFrame& frame) override;
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_SERVER_H_
