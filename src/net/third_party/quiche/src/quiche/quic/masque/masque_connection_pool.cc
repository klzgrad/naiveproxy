// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_connection_pool.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "openssl/base.h"
#include "openssl/bio.h"
#include "openssl/pool.h"
#include "openssl/ssl.h"
#include "openssl/stack.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/masque/masque_h2_connection.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_socket_address.h"

namespace quic {

MasqueConnectionPool::MasqueConnectionPool(
    QuicEventLoop *event_loop, SSL_CTX *ssl_ctx,
    bool disable_certificate_verification, int address_family_for_lookup,
    Visitor *visitor)
    : event_loop_(event_loop),
      ssl_ctx_(ssl_ctx),
      disable_certificate_verification_(disable_certificate_verification),
      address_family_for_lookup_(address_family_for_lookup),
      visitor_(visitor) {}

void MasqueConnectionPool::OnConnectionReady(MasqueH2Connection *connection) {
  SendPendingRequests(connection);
}

void MasqueConnectionPool::OnConnectionFinished(
    MasqueH2Connection *connection) {
  FailPendingRequests(
      connection,
      absl::InternalError("Connection finished before receiving request"));
}

void MasqueConnectionPool::OnRequest(
    MasqueH2Connection * /*connection*/, int32_t /*stream_id*/,
    const quiche::HttpHeaderBlock & /*headers*/, const std::string & /*body*/) {
  QUICHE_LOG(FATAL) << "Client cannot receive requests";
}

void MasqueConnectionPool::OnResponse(MasqueH2Connection *connection,
                                      int32_t stream_id,
                                      const quiche::HttpHeaderBlock &headers,
                                      const std::string &body) {
  bool found = false;
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest &pending_request = *it->second;
    if (pending_request.connection == connection &&
        pending_request.stream_id == stream_id) {
      pending_requests_.erase(it++);
      Message response;
      response.headers = headers.Clone();
      response.body = body;
      visitor_->OnResponse(this, request_id, std::move(response));
      found = true;
      break;
    }
    ++it;
  }
  if (!found) {
    QUICHE_LOG(ERROR) << "Received unexpected response for unknown request: "
                      << headers.DebugString();
  }
}

absl::StatusOr<MasqueConnectionPool::RequestId>
MasqueConnectionPool::SendRequest(const Message &request) {
  auto authority = request.headers.find(":authority");
  if (authority == request.headers.end()) {
    return absl::InvalidArgumentError("Request missing :authority header");
  }
  ConnectionState *connection =
      GetOrCreateConnectionState(std::string(authority->second));
  if (connection == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failed to create connection to ", authority->second));
  }
  auto pending_request = std::make_unique<PendingRequest>();
  if (connection->connection() != nullptr) {
    pending_request->connection = connection->connection();
    pending_request->stream_id =
        connection->connection()->SendRequest(request.headers, request.body);
    if (pending_request->stream_id < 0) {
      return absl::InternalError(
          absl::StrCat("Failed to send request to ", authority->second));
    }
  }
  RequestId request_id = ++next_request_id_;
  pending_request->request.headers = request.headers.Clone();
  pending_request->request.body = request.body;
  pending_requests_.insert({request_id, std::move(pending_request)});
  return request_id;
}

MasqueConnectionPool::ConnectionState *
MasqueConnectionPool::GetOrCreateConnectionState(const std::string &authority) {
  auto connection_state_it = connections_.find(authority);
  if (connection_state_it != connections_.end()) {
    return connection_state_it->second.get();
  }
  auto connection_state = std::make_unique<ConnectionState>(this);
  if (!connection_state->SetupSocket(authority,
                                     disable_certificate_verification_,
                                     address_family_for_lookup_)) {
    QUICHE_LOG(ERROR) << "Failed to setup socket for " << authority;
    return nullptr;
  }
  return connections_.insert({authority, std::move(connection_state)})
      .first->second.get();
}

void MasqueConnectionPool::AttachConnectionToPendingRequests(
    const std::string &authority, MasqueH2Connection *connection) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();
       ++it) {
    PendingRequest &pending_request = *it->second;
    auto authority_header = pending_request.request.headers.find(":authority");
    if (authority_header == pending_request.request.headers.end()) {
      QUICHE_LOG(ERROR) << "Request missing :authority header";
      continue;
    }
    if (authority_header->second != authority) {
      continue;
    }
    pending_request.connection = connection;
  }
}

