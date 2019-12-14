// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_

#include "url/origin.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_session_interface.h"

namespace quic {

// A server session for the QuicTransport protocol.
class QUIC_EXPORT QuicTransportServerSession
    : public QuicSession,
      public QuicTransportSessionInterface {
 public:
  class ServerVisitor {
   public:
    virtual ~ServerVisitor() {}

    virtual bool CheckOrigin(url::Origin origin) = 0;
  };

  QuicTransportServerSession(QuicConnection* connection,
                             Visitor* owner,
                             const QuicConfig& config,
                             const ParsedQuicVersionVector& supported_versions,
                             const QuicCryptoServerConfig* crypto_config,
                             QuicCompressedCertsCache* compressed_certs_cache,
                             ServerVisitor* visitor);

  std::vector<QuicStringPiece>::const_iterator SelectAlpn(
      const std::vector<QuicStringPiece>& alpns) const override {
    return std::find(alpns.cbegin(), alpns.cend(), QuicTransportAlpn());
  }

  bool ShouldKeepConnectionAlive() const override { return true; }

  QuicCryptoStream* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoStream* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

  bool IsSessionReady() const override { return ready_; }

  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    QUIC_BUG << "QuicTransportServerSession::CreateIncomingStream("
                "PendingStream) not implemented";
    return nullptr;
  }

 protected:
  class ClientIndication : public QuicStream {
   public:
    explicit ClientIndication(QuicTransportServerSession* session);
    void OnDataAvailable() override;

   private:
    QuicTransportServerSession* session_;
    std::string buffer_;
  };

  // Utility class for parsing the client indication.
  class ClientIndicationParser {
   public:
    ClientIndicationParser(QuicTransportServerSession* session,
                           QuicStringPiece indication)
        : session_(session), reader_(indication) {}

    // Parses the specified indication.  Automatically closes the connection
    // with detailed error if parsing fails.  Returns true on success, false on
    // failure.
    bool Parse();

   private:
    void Error(const std::string& error_message);
    void ParseError(QuicStringPiece error_message);

    QuicTransportServerSession* session_;
    QuicDataReader reader_;
  };

  // Parses and processes the client indication as described in
  // https://vasilvv.github.io/webtransport/draft-vvv-webtransport-quic.html#rfc.section.3.2
  void ProcessClientIndication(QuicStringPiece indication);

  std::unique_ptr<QuicCryptoServerStream> crypto_stream_;
  bool ready_ = false;
  ServerVisitor* visitor_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_
