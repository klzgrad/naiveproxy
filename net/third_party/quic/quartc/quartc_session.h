// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_H_

#include "net/third_party/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quic/core/quic_crypto_stream.h"
#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/quartc/quartc_clock_interface.h"
#include "net/third_party/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quic/quartc/quartc_session_interface.h"
#include "net/third_party/quic/quartc/quartc_stream.h"

namespace net {

// A helper class is used by the QuicCryptoServerStream.
class QuartcCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
 public:
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const override;

  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& client_address,
                            const QuicSocketAddress& peer_address,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;
};

// Adapts |QuartcSessionVisitor|s to the |QuicConnectionDebugVisitor| interface.
// Keeps a set of |QuartcSessionVisitor|s and forwards QUIC debug callbacks to
// each visitor in the set.
class QuartcSessionVisitorAdapter : public QuicConnectionDebugVisitor {
 public:
  QuartcSessionVisitorAdapter();
  ~QuartcSessionVisitorAdapter() override;

  void OnPacketSent(const SerializedPacket& serialized_packet,
                    QuicPacketNumber original_packet_number,
                    TransmissionType transmission_type,
                    QuicTime sent_time) override;
  void OnIncomingAck(const QuicAckFrame& ack_frame,
                     QuicTime ack_receive_time,
                     QuicPacketNumber largest_observed,
                     bool rtt_updated,
                     QuicPacketNumber least_unacked_sent_packet) override;
  void OnPacketLoss(QuicPacketNumber lost_packet_number,
                    TransmissionType transmission_type,
                    QuicTime detection_time) override;
  void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame,
                           const QuicTime& receive_time) override;
  void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& version) override;

  const std::set<QuartcSessionVisitor*>& visitors() const { return visitors_; }
  std::set<QuartcSessionVisitor*>& mutable_visitors() { return visitors_; }

  // Disallow copy and assign.
  QuartcSessionVisitorAdapter(const QuartcSessionVisitorAdapter&) = delete;
  QuartcSessionVisitorAdapter operator=(const QuartcSessionVisitorAdapter&) =
      delete;

 private:
  std::set<QuartcSessionVisitor*> visitors_;
};

class QUIC_EXPORT_PRIVATE QuartcSession
    : public QuicSession,
      public QuartcSessionInterface,
      public QuicCryptoClientStream::ProofHandler {
 public:
  QuartcSession(std::unique_ptr<QuicConnection> connection,
                const QuicConfig& config,
                const std::string& unique_remote_server_id,
                Perspective perspective,
                QuicConnectionHelperInterface* helper,
                QuicClock* clock,
                std::unique_ptr<QuartcPacketWriter> packet_writer);
  ~QuartcSession() override;

  // QuicSession overrides.
  QuicCryptoStream* GetMutableCryptoStream() override;

  const QuicCryptoStream* GetCryptoStream() const override;

  QuartcStream* CreateOutgoingDynamicStream() override;

  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

  void CloseStream(QuicStreamId stream_id) override;

  // QuicConnectionVisitorInterface overrides.
  void OnConnectionClosed(QuicErrorCode error,
                          const std::string& error_details,
                          ConnectionCloseSource source) override;

  // QuartcSessionInterface overrides
  void StartCryptoHandshake() override;

  bool ExportKeyingMaterial(const std::string& label,
                            const uint8_t* context,
                            size_t context_len,
                            bool used_context,
                            uint8_t* result,
                            size_t result_len) override;

  void CloseConnection(const std::string& details) override;

  QuartcStreamInterface* CreateOutgoingStream(
      const OutgoingStreamParameters& param) override;

  void CancelStream(QuicStreamId stream_id) override;

  bool IsOpenStream(QuicStreamId stream_id) override;

  QuicConnectionStats GetStats() override;

  void SetDelegate(QuartcSessionInterface::Delegate* session_delegate) override;

  void AddSessionVisitor(QuartcSessionVisitor* visitor) override;
  void RemoveSessionVisitor(QuartcSessionVisitor* visitor) override;

  void OnTransportCanWrite() override;

  // Decrypts an incoming QUIC packet to a data stream.
  bool OnTransportReceived(const char* data, size_t data_len) override;

  void BundleWrites() override;
  void FlushWrites() override;

  // ProofHandler overrides.
  void OnProofValid(const QuicCryptoClientConfig::CachedState& cached) override;

  // Called by the client crypto handshake when proof verification details
  // become available, either because proof verification is complete, or when
  // cached details are used.
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) override;

  // Override the default crypto configuration.
  // The session will take the ownership of the configurations.
  void SetClientCryptoConfig(QuicCryptoClientConfig* client_config);

  void SetServerCryptoConfig(QuicCryptoServerConfig* server_config);

 protected:
  // QuicSession override.
  QuicStream* CreateIncomingDynamicStream(QuicStreamId id) override;

  std::unique_ptr<QuartcStream> CreateDataStream(QuicStreamId id,
                                                 spdy::SpdyPriority priority);
  // Activates a QuartcStream.  The session takes ownership of the stream, but
  // returns an unowned pointer to the stream for convenience.
  QuartcStream* ActivateDataStream(std::unique_ptr<QuartcStream> stream);

  void ResetStream(QuicStreamId stream_id, QuicRstStreamErrorCode error);

 private:
  // For crypto handshake.
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
  const std::string unique_remote_server_id_;
  Perspective perspective_;
  // Take the ownership of the QuicConnection.  Note:  if |connection_| changes,
  // the new value of |connection_| must be given to |packet_writer_| before any
  // packets are written.  Otherwise, |packet_writer_| will crash.
  std::unique_ptr<QuicConnection> connection_;
  // Not owned by QuartcSession. From the QuartcFactory.
  QuicConnectionHelperInterface* helper_;
  // For recording packet receipt time
  QuicClock* clock_;
  // Packet writer used by |connection_|.
  std::unique_ptr<QuartcPacketWriter> packet_writer_;

  // Not owned by QuartcSession.
  QuartcSessionInterface::Delegate* session_delegate_ = nullptr;
  // Used by QUIC crypto server stream to track most recently compressed certs.
  std::unique_ptr<QuicCompressedCertsCache> quic_compressed_certs_cache_;
  // This helper is needed when create QuicCryptoServerStream.
  QuartcCryptoServerStreamHelper stream_helper_;
  // Config for QUIC crypto client stream, used by the client.
  std::unique_ptr<QuicCryptoClientConfig> quic_crypto_client_config_;
  // Config for QUIC crypto server stream, used by the server.
  std::unique_ptr<QuicCryptoServerConfig> quic_crypto_server_config_;

  // Holds pointers to QuartcSessionVisitors and adapts them to the
  // QuicConnectionDebugVisitor interface.
  QuartcSessionVisitorAdapter session_visitor_adapter_;

  std::unique_ptr<QuicConnection::ScopedPacketFlusher> packet_flusher_;

  DISALLOW_COPY_AND_ASSIGN(QuartcSession);
};

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_H_
