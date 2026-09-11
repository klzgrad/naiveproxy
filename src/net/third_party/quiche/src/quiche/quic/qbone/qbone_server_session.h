// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_SERVER_SESSION_H_
#define QUICHE_QUIC_QBONE_QBONE_SERVER_SESSION_H_

#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/qbone/qbone_control.pb.h"
#include "quiche/quic/qbone/qbone_control_stream.h"
#include "quiche/quic/qbone/qbone_packet_processor.h"
#include "quiche/quic/qbone/qbone_packet_writer.h"
#include "quiche/quic/qbone/qbone_session_base.h"

namespace quic {

// A helper class is used by the QuicCryptoServerStream.
class QboneCryptoServerStreamHelper
    : public QuicCryptoServerStreamBase::Helper {
 public:
  // This will look for the QBONE alpn.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& chlo,
                            const QuicSocketAddress& client_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;
};

class QUIC_EXPORT_PRIVATE QboneServerSession
    : public QboneSessionBase,
      public QbonePacketProcessor::OutputInterface,
      public QbonePacketProcessor::StatsInterface {
 public:
  // `writer` may be nullptr, but a non-null writer must be given (through
  // set_writer() or a test value override) before sending any packets to the
  // network.
  QboneServerSession(
      const quic::ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection, Visitor* owner, const QuicConfig& config,
      const QuicCryptoServerConfig* quic_crypto_server_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      QbonePacketWriter* absl_nullable writer ABSL_ATTRIBUTE_LIFETIME_BOUND,
      QuicIpAddress self_ip, QuicIpAddress client_ip,
      size_t client_ip_subnet_length,
      QboneServerControlStream::Handler* handler);
  QboneServerSession(const QboneServerSession&) = delete;
  QboneServerSession& operator=(const QboneServerSession&) = delete;
  ~QboneServerSession() override;

  // Override to create control stream at FORWARD_SECURE encryption level.
  void SetDefaultEncryptionLevel(quic::EncryptionLevel level) override;

  virtual bool SendClientRequest(const QboneClientRequest& request);

  void ProcessPacketFromNetwork(absl::string_view packet) override;
  void ProcessPacketFromPeer(absl::string_view packet) override;

  // QbonePacketProcessor::OutputInterface implementation.
  void SendPacketToClient(absl::string_view packet) override;
  void SendPacketToNetwork(absl::string_view packet) override;

  // QbonePacketProcessor::StatsInterface implementation.
  void OnPacketForwarded(QbonePacketProcessor::Direction direction,
                         uint8_t traffic_class) override {}
  void OnPacketDroppedSilently(QbonePacketProcessor::Direction direction,
                               uint8_t traffic_class) override {}
  void OnPacketDroppedWithIcmp(QbonePacketProcessor::Direction direction,
                               uint8_t traffic_class) override {}
  void OnPacketDroppedWithTcpReset(QbonePacketProcessor::Direction direction,
                                   uint8_t traffic_class) override {}
  void RecordThroughput(size_t bytes, QbonePacketProcessor::Direction direction,
                        uint8_t traffic_class) override {}

  // `writer` must outlive the session.
  void set_writer(QbonePacketWriter* absl_nullable writer);

 protected:
  // QboneSessionBase interface implementation.
  std::unique_ptr<QuicCryptoStream> CreateCryptoStream() override;
  void SendErrorPacketToNetwork(absl::string_view packet) override;

  // Instantiates QboneServerControlStream.
  virtual void CreateControlStream();

  // Instantiates QboneServerControlStream from the pending stream and returns a
  // pointer to it.
  QuicStream* CreateControlStreamFromPendingStream(PendingStream* pending);

  // The packet processor.
  QbonePacketProcessor processor_;

  // Config for QUIC crypto server stream, used by the server.
  const QuicCryptoServerConfig* quic_crypto_server_config_;

 private:
  QbonePacketWriter* absl_nullable writer_;

  // Used by QUIC crypto server stream to track most recently compressed certs.
  QuicCompressedCertsCache* compressed_certs_cache_;
  // This helper is needed when create QuicCryptoServerStream.
  QboneCryptoServerStreamHelper stream_helper_;
  // Passed to the control stream.
  QboneServerControlStream::Handler* handler_;
  // The unowned control stream.
  QboneServerControlStream* control_stream_ = nullptr;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_SERVER_SESSION_H_
