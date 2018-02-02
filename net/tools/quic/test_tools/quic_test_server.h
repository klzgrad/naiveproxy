// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_TEST_TOOLS_QUIC_TEST_SERVER_H_
#define NET_TOOLS_QUIC_TEST_TOOLS_QUIC_TEST_SERVER_H_

#include "net/quic/core/quic_session.h"
#include "net/tools/quic/quic_dispatcher.h"
#include "net/tools/quic/quic_server.h"
#include "net/tools/quic/quic_simple_server_session.h"
#include "net/tools/quic/quic_simple_server_stream.h"

namespace net {

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
    virtual QuicServerSessionBase* CreateSession(
        const QuicConfig& config,
        QuicConnection* connection,
        QuicSession::Visitor* visitor,
        QuicCryptoServerStream::Helper* helper,
        const QuicCryptoServerConfig* crypto_config,
        QuicCompressedCertsCache* compressed_certs_cache,
        QuicHttpResponseCache* response_cache) = 0;
  };

  // Factory for creating QuicSimpleServerStreams.
  class StreamFactory {
   public:
    virtual ~StreamFactory() {}

    // Returns a new stream owned by the caller.
    virtual QuicSimpleServerStream* CreateStream(
        QuicStreamId id,
        QuicSpdySession* session,
        QuicHttpResponseCache* response_cache) = 0;
  };

  class CryptoStreamFactory {
   public:
    virtual ~CryptoStreamFactory() {}

    // Returns a new QuicCryptoServerStreamBase owned by the caller
    virtual QuicCryptoServerStreamBase* CreateCryptoStream(
        const QuicCryptoServerConfig* crypto_config,
        QuicServerSessionBase* session) = 0;
  };

  QuicTestServer(std::unique_ptr<ProofSource> proof_source,
                 QuicHttpResponseCache* response_cache);
  QuicTestServer(std::unique_ptr<ProofSource> proof_source,
                 const QuicConfig& config,
                 const QuicTransportVersionVector& supported_versions,
                 QuicHttpResponseCache* response_cache);

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
                         QuicCryptoServerStream::Helper* helper,
                         const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicHttpResponseCache* response_cache);

  // Override to send GoAway.
  void OnStreamFrame(const QuicStreamFrame& frame) override;
};

}  // namespace test

}  // namespace net

#endif  // NET_TOOLS_QUIC_TEST_TOOLS_QUIC_TEST_SERVER_H_
