// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_SERVER_SESSION_H_
#define QUICHE_QUIC_QBONE_QBONE_SERVER_SESSION_H_

#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_control.pb.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_control_stream.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_processor.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_writer.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_session_base.h"

namespace quic {

// A helper class is used by the QuicCryptoServerStream.
class QboneCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
 public:
  // This will look for the qbone alpn.
  bool CanAcceptClientHello(const CryptoHandshakeMessage& chlo,
                            const QuicSocketAddress& client_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& self_address,
                            string* error_details) const override;
};

class QUIC_EXPORT_PRIVATE QboneServerSession
    : public QboneSessionBase,
      public QbonePacketProcessor::OutputInterface,
      public QbonePacketProcessor::StatsInterface {
 public:
  QboneServerSession(const quic::ParsedQuicVersionVector& supported_versions,
                     QuicConnection* connection,
                     Visitor* owner,
                     const QuicConfig& config,
                     const QuicCryptoServerConfig* quic_crypto_server_config,
                     QuicCompressedCertsCache* compressed_certs_cache,
                     QbonePacketWriter* writer,
                     QuicIpAddress self_ip,
                     QuicIpAddress client_ip,
                     size_t client_ip_subnet_length,
                     QboneServerControlStream::Handler* handler);
  QboneServerSession(const QboneServerSession&) = delete;
  QboneServerSession& operator=(const QboneServerSession&) = delete;
  ~QboneServerSession() override;

  void Initialize() override;

  virtual bool SendClientRequest(const QboneClientRequest& request);

  void ProcessPacketFromNetwork(QuicStringPiece packet) override;
  void ProcessPacketFromPeer(QuicStringPiece packet) override;

  // QbonePacketProcessor::OutputInterface implementation.
  void SendPacketToClient(QuicStringPiece packet) override;
  void SendPacketToNetwork(QuicStringPiece packet) override;

  // QbonePacketProcessor::StatsInterface implementation.
  void OnPacketForwarded(QbonePacketProcessor::Direction direction) override {}
  void OnPacketDroppedSilently(
      QbonePacketProcessor::Direction direction) override {}
  void OnPacketDroppedWithIcmp(
      QbonePacketProcessor::Direction direction) override {}
  void OnPacketDroppedWithTcpReset(
      QbonePacketProcessor::Direction direction) override {}
  void OnPacketDeferred(QbonePacketProcessor::Direction direction) override {}

 protected:
  // QboneSessionBase interface implementation.
  std::unique_ptr<QuicCryptoStream> CreateCryptoStream() override;
  // The packet processor.
  QbonePacketProcessor processor_;

 private:
  // Config for QUIC crypto server stream, used by the server.
  const QuicCryptoServerConfig* quic_crypto_server_config_;
  // Used by QUIC crypto server stream to track most recently compressed certs.
  QuicCompressedCertsCache* compressed_certs_cache_;
  // This helper is needed when create QuicCryptoServerStream.
  QboneCryptoServerStreamHelper stream_helper_;
  // Passed to the control stream.
  QboneServerControlStream::Handler* handler_;
  // The unowned control stream.
  QboneServerControlStream* control_stream_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_SERVER_SESSION_H_
