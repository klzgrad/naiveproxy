// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUARTC_QUARTC_SESSION_H_
#define NET_QUIC_QUARTC_QUARTC_SESSION_H_

#include "net/quic/core/quic_crypto_client_stream.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_session.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/quartc/quartc_clock_interface.h"
#include "net/quic/quartc/quartc_session_interface.h"
#include "net/quic/quartc/quartc_stream.h"

namespace net {

// A helper class is used by the QuicCryptoServerStream.
class QuartcCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
 public:
  QuicConnectionId GenerateConnectionIdForReject(
      QuicConnectionId connection_id) const override;

  bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                            const QuicSocketAddress& self_address,
                            std::string* error_details) const override;
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
                QuicClock* clock);
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

  QuartcStreamInterface* CreateOutgoingStream(
      const OutgoingStreamParameters& param) override;

  void CancelStream(QuicStreamId stream_id) override;

  bool IsOpenStream(QuicStreamId stream_id) override;

  QuartcSessionStats GetStats() override;

  void SetDelegate(QuartcSessionInterface::Delegate* session_delegate) override;

  void OnTransportCanWrite() override;

  // Decrypts an incoming QUIC packet to a data stream.
  bool OnTransportReceived(const char* data, size_t data_len) override;

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
                                                 SpdyPriority priority);
  // Activates a QuartcStream.  The session takes ownership of the stream, but
  // returns an unowned pointer to the stream for convenience.
  QuartcStream* ActivateDataStream(std::unique_ptr<QuartcStream> stream);

  void ResetStream(QuicStreamId stream_id, QuicRstStreamErrorCode error);

 private:
  // For crypto handshake.
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
  const std::string unique_remote_server_id_;
  Perspective perspective_;
  // Take the ownership of the QuicConnection.
  std::unique_ptr<QuicConnection> connection_;
  // Not owned by QuartcSession. From the QuartcFactory.
  QuicConnectionHelperInterface* helper_;
  // For recording packet receipt time
  QuicClock* clock_;
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

  DISALLOW_COPY_AND_ASSIGN(QuartcSession);
};

}  // namespace net

#endif  // NET_QUIC_QUARTC_QUARTC_SESSION_H_
