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

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
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
#include "quiche/common/quiche_status_utils.h"

namespace quic {

namespace {

// Default DNS resolver that uses getaddrinfo().
class DefaultDnsResolver : public MasqueConnectionPool::DnsResolver {
 public:
  quiche::QuicheSocketAddress LookupAddress(
      int address_family_for_lookup, absl::string_view host,
      absl::string_view port) const override {
    return tools::LookupAddress(address_family_for_lookup, std::string(host),
                                std::string(port));
  }

  static DefaultDnsResolver* Get() {
    static DefaultDnsResolver resolver;
    return &resolver;
  }
};

}  // namespace

// static
int16_t MasqueConnectionPool::GetStatusCode(const Message& message) {
  auto it = message.headers.find(":status");
  if (it == message.headers.end()) {
    return 0;
  }
  int16_t status_code = 0;
  if (!absl::SimpleAtoi(it->second, &status_code)) {
    return 0;
  }
  return status_code;
}

quiche::QuicheSocketAddress MasqueConnectionPool::LookupAddress(
    absl::string_view host, absl::string_view port) {
  const DnsResolver* dns_resolver = dns_config_.resolver();
  if (dns_resolver == nullptr) {
    dns_resolver = DefaultDnsResolver::Get();
  }
  dns_config_.ApplyOverrides(&host, &port);
  return dns_resolver->LookupAddress(dns_config_.address_family_for_lookup(),
                                     host, port);
}

absl::Status MasqueConnectionPool::DnsConfig::SetAddressFamily(
    int address_family) {
  if (address_family == 0) {
    address_family_for_lookup_ = AF_UNSPEC;
  } else if (address_family == 4) {
    address_family_for_lookup_ = AF_INET;
  } else if (address_family == 6) {
    address_family_for_lookup_ = AF_INET6;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid address_family ", address_family));
  }
  return absl::OkStatus();
}

absl::Status MasqueConnectionPool::DnsConfig::SetOverrides(
    const std::string& overrides) {
  if (overrides.empty()) {
    return absl::OkStatus();
  }
  std::vector<absl::string_view> overrides_split =
      absl::StrSplit(overrides, ';');
  for (absl::string_view override : overrides_split) {
    std::vector<absl::string_view> override_split =
        absl::StrSplit(override, ':');
    if (override_split.size() < 3 || override_split.size() > 4) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid override: \"", override, "\""));
    }
    absl::string_view input_host = override_split[0];
    absl::string_view input_port = override_split[1];
    absl::string_view output_host = override_split[2];
    absl::string_view output_port =
        override_split.size() > 3 ? override_split[3] : "";
    auto [it, inserted] = overrides_.insert(
        {std::make_pair(std::string(input_host), std::string(input_port)),
         std::make_pair(std::string(output_host), std::string(output_port))});
    if (!inserted) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicate override entry: \"", input_host, ":", input_port, "\""));
    }
  }
  return absl::OkStatus();
}

void MasqueConnectionPool::DnsConfig::ApplyOverrides(
    absl::string_view* host, absl::string_view* port) const {
  for (const auto& [input, output] : overrides_) {
    if ((input.first == *host || input.first.empty()) &&
        (input.second == *port || input.second.empty())) {
      *host = output.first;
      if (!output.second.empty()) {
        *port = output.second;
      }
      return;
    }
  }
}

MasqueConnectionPool::MasqueConnectionPool(
    QuicEventLoop* event_loop, SSL_CTX* ssl_ctx,
    bool disable_certificate_verification, const DnsConfig& dns_config,
    Visitor* visitor)
    : event_loop_(event_loop),
      tls_ssl_ctx_(ssl_ctx),
      disable_certificate_verification_(disable_certificate_verification),
      dns_config_(dns_config),
      visitor_(visitor) {}

void MasqueConnectionPool::OnConnectionReady(MasqueH2Connection* connection) {
  SendPendingRequests(connection);
}