void MasqueConnectionPool::SendPendingRequests(MasqueH2Connection *connection) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest &pending_request = *it->second;
    if (pending_request.connection != connection) {
      ++it;
      continue;
    }
    int32_t stream_id = connection->SendRequest(pending_request.request.headers,
                                                pending_request.request.body);
    if (stream_id < 0) {
      QUICHE_LOG(ERROR) << "Failed to send request";
      visitor_->OnResponse(this, request_id,
                           absl::InternalError("Failed to send request"));
      pending_requests_.erase(it++);
      continue;
    }
    pending_request.stream_id = stream_id;
    ++it;
  }
}

void MasqueConnectionPool::FailPendingRequests(MasqueH2Connection *connection,
                                               const absl::Status &error) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest &pending_request = *it->second;
    if (pending_request.connection != connection) {
      ++it;
      continue;
    }
    visitor_->OnResponse(this, request_id, error);
    pending_requests_.erase(it++);
  }
}

MasqueConnectionPool::ConnectionState::ConnectionState(
    MasqueConnectionPool *connection_pool)
    : connection_pool_(connection_pool) {}

MasqueConnectionPool::ConnectionState::~ConnectionState() {
  if (socket_ != kInvalidSocketFd) {
    if (!connection_pool_->event_loop()->UnregisterSocket(socket_)) {
      QUICHE_LOG(ERROR) << "Failed to unregister socket";
    }
    if (!socket_api::Close(socket_).ok()) {
      QUICHE_LOG(ERROR) << "Error while closing socket";
    }
    socket_ = kInvalidSocketFd;
  }
}

bool MasqueConnectionPool::ConnectionState::SetupSocket(
    const std::string &authority, bool disable_certificate_verification,
    int address_family_for_lookup) {
  authority_ = authority;
  std::vector<std::string> authority_split =
      absl::StrSplit(authority_, absl::MaxSplits(':', 1));
  std::string port;
  if (authority_split.size() == 2) {
    host_ = authority_split[0];
    port = authority_split[1];
  } else {
    host_ = authority_split[0];
    port = "443";
  }
  quiche::QuicheSocketAddress socket_address =
      tools::LookupAddress(address_family_for_lookup, host_, port);
  if (!socket_address.IsInitialized()) {
    QUICHE_LOG(ERROR) << "Failed to resolve address for \"" << authority_
                      << "\"";
    return false;
  }
  absl::StatusOr<SocketFd> create_result = socket_api::CreateSocket(
      socket_address.host().address_family(), socket_api::SocketProtocol::kTcp,
      /*blocking=*/false);
  if (!create_result.ok() || create_result.value() == kInvalidSocketFd) {
    QUICHE_LOG(ERROR) << "Failed to create socket: " << create_result.status();
    return false;
  }
  socket_ = create_result.value();
  // Ignore result because asynchronous connect is expected to fail.
  (void)socket_api::Connect(socket_, socket_address);
  if (!connection_pool_->event_loop()->RegisterSocket(
          socket_, kSocketEventReadable | kSocketEventWritable, this)) {
    QUICHE_LOG(ERROR) << "Failed to register socket with the event loop";
    return false;
  }
  QUICHE_LOG(INFO) << "Socket connect in progress to " << socket_address;

  if (disable_certificate_verification) {
    proof_verifier_ = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier_ = CreateDefaultProofVerifier(host_);
  }
  return true;
}

void MasqueConnectionPool::ConnectionState::OnSocketEvent(
    QuicEventLoop * /*event_loop*/, SocketFd fd, QuicSocketEventMask events) {
  if (fd != socket_) {
    return;
  }
  if (connection_ && ((events & kSocketEventReadable) != 0)) {
    connection_->OnTransportReadable();
  }
  if ((events & kSocketEventWritable) != 0) {
    if (!ssl_) {
      ssl_.reset((SSL_new(connection_pool_->ssl_ctx())));
      SSL_set_connect_state(ssl_.get());

      if (SSL_set_app_data(ssl_.get(), this) != 1) {
        QUICHE_LOG(FATAL) << "SSL_set_app_data failed";
      }
      SSL_set_custom_verify(ssl_.get(), SSL_VERIFY_PEER, &VerifyCallback);

      if (SSL_set_tlsext_host_name(ssl_.get(), host_.c_str()) != 1) {
        QUICHE_LOG(FATAL) << "SSL_set_tlsext_host_name failed";
      }

      static constexpr uint8_t kAlpnProtocols[] = {
          0x02, 'h', '2',  // h2
      };
      if (SSL_set_alpn_protos(ssl_.get(), kAlpnProtocols,
                              sizeof(kAlpnProtocols)) != 0) {
        QUICHE_LOG(FATAL) << "SSL_set_alpn_protos failed";
      }
      BIO *bio = BIO_new_socket(socket_, BIO_CLOSE);
      SSL_set_bio(ssl_.get(), bio, bio);
      // `SSL_set_bio` causes `ssl_` to take ownership of `bio`.
      connection_ = std::make_unique<MasqueH2Connection>(
          ssl_.get(), /*is_server=*/false, connection_pool_);
      connection_->OnTransportReadable();
      connection_pool_->AttachConnectionToPendingRequests(authority_,
                                                          connection_.get());
    }
    connection_->AttemptToSend();
  }
}

