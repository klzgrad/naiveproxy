// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CONNECTION_POOL_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CONNECTION_POOL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/masque/masque_h2_connection.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/quiche_socket_address.h"

namespace quic {

class QUIC_NO_EXPORT MasqueConnectionPool : public MasqueH2Connection::Visitor {
 public:
  using RequestId = uint64_t;
  struct Message {
    quiche::HttpHeaderBlock headers;
    std::string body;
  };

  // Returns the HTTP status code from the message, or 0 if not available.
  static int16_t GetStatusCode(const Message& message);

  // Interface for resolving addresses.
  class QUIC_NO_EXPORT DnsResolver {
   public:
    virtual ~DnsResolver() = default;

    // Address Resolution must be thread safe.
    // Address family should be AF_UNSPEC, AF_INET, or AF_INET6.
    virtual quiche::QuicheSocketAddress LookupAddress(
        int address_family_for_lookup, absl::string_view host,
        absl::string_view port) const = 0;
  };

  // Configuration for DNS resolution.
  class QUIC_NO_EXPORT DnsConfig {
   public:
    DnsConfig() = default;

    absl::Status SetAddressFamily(int address_family);
    int address_family_for_lookup() const { return address_family_for_lookup_; }

    // Note that `resolver` is unowned and must outlive any MasqueConnectionPool
    // configured with this object. If `resolver` is nullptr, a default DNS
    // resolver will be used.
    void SetResolver(const DnsResolver* resolver) { resolver_ = resolver; }
    const DnsResolver* resolver() const { return resolver_; }

    absl::Status SetOverrides(const std::string& overrides);
    void ApplyOverrides(absl::string_view* host, absl::string_view* port) const;

   private:
    int address_family_for_lookup_ = AF_UNSPEC;
    const DnsResolver* resolver_ = nullptr;
    absl::flat_hash_map<std::pair<std::string, std::string>,
                        std::pair<std::string, std::string>>
        overrides_;
  };

  class QUIC_NO_EXPORT Visitor {
   public:
    virtual ~Visitor() = default;
    virtual void OnPoolResponse(MasqueConnectionPool* pool,
                                RequestId request_id,
                                absl::StatusOr<Message>&& response) = 0;
  };

  // If the request fails immediately, the error will be returned. Otherwise, a
  // request ID will be returned and the result (the response or an error) will
  // be delivered later with that same request ID via Visitor::OnResponse.
  absl::StatusOr<RequestId> SendRequest(const Message& request,
                                        bool mtls = false);

  // `event_loop`, `ssl_ctx`, and `visitor` must outlive this object.
  explicit MasqueConnectionPool(QuicEventLoop* event_loop, SSL_CTX* ssl_ctx,
                                bool disable_certificate_verification,
                                const DnsConfig& dns_config, Visitor* visitor);

  QuicEventLoop* event_loop() { return event_loop_; }
  SSL_CTX* GetSslCtx(bool mtls) { return mtls ? mtls_ssl_ctx_ : tls_ssl_ctx_; }
  void SetMtlsSslCtx(SSL_CTX* ssl_ctx) { mtls_ssl_ctx_ = ssl_ctx; }

  // From MasqueH2Connection::Visitor:
  void OnConnectionReady(MasqueH2Connection* connection) override;
  void OnConnectionFinished(MasqueH2Connection* connection,
                            absl::Status error) override;
  void OnRequest(MasqueH2Connection* connection, int32_t stream_id,
                 const quiche::HttpHeaderBlock& headers,
                 const std::string& body) override;
  void OnResponse(MasqueH2Connection* connection, int32_t stream_id,
                  const quiche::HttpHeaderBlock& headers,
                  const std::string& body) override;
  void OnStreamFailure(MasqueH2Connection* connection, int32_t stream_id,
                       absl::Status error) override;

  static absl::StatusOr<bssl::UniquePtr<SSL_CTX>> CreateSslCtx(
      const std::string& client_cert_file,
      const std::string& client_cert_key_file);

  static absl::StatusOr<bssl::UniquePtr<SSL_CTX>> CreateSslCtxFromData(
      const std::string& client_cert_pem_data,
      const std::string& client_cert_key_data);

 private:
  class ConnectionState : public QuicSocketEventListener {
   public:
    explicit ConnectionState(MasqueConnectionPool* connection_pool);
    ~ConnectionState() override;
    absl::Status SetupSocket(const std::string& authority,
                             bool disable_certificate_verification);
    // From QuicSocketEventListener.
    void OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                       QuicSocketEventMask events) override;

    MasqueH2Connection* connection() { return connection_.get(); }
    void set_mtls(bool mtls) { mtls_ = mtls; }

   private:
    static enum ssl_verify_result_t VerifyCallback(SSL* ssl,
                                                   uint8_t* out_alert);
    enum ssl_verify_result_t VerifyCertificate(SSL* ssl, uint8_t* out_alert);
    MasqueConnectionPool* connection_pool_;  // Not owned.
    std::string authority_;
    std::string host_;
    std::unique_ptr<ProofVerifier> proof_verifier_;
    SocketFd socket_ = kInvalidSocketFd;
    bssl::UniquePtr<SSL> ssl_;
    std::unique_ptr<MasqueH2Connection> connection_;
    bool mtls_ = false;
  };
  struct PendingRequest {
    Message request;
    MasqueH2Connection* connection = nullptr;  // Not owned.
    int32_t stream_id = -1;
  };

  absl::StatusOr<MasqueConnectionPool::ConnectionState*>
  GetOrCreateConnectionState(const std::string& authority, bool mtls);
  void AttachConnectionToPendingRequests(const std::string& authority,
                                         MasqueH2Connection* connection);
  void SendPendingRequests(MasqueH2Connection* connection);
  void FailPendingRequests(MasqueH2Connection* connection,
                           const absl::Status& error);
  quiche::QuicheSocketAddress LookupAddress(absl::string_view host,
                                            absl::string_view port);

  QuicEventLoop* event_loop_;        // Not owned.
  SSL_CTX* tls_ssl_ctx_;             // Not owned.
  SSL_CTX* mtls_ssl_ctx_ = nullptr;  // Not owned.
  const bool disable_certificate_verification_;
  const DnsConfig dns_config_;
  Visitor* visitor_;  // Not owned.
  absl::flat_hash_map<std::string, std::unique_ptr<ConnectionState>>
      connections_;
  absl::flat_hash_map<RequestId, std::unique_ptr<PendingRequest>>
      pending_requests_;
  RequestId next_request_id_ = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CONNECTION_POOL_H_