void MasqueConnectionPool::OnConnectionFinished(MasqueH2Connection* connection,
                                                absl::Status error) {
  FailPendingRequests(
      connection,
      error.ok() ? absl::InternalError(
                       "Connection finished before receiving complete response")
                 : error);
}

void MasqueConnectionPool::OnRequest(MasqueH2Connection* /*connection*/,
                                     int32_t /*stream_id*/,
                                     const quiche::HttpHeaderBlock& /*headers*/,
                                     const std::string& /*body*/) {
  QUICHE_LOG(FATAL) << "Client cannot receive requests";
}

void MasqueConnectionPool::OnResponse(MasqueH2Connection* connection,
                                      int32_t stream_id,
                                      const quiche::HttpHeaderBlock& headers,
                                      const std::string& body) {
  bool found = false;
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest& pending_request = *it->second;
    if (pending_request.connection == connection &&
        pending_request.stream_id == stream_id) {
      pending_requests_.erase(it++);
      Message response;
      response.headers = headers.Clone();
      response.body = body;
      visitor_->OnPoolResponse(this, request_id, std::move(response));
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

void MasqueConnectionPool::OnStreamFailure(MasqueH2Connection* connection,
                                           int32_t stream_id,
                                           absl::Status error) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest& pending_request = *it->second;
    if (pending_request.connection == connection &&
        pending_request.stream_id == stream_id) {
      pending_requests_.erase(it++);
      visitor_->OnPoolResponse(this, request_id, error);
      break;
    }
    ++it;
  }
}

absl::StatusOr<MasqueConnectionPool::RequestId>
MasqueConnectionPool::SendRequest(const Message& request, bool mtls) {
  auto authority = request.headers.find(":authority");
  if (authority == request.headers.end()) {
    return absl::InvalidArgumentError("Request missing :authority header");
  }
  QUICHE_ASSIGN_OR_RETURN(
      ConnectionState * connection,
      GetOrCreateConnectionState(std::string(authority->second), mtls));
  auto pending_request = std::make_unique<PendingRequest>();
  if (connection->connection() != nullptr) {
    QUICHE_LOG(INFO) << "Reusing existing connection to " << authority->second;
    pending_request->connection = connection->connection();
    pending_request->stream_id =
        connection->connection()->SendRequest(request.headers, request.body);
    if (pending_request->stream_id < 0) {
      return absl::InternalError(
          absl::StrCat("Failed to send request to ", authority->second));
    }
    connection->connection()->AttemptToSend();
  } else {
    QUICHE_LOG(INFO) << "No existing connection to " << authority->second;
  }
  RequestId request_id = ++next_request_id_;
  pending_request->request.headers = request.headers.Clone();
  pending_request->request.body = request.body;
  pending_requests_.insert({request_id, std::move(pending_request)});
  return request_id;
}

absl::StatusOr<MasqueConnectionPool::ConnectionState*>
MasqueConnectionPool::GetOrCreateConnectionState(const std::string& authority,
                                                 bool mtls) {
  std::string entry = absl::StrCat((mtls ? "m" : ""), "tls:", authority);
  auto connection_state_it = connections_.find(entry);
  if (connection_state_it != connections_.end()) {
    return connection_state_it->second.get();
  }
  auto connection_state = std::make_unique<ConnectionState>(this);
  connection_state->set_mtls(mtls);
  QUICHE_RETURN_IF_ERROR(connection_state->SetupSocket(
      authority, disable_certificate_verification_));
  return connections_.insert({entry, std::move(connection_state)})
      .first->second.get();
}

void MasqueConnectionPool::AttachConnectionToPendingRequests(
    const std::string& authority, MasqueH2Connection* connection) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();
       ++it) {
    PendingRequest& pending_request = *it->second;
    auto authority_header = pending_request.request.headers.find(":authority");
    if (authority_header == pending_request.request.headers.end()) {
      QUICHE_LOG(ERROR) << "Request missing :authority header";
      continue;
    }
    if (authority_header->second != authority) {
      continue;
    }
    QUICHE_LOG(INFO) << "Attaching connection to pending request for "
                     << authority;
    pending_request.connection = connection;
  }
}