// static
enum ssl_verify_result_t MasqueConnectionPool::ConnectionState::VerifyCallback(
    SSL *ssl, uint8_t *out_alert) {
  return static_cast<MasqueConnectionPool::ConnectionState *>(
             SSL_get_app_data(ssl))
      ->VerifyCertificate(ssl, out_alert);
}

enum ssl_verify_result_t
MasqueConnectionPool::ConnectionState::VerifyCertificate(SSL *ssl,
                                                         uint8_t *out_alert) {
  const STACK_OF(CRYPTO_BUFFER) *cert_chain = SSL_get0_peer_certificates(ssl);
  if (cert_chain == nullptr) {
    QUICHE_LOG(ERROR) << "No certificate chain";
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  std::vector<std::string> certs;
  for (CRYPTO_BUFFER *cert : cert_chain) {
    certs.push_back(
        std::string(reinterpret_cast<const char *>(CRYPTO_BUFFER_data(cert)),
                    CRYPTO_BUFFER_len(cert)));
  }
  const uint8_t *ocsp_response_raw;
  size_t ocsp_response_len;
  SSL_get0_ocsp_response(ssl, &ocsp_response_raw, &ocsp_response_len);
  std::string ocsp_response(reinterpret_cast<const char *>(ocsp_response_raw),
                            ocsp_response_len);
  const uint8_t *sct_list_raw;
  size_t sct_list_len;
  SSL_get0_signed_cert_timestamp_list(ssl, &sct_list_raw, &sct_list_len);
  std::string cert_sct(reinterpret_cast<const char *>(sct_list_raw),
                       sct_list_len);
  std::string error_details;
  std::unique_ptr<ProofVerifyDetails> details;
  QuicAsyncStatus verify_status = proof_verifier_->VerifyCertChain(
      host_, /*port=*/443, certs, ocsp_response, cert_sct,
      /*context=*/nullptr, &error_details, &details, out_alert,
      /*callback=*/nullptr);
  if (verify_status != QUIC_SUCCESS) {
    // TODO(dschinazi) properly handle QUIC_PENDING.
    QUICHE_LOG(ERROR) << "Failed to verify certificate"
                      << (verify_status == QUIC_PENDING ? " (pending)" : "")
                      << ": " << error_details;
    return ssl_verify_invalid;
  }
  QUICHE_LOG(INFO) << "Successfully verified certificate";
  return ssl_verify_ok;
}

// static
absl::StatusOr<bssl::UniquePtr<SSL_CTX>> MasqueConnectionPool::CreateSslCtx(
    const std::string &client_cert_file,
    const std::string &client_cert_key_file) {
  if (client_cert_file.empty() != client_cert_key_file.empty()) {
    return absl::InvalidArgumentError(
        "Both private key and certificate chain are required when using client "
        "certificates");
  }
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));

  if (!client_cert_key_file.empty() &&
      !SSL_CTX_use_PrivateKey_file(ctx.get(), client_cert_key_file.c_str(),
                                   SSL_FILETYPE_PEM)) {
    QUICHE_LOG(ERROR) << "Failed to load client certificate private key: "
                      << client_cert_key_file;
    return absl::InternalError(
        absl::StrCat("Failed to load client certificate private key: ",
                     client_cert_key_file));
  }
  if (!client_cert_file.empty() && !SSL_CTX_use_certificate_chain_file(
                                       ctx.get(), client_cert_file.c_str())) {
    QUICHE_LOG(ERROR) << "Failed to load client certificate chain: "
                      << client_cert_file;
    return absl::InternalError(absl::StrCat(
        "Failed to load client certificate chain: ", client_cert_file));
  }

  SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION);

  return ctx;
}

}  // namespace quic
