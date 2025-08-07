// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CONNECTION_POOL_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CONNECTION_POOL_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "openssl/base.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/masque/masque_h2_connection.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

class QUIC_NO_EXPORT MasqueConnectionPool : public MasqueH2Connection::Visitor {
 public:
  using RequestId = uint64_t;
  struct Message {
    quiche::HttpHeaderBlock headers;
    std::string body;
  };

  class QUIC_NO_EXPORT Visitor {
   public:
    virtual ~Visitor() = default;
    virtual void OnResponse(MasqueConnectionPool *pool, RequestId request_id,
                            const absl::StatusOr<Message> &response) = 0;
  };

  // If the request fails immediately, the error will be returned. Otherwise, a
  // request ID will be returned and the result (the response or an error) will
  // be delivered later with that same request ID via Visitor::OnResponse.
  absl::StatusOr<RequestId> SendRequest(const Message &request);

  // `event_loop`, `ssl_ctx`, and `visitor` must outlive this object.
  explicit MasqueConnectionPool(QuicEventLoop *event_loop, SSL_CTX *ssl_ctx,
                                bool disable_certificate_verification,
                                int address_family_for_lookup,
                                Visitor *visitor);

  QuicEventLoop *event_loop() { return event_loop_; }
  SSL_CTX *ssl_ctx() { return ssl_ctx_; }

  // From MasqueH2Connection::Visitor:
  void OnConnectionReady(MasqueH2Connection *connection) override;
  void OnConnectionFinished(MasqueH2Connection *connection) override;
  void OnRequest(MasqueH2Connection *connection, int32_t stream_id,
                 const quiche::HttpHeaderBlock &headers,
                 const std::string &body) override;
  void OnResponse(MasqueH2Connection *connection, int32_t stream_id,
                  const quiche::HttpHeaderBlock &headers,
                  const std::string &body) override;

  static absl::StatusOr<bssl::UniquePtr<SSL_CTX>> CreateSslCtx(
      const std::string &client_cert_file,
      const std::string &client_cert_key_file);

 private:
  class ConnectionState : public QuicSocketEventListener {
   public:
    explicit ConnectionState(MasqueConnectionPool *connection_pool);
    ~ConnectionState() override;
    bool SetupSocket(const std::string &authority,
                     bool disable_certificate_verification,
                     int address_family_for_lookup);
    // From QuicSocketEventListener.
    void OnSocketEvent(QuicEventLoop *event_loop, SocketFd fd,
                       QuicSocketEventMask events) override;

    MasqueH2Connection *connection() { return connection_.get(); }

   private:
    static enum ssl_verify_result_t VerifyCallback(SSL *ssl,
                                                   uint8_t *out_alert);
    enum ssl_verify_result_t VerifyCertificate(SSL *ssl, uint8_t *out_alert);
    MasqueConnectionPool *connection_pool_;  // Not owned.
    std::string authority_;
    std::string host_;
    std::unique_ptr<ProofVerifier> proof_verifier_;
    SocketFd socket_ = kInvalidSocketFd;
    bssl::UniquePtr<SSL> ssl_;
    std::unique_ptr<MasqueH2Connection> connection_;
  };
  struct PendingRequest {
    Message request;
    MasqueH2Connection *connection = nullptr;  // Not owned.
    int32_t stream_id = -1;
  };

  ConnectionState *GetOrCreateConnectionState(const std::string &authority);
  void AttachConnectionToPendingRequests(const std::string &authority,
                                         MasqueH2Connection *connection);
  void SendPendingRequests(MasqueH2Connection *connection);
  void FailPendingRequests(MasqueH2Connection *connection,
                           const absl::Status &error);

  QuicEventLoop *event_loop_;  // Not owned.
  SSL_CTX *ssl_ctx_;           // Not owned.
  const bool disable_certificate_verification_;
  const int address_family_for_lookup_;
  Visitor *visitor_;  // Not owned.
  absl::flat_hash_map<std::string, std::unique_ptr<ConnectionState>>
      connections_;
  absl::flat_hash_map<RequestId, std::unique_ptr<PendingRequest>>
      pending_requests_;
  RequestId next_request_id_ = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CONNECTION_POOL_H_