void MasqueConnectionPool::SendPendingRequests(MasqueH2Connection* connection) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest& pending_request = *it->second;
    if (pending_request.connection != connection) {
      ++it;
      continue;
    }
    QUICHE_LOG(INFO) << "Sending pending request ID " << request_id;
    int32_t stream_id = connection->SendRequest(pending_request.request.headers,
                                                pending_request.request.body);
    if (stream_id < 0) {
      QUICHE_LOG(ERROR) << "Failed to send request";
      visitor_->OnPoolResponse(this, request_id,
                               absl::InternalError("Failed to send request"));
      pending_requests_.erase(it++);
      continue;
    }
    connection->AttemptToSend();
    pending_request.stream_id = stream_id;
    ++it;
  }
}

void MasqueConnectionPool::FailPendingRequests(MasqueH2Connection* connection,
                                               const absl::Status& error) {
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    RequestId request_id = it->first;
    PendingRequest& pending_request = *it->second;
    if (pending_request.connection != connection) {
      ++it;
      continue;
    }
    visitor_->OnPoolResponse(this, request_id, error);
    pending_requests_.erase(it++);
  }
}

MasqueConnectionPool::ConnectionState::ConnectionState(
    MasqueConnectionPool* connection_pool)
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

absl::Status MasqueConnectionPool::ConnectionState::SetupSocket(
    const std::string& authority, bool disable_certificate_verification) {
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
      connection_pool_->LookupAddress(host_, port);
  if (!socket_address.IsInitialized()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to resolve address for \"", authority_, "\""));
  }
  absl::StatusOr<SocketFd> create_result = socket_api::CreateSocket(
      socket_address.host().address_family(), socket_api::SocketProtocol::kTcp,
      /*blocking=*/false);
  if (!create_result.ok() || create_result.value() == kInvalidSocketFd) {
    return absl::InternalError(absl::StrCat("Failed to create socket: ",
                                            create_result.status().message()));
  }
  socket_ = create_result.value();
  // Ignore result because asynchronous connect is expected to fail.
  (void)socket_api::Connect(socket_, socket_address);
  if (!connection_pool_->event_loop()->RegisterSocket(
          socket_, kSocketEventReadable | kSocketEventWritable, this)) {
    return absl::InternalError("Failed to register socket with the event loop");
  }
  QUICHE_LOG(INFO) << "Socket fd " << socket_ << " connect in progress to "
                   << socket_address;

  if (disable_certificate_verification) {
    proof_verifier_ = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier_ = CreateDefaultProofVerifier(host_);
    if (!proof_verifier_) {
      QUICHE_LOG(FATAL) << "The default proof verifier is not supported. Pass "
                           "in --disable_certificate_verification.";
    }
  }
  return absl::OkStatus();
}

void MasqueConnectionPool::ConnectionState::OnSocketEvent(
    QuicEventLoop* event_loop, SocketFd fd, QuicSocketEventMask events) {
  auto cleanup = absl::MakeCleanup([this, event_loop, fd]() {
    if (!event_loop->SupportsEdgeTriggered() &&
        (!connection_ || !connection_->aborted())) {
      if (!event_loop->RearmSocket(
              fd, kSocketEventReadable | kSocketEventWritable)) {
        QUICHE_LOG(FATAL) << "Failed to re-arm socket " << fd;
      }
    }
  });

  if (fd != socket_) {
    return;
  }
  if (connection_ && ((events & kSocketEventReadable) != 0)) {
    connection_->OnTransportReadable();
  }
  if ((events & kSocketEventWritable) != 0) {
    if (!ssl_) {
      ssl_.reset((SSL_new(connection_pool_->GetSslCtx(mtls_))));
      SSL_set_connect_state(ssl_.get());

      if (SSL_set_app_data(ssl_.get(), this) != 1) {
        QUICHE_LOG(FATAL) << "SSL_set_app_data failed";
      }
      SSL_set_custom_verify(ssl_.get(), SSL_VERIFY_PEER, &VerifyCallback);

      if (SSL_set_tlsext_host_name(ssl_.get(), host_.c_str()) != 1) {
        QUICHE_LOG(FATAL) << "SSL_set_tlsext_host_name failed";
      }

      static constexpr uint8_t kAlpnProtocols[] = {
          // clang-format off
          0x02, 'h', '2',  // h2
          // clang-format on
      };
      if (SSL_set_alpn_protos(ssl_.get(), kAlpnProtocols,
                              sizeof(kAlpnProtocols)) != 0) {
        QUICHE_LOG(FATAL) << "SSL_set_alpn_protos failed";
      }
      BIO* bio = BIO_new_socket(socket_, BIO_CLOSE);
      SSL_set_bio(ssl_.get(), bio, bio);
      // `SSL_set_bio` causes `ssl_` to take ownership of `bio`.
      connection_ = std::make_unique<MasqueH2Connection>(
          ssl_.get(), /*is_server=*/false, connection_pool_);
      connection_pool_->AttachConnectionToPendingRequests(authority_,
                                                          connection_.get());
      connection_->OnTransportReadable();
    }
    connection_->AttemptToSend();
  }
}

