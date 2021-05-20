// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_

#include "absl/strings/string_view.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "quic/core/quic_connection.h"
#include "quic/core/quic_crypto_server_stream_base.h"
#include "quic/core/quic_session.h"
#include "quic/quic_transport/quic_transport_protocol.h"
#include "quic/quic_transport/quic_transport_session_interface.h"
#include "quic/quic_transport/quic_transport_stream.h"

namespace quic {

// A server session for the QuicTransport protocol.
class QUIC_EXPORT_PRIVATE QuicTransportServerSession
    : public QuicSession,
      public QuicTransportSessionInterface {
 public:
  class QUIC_EXPORT_PRIVATE ServerVisitor {
   public:
    virtual ~ServerVisitor() {}

    // Allows the server to decide whether the specified origin is allowed to
    // connect to it.
    virtual bool CheckOrigin(url::Origin origin) = 0;

    // Indicates that the server received a path parameter from the client.  The
    // path parameter is parsed, and can be retrived from url.path() and
    // url.query().  If this method returns false, the connection is closed.
    virtual bool ProcessPath(const GURL& url) = 0;
  };

  QuicTransportServerSession(QuicConnection* connection,
                             Visitor* owner,
                             const QuicConfig& config,
                             const ParsedQuicVersionVector& supported_versions,
                             const QuicCryptoServerConfig* crypto_config,
                             QuicCompressedCertsCache* compressed_certs_cache,
                             ServerVisitor* visitor);

  std::vector<absl::string_view>::const_iterator SelectAlpn(
      const std::vector<absl::string_view>& alpns) const override {
    return std::find(alpns.cbegin(), alpns.cend(), QuicTransportAlpn());
  }

  bool ShouldKeepConnectionAlive() const override { return true; }

  QuicCryptoServerStreamBase* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoServerStreamBase* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

  // Returns true once the encryption has been established, the client
  // indication has been received and the origin has been verified.  No
  // application data will be read or written before the connection is ready.
  // Once the connection becomes ready, this method will never return false.
  bool IsSessionReady() const override { return ready_; }

  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    QUIC_BUG << "QuicTransportServerSession::CreateIncomingStream("
                "PendingStream) not implemented";
    return nullptr;
  }

 protected:
  class QUIC_EXPORT_PRIVATE ClientIndication : public QuicStream {
   public:
    explicit ClientIndication(QuicTransportServerSession* session);
    void OnDataAvailable() override;

   private:
    QuicTransportServerSession* session_;
    std::string buffer_;
  };

  // Utility class for parsing the client indication.
  class QUIC_EXPORT_PRIVATE ClientIndicationParser {
   public:
    ClientIndicationParser(QuicTransportServerSession* session,
                           absl::string_view indication)
        : session_(session), reader_(indication) {}

    // Parses the specified indication.  Automatically closes the connection
    // with detailed error if parsing fails.  Returns true on success, false on
    // failure.
    bool Parse();

   private:
    void Error(const std::string& error_message);
    void ParseError(absl::string_view error_message);

    // Processes the path portion of the client indication.
    bool ProcessPath(absl::string_view path);

    QuicTransportServerSession* session_;
    QuicDataReader reader_;
  };

  // Parses and processes the client indication as described in
  // https://vasilvv.github.io/webtransport/draft-vvv-webtransport-quic.html#rfc.section.3.2
  void ProcessClientIndication(absl::string_view indication);

  virtual void OnIncomingDataStream(QuicTransportStream* /*stream*/) {}

  std::unique_ptr<QuicCryptoServerStreamBase> crypto_stream_;
  bool ready_ = false;
  ServerVisitor* visitor_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_
