// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_GENERIC_CLIENT_SESSION_H_
#define QUICHE_QUIC_CORE_QUIC_GENERIC_CLIENT_SESSION_H_

#include <cstdint>
#include <memory>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_crypto_client_stream.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_datagram_queue.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/web_transport_stats.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

class QuicGenericStream;

// Returns QUIC versions to be used with QuicGenericSessionBase.
ParsedQuicVersionVector GetQuicVersionsForGenericSession();

using CreateWebTransportSessionVisitorCallback =
    quiche::UnretainedCallback<std::unique_ptr<webtransport::SessionVisitor>(
        webtransport::Session& session)>;

// QuicGenericSessionBase lets users access raw QUIC connections via
// WebTransport API.
class QUICHE_EXPORT QuicGenericSessionBase : public QuicSession,
                                             public webtransport::Session {
 public:
  QuicGenericSessionBase(
      QuicConnection* connection, bool owns_connection, Visitor* owner,
      const QuicConfig& config, std::string alpn,
      webtransport::SessionVisitor* visitor, bool owns_visitor,
      std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer);
  ~QuicGenericSessionBase();

  // QuicSession implementation.
  std::vector<std::string> GetAlpnsToOffer() const override {
    return std::vector<std::string>({alpn_});
  }
  std::vector<absl::string_view>::const_iterator SelectAlpn(
      const std::vector<absl::string_view>& alpns) const override {
    return absl::c_find(alpns, alpn_);
  }
  void OnAlpnSelected(absl::string_view alpn) override {
    QUICHE_DCHECK_EQ(alpn, alpn_);
  }
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;

  bool ShouldKeepConnectionAlive() const override { return true; }

  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    QUIC_BUG(QuicGenericSessionBase_PendingStream)
        << "QuicGenericSessionBase::CreateIncomingStream(PendingStream) not "
           "implemented";
    return nullptr;
  }

  void OnTlsHandshakeComplete() override;
  void OnMessageReceived(absl::string_view message) override;

  // webtransport::Session implementation.
  webtransport::Stream* AcceptIncomingBidirectionalStream() override;
  webtransport::Stream* AcceptIncomingUnidirectionalStream() override;

  bool CanOpenNextOutgoingBidirectionalStream() override {
    return QuicSession::CanOpenNextOutgoingBidirectionalStream();
  }
  bool CanOpenNextOutgoingUnidirectionalStream() override {
    return QuicSession::CanOpenNextOutgoingUnidirectionalStream();
  }
  webtransport::Stream* OpenOutgoingBidirectionalStream() override;
  webtransport::Stream* OpenOutgoingUnidirectionalStream() override;

  webtransport::Stream* GetStreamById(webtransport::StreamId id) override;

  webtransport::DatagramStatus SendOrQueueDatagram(
      absl::string_view datagram) override;
  void SetDatagramMaxTimeInQueue(absl::Duration max_time_in_queue) override {
    datagram_queue()->SetMaxTimeInQueue(QuicTimeDelta(max_time_in_queue));
  }
  webtransport::DatagramStats GetDatagramStats() override {
    return WebTransportDatagramStatsForQuicSession(*this);
  }
  webtransport::SessionStats GetSessionStats() override {
    return WebTransportStatsForQuicSession(*this);
  }
  void NotifySessionDraining() override {}
  void SetOnDraining(quiche::SingleUseCallback<void()>) override {}

  void CloseSession(webtransport::SessionErrorCode error_code,
                    absl::string_view error_message) override {
    connection()->CloseConnection(
        QUIC_NO_ERROR, static_cast<QuicIetfTransportErrorCodes>(error_code),
        std::string(error_message),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  QuicByteCount GetMaxDatagramSize() const override {
    return GetGuaranteedLargestMessagePayload();
  }

 private:
  // Creates and activates a QuicGenericStream for the given ID.
  QuicGenericStream* CreateStream(QuicStreamId id);

  void OnCanCreateNewOutgoingStream(bool unidirectional) override;

  std::string alpn_;
  webtransport::SessionVisitor* visitor_;
  bool owns_connection_;
  bool owns_visitor_;

  // Contains all of the streams that has been received by the session but have
  // not been processed by the application.
  quiche::QuicheCircularDeque<QuicStreamId> incoming_bidirectional_streams_;
  quiche::QuicheCircularDeque<QuicStreamId> incoming_unidirectional_streams_;
};

class QUICHE_EXPORT QuicGenericClientSession final
    : public QuicGenericSessionBase {
 public:
  QuicGenericClientSession(
      QuicConnection* connection, bool owns_connection, Visitor* owner,
      const QuicConfig& config, std::string host, uint16_t port,
      std::string alpn, webtransport::SessionVisitor* visitor,
      bool owns_visitor,
      std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
      QuicCryptoClientConfig* crypto_config);
  QuicGenericClientSession(
      QuicConnection* connection, bool owns_connection, Visitor* owner,
      const QuicConfig& config, std::string host, uint16_t port,
      std::string alpn,
      CreateWebTransportSessionVisitorCallback create_visitor_callback,
      std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
      QuicCryptoClientConfig* crypto_config);

  void CryptoConnect() { crypto_stream_->CryptoConnect(); }

  // QuicSession implementation.
  QuicCryptoStream* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoStream* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

 private:
  std::unique_ptr<QuicCryptoClientStream> crypto_stream_;
};

class QUICHE_EXPORT QuicGenericServerSession final
    : public QuicGenericSessionBase {
 public:
  QuicGenericServerSession(
      QuicConnection* connection, bool owns_connection, Visitor* owner,
      const QuicConfig& config, std::string alpn,
      webtransport::SessionVisitor* visitor, bool owns_visitor,
      std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache);
  QuicGenericServerSession(
      QuicConnection* connection, bool owns_connection, Visitor* owner,
      const QuicConfig& config, std::string alpn,
      CreateWebTransportSessionVisitorCallback create_visitor_callback,
      std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache);

  // QuicSession implementation.
  QuicCryptoStream* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoStream* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

 private:
  std::unique_ptr<QuicCryptoServerStreamBase> crypto_stream_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_GENERIC_CLIENT_SESSION_H_