// static
enum ssl_verify_result_t MasqueConnectionPool::ConnectionState::VerifyCallback(
    SSL* ssl, uint8_t* out_alert) {
  return static_cast<MasqueConnectionPool::ConnectionState*>(
             SSL_get_app_data(ssl))
      ->VerifyCertificate(ssl, out_alert);
}

enum ssl_verify_result_t
MasqueConnectionPool::ConnectionState::VerifyCertificate(SSL* ssl,
                                                         uint8_t* out_alert) {
  const STACK_OF(CRYPTO_BUFFER)* cert_chain = SSL_get0_peer_certificates(ssl);
  if (cert_chain == nullptr) {
    QUICHE_LOG(ERROR) << "No certificate chain";
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  std::vector<std::string> certs;
  for (CRYPTO_BUFFER* cert : cert_chain) {
    certs.push_back(
        std::string(reinterpret_cast<const char*>(CRYPTO_BUFFER_data(cert)),
                    CRYPTO_BUFFER_len(cert)));
  }
  const uint8_t* ocsp_response_raw;
  size_t ocsp_response_len;
  SSL_get0_ocsp_response(ssl, &ocsp_response_raw, &ocsp_response_len);
  std::string ocsp_response(reinterpret_cast<const char*>(ocsp_response_raw),
                            ocsp_response_len);
  const uint8_t* sct_list_raw;
  size_t sct_list_len;
  SSL_get0_signed_cert_timestamp_list(ssl, &sct_list_raw, &sct_list_len);
  std::string cert_sct(reinterpret_cast<const char*>(sct_list_raw),
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
    const std::string& client_cert_file,
    const std::string& client_cert_key_file) {
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
  SSL_CTX_set_mode(ctx.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  return ctx;
}

// static
absl::StatusOr<bssl::UniquePtr<SSL_CTX>>
MasqueConnectionPool::CreateSslCtxFromData(
    const std::string& client_cert_pem_data,
    const std::string& client_cert_key_data) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  // Load public cert.
  BIO* cert_bio = BIO_new_mem_buf(client_cert_pem_data.c_str(), -1);
  QUICHE_CHECK(cert_bio);
  X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
  QUICHE_CHECK(cert);
  BIO_free(cert_bio);
  int rv = SSL_CTX_use_certificate(ctx.get(), cert);
  QUICHE_CHECK_EQ(rv, 1);
  X509_free(cert);

  // Load private key.
  BIO* key_bio = BIO_new_mem_buf(client_cert_key_data.c_str(), -1);
  QUICHE_CHECK(key_bio);
  EVP_PKEY* private_key =
      PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
  QUICHE_CHECK(private_key);
  BIO_free(key_bio);
  rv = SSL_CTX_use_PrivateKey(ctx.get(), private_key);
  QUICHE_CHECK_EQ(rv, 1);
  EVP_PKEY_free(private_key);

  SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_mode(ctx.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  return ctx;
}

}  // namespace quic
