// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This binary contains minimal code to create an HTTP/2 server with TLS and
// TCP. It will be refactored to allow layering, with the goal of being able to
// use MASQUE over HTTP/2, and CONNECT in our MASQUE code.

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/bio.h"
#include "openssl/hpke.h"
#include "openssl/ssl.h"
#include "openssl/x509.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/masque/masque_connection_pool.h"
#include "quiche/quic/masque/masque_h2_connection.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/binary_http/binary_http_message.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_ip_address_family.h"
#include "quiche/common/quiche_socket_address.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, port, 9661,
                                "The port the MASQUE server will listen on.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, server_certificate_file, "",
                                "Path to the certificate chain.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, server_key_file, "",
                                "Path to the pkcs8 private key.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, client_root_ca_file, "",
                                "Path to the PEM file containing root CAs.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, ohttp_key, "",
    "Hex-encoded bytes of the OHTTP HPKE private key.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, gateway_path, "",
    "Enables and configures an OHTTP gateway. Sets the path at which the "
    "gateway will respond to both key requests and encapsulated requests. "
    "Example: \"/.well-known/ohttp-gateway\".");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, relay, "",
    "Enables and configures an OHTTP relay. The format is a list of (path, "
    "URL) pairs where each local path is relayed to the corresponding URL, "
    "formatted as \"path1>url1|path2>url2\". For example: "
    "\"/foo>https://foo.example:8443/|/bar>https://example.com/bar\".");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, key_proxy, "",
    "Enables and configures proxying of OHTTP key requests. The format is a "
    "list of (path, URL) pairs where each local path is proxied to the "
    "corresponding "
    "URL, formatted as \"path1>url1|path2>url2\". For example: "
    "\"/foo>https://foo.example:8443/|/bar>https://example.com/bar\".");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int, address_family, 0,
                                "IP address family to use. Must be 0, 4 or 6. "
                                "Defaults to 0 which means any.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, client_cert_file, "",
                                "Path to the client certificate chain.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, client_cert_key_file, "",
    "Path to the pkcs8 client certificate private key.");

using quiche::BinaryHttpRequest;
using quiche::BinaryHttpResponse;
using quiche::ObliviousHttpGateway;
using quiche::ObliviousHttpHeaderKeyConfig;
using quiche::ObliviousHttpKeyConfigs;
using quiche::ObliviousHttpRequest;
using quiche::ObliviousHttpResponse;

namespace quic {

namespace {

absl::string_view RemoveParameters(absl::string_view value) {
  std::vector<absl::string_view> split =
      absl::StrSplit(value, absl::MaxSplits(';', 1));
  absl::string_view without_params = split[0];
  quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&without_params);
  return without_params;
}

bool ListHeaderContainsValue(absl::string_view header,
                             absl::string_view value) {
  std::vector<absl::string_view> header_split = absl::StrSplit(header, ',');
  for (absl::string_view header_value : header_split) {
    if (RemoveParameters(header_value) == value) {
      return true;
    }
  }
  return false;
}

class MasqueOhttpGateway {
 public:
  class Visitor {
   public:
    virtual ~Visitor() = default;
    virtual void SavePendingGatewayRequest(
        MasqueH2Connection* connection, int32_t stream_id,
        MasqueConnectionPool::RequestId request_id,
        ObliviousHttpRequest::Context&& ohttp_context) = 0;
  };

  static std::unique_ptr<MasqueOhttpGateway> Create(
      const std::string& ohttp_key) {
    auto ohttp_gateway = absl::WrapUnique(new MasqueOhttpGateway());
    if (!ohttp_gateway->Setup(ohttp_key).ok()) {
      return nullptr;
    }
    return ohttp_gateway;
  }

  absl::Status HandleRequest(MasqueConnectionPool* pool,
                             MasqueH2Connection* connection, int32_t stream_id,
                             const std::string& encapsulated_request) {
    if (!ohttp_gateway_.has_value()) {
      QUICHE_LOG(ERROR) << "Not ready to handle OHTTP request";
      return absl::InternalError("Not ready to handle OHTTP request");
    }
    absl::StatusOr<ObliviousHttpRequest> decrypted_request =
        ohttp_gateway_->DecryptObliviousHttpRequest(encapsulated_request);
    QUICHE_RETURN_IF_ERROR(decrypted_request.status());
    absl::StatusOr<BinaryHttpRequest> binary_request =
        BinaryHttpRequest::Create(decrypted_request->GetPlaintextData());
    QUICHE_RETURN_IF_ERROR(binary_request.status());
    const BinaryHttpRequest::ControlData& control_data =
        binary_request->control_data();

    MasqueConnectionPool::Message request;
    request.headers[":method"] = control_data.method;
    request.headers[":scheme"] = control_data.scheme;
    request.headers[":authority"] = control_data.authority;
    request.headers[":path"] = control_data.path;
    request.body = binary_request->body();
    absl::StatusOr<MasqueConnectionPool::RequestId> request_id =
        pool->SendRequest(request);
    QUICHE_RETURN_IF_ERROR(request_id.status());
    QUICHE_LOG(INFO) << "Sent decapsulated request";
    visitor_->SavePendingGatewayRequest(
        connection, stream_id, *request_id,
        std::move(*decrypted_request).ReleaseContext());
    return absl::OkStatus();
  }

  absl::StatusOr<MasqueConnectionPool::Message> HandleResponse(
      MasqueConnectionPool::Message&& response,
      ObliviousHttpRequest::Context&& ohttp_context) {
    if (!ohttp_gateway_.has_value()) {
      return absl::InternalError("Not ready to handle OHTTP response");
    }
    auto status_pair = response.headers.find(":status");
    if (status_pair == response.headers.end()) {
      return absl::InternalError("Response is missing status code");
    }
    int status_code;
    if (!absl::SimpleAtoi(status_pair->second, &status_code)) {
      return absl::InternalError(
          absl::StrCat("Failed to parse status code: ", status_pair->second));
    }
    BinaryHttpResponse binary_response(status_code);
    for (const auto& [key, value] : response.headers) {
      if (key != ":status") {
        binary_response.AddHeaderField({std::string(key), std::string(value)});
      }
    }
    binary_response.swap_body(response.body);
    absl::StatusOr<std::string> encoded_response = binary_response.Serialize();
    QUICHE_RETURN_IF_ERROR(encoded_response.status());

    absl::StatusOr<ObliviousHttpResponse> ohttp_response =
        ohttp_gateway_->CreateObliviousHttpResponse(*encoded_response,
                                                    ohttp_context);
    QUICHE_RETURN_IF_ERROR(ohttp_response.status());
    MasqueConnectionPool::Message outer_response;
    outer_response.headers[":status"] = "200";
    outer_response.headers["content-type"] = "message/ohttp-res";
    outer_response.body = ohttp_response->EncapsulateAndSerialize();
    return outer_response;
  }

  std::string concatenated_keys() const { return concatenated_keys_; }
  void set_visitor(Visitor* visitor) { visitor_ = visitor; }

 private:
  MasqueOhttpGateway() = default;

  absl::Status Setup(const std::string& ohttp_key) {
    hpke_key_.reset(EVP_HPKE_KEY_new());
    if (!ohttp_key.empty()) {
      if (!absl::HexStringToBytes(ohttp_key, &hpke_private_key_)) {
        return absl::InvalidArgumentError(
            "OHTTP key is not a valid hex string");
      }
      if (EVP_HPKE_KEY_init(
              hpke_key_.get(), kem_,
              reinterpret_cast<const uint8_t*>(hpke_private_key_.data()),
              hpke_private_key_.size()) != 1) {
        return absl::InternalError("Failed to ingest HPKE key");
      }
    } else {
      if (EVP_HPKE_KEY_generate(hpke_key_.get(), kem_) != 1) {
        return absl::InternalError("Failed to generate new HPKE key");
      }
      size_t private_key_len = EVP_HPKE_KEM_private_key_len(kem_);
      hpke_private_key_ = std::string(private_key_len, '0');
      if (EVP_HPKE_KEY_private_key(
              hpke_key_.get(),
              reinterpret_cast<uint8_t*>(hpke_private_key_.data()),
              &private_key_len, private_key_len) != 1 ||
          private_key_len != hpke_private_key_.size()) {
        return absl::InternalError("Failed to extract new HPKE private key");
      }
      QUICHE_LOG(INFO) << "Generated new HPKE private key: "
                       << absl::BytesToHexString(hpke_private_key_);
    }
    size_t public_key_len = EVP_HPKE_KEM_public_key_len(kem_);
    hpke_public_key_ = std::string(public_key_len, '0');
    if (EVP_HPKE_KEY_public_key(
            hpke_key_.get(),
            reinterpret_cast<uint8_t*>(hpke_public_key_.data()),
            &public_key_len, public_key_len) != 1 ||
        public_key_len != hpke_public_key_.size()) {
      return absl::InternalError("Failed to extract new HPKE public key");
    }
    static constexpr uint8_t kOhttpKeyId = 0x01;
    static constexpr uint16_t kOhttpKemId = EVP_HPKE_DHKEM_X25519_HKDF_SHA256;
    static constexpr uint16_t kOhttpKdfId = EVP_HPKE_HKDF_SHA256;
    static constexpr uint16_t kOhttpAeadId = EVP_HPKE_AES_128_GCM;
    absl::StatusOr<ObliviousHttpHeaderKeyConfig> ohttp_header_key_config =
        ObliviousHttpHeaderKeyConfig::Create(kOhttpKeyId, kOhttpKemId,
                                             kOhttpKdfId, kOhttpAeadId);
    QUICHE_RETURN_IF_ERROR(ohttp_header_key_config.status());
    QUICHE_LOG(INFO) << "Using OHTTP header key config: "
                     << ohttp_header_key_config->DebugString();
    absl::StatusOr<ObliviousHttpKeyConfigs> ohttp_key_configs =
        ObliviousHttpKeyConfigs::Create(*ohttp_header_key_config,
                                        hpke_public_key_);
    QUICHE_RETURN_IF_ERROR(ohttp_key_configs.status());
    QUICHE_LOG(INFO) << "Using OHTTP key configs: " << std::endl
                     << ohttp_key_configs->DebugString();
    absl::StatusOr<std::string> concatenated_keys =
        ohttp_key_configs->GenerateConcatenatedKeys();
    QUICHE_RETURN_IF_ERROR(concatenated_keys.status());
    concatenated_keys_ = *concatenated_keys;
    absl::StatusOr<ObliviousHttpGateway> ohttp_gateway =
        ObliviousHttpGateway::Create(hpke_private_key_,
                                     *ohttp_header_key_config);
    QUICHE_RETURN_IF_ERROR(ohttp_gateway.status());
    ohttp_gateway_.emplace(std::move(*ohttp_gateway));
    return absl::OkStatus();
  }

  Visitor* visitor_ = nullptr;
  std::string hpke_private_key_;
  std::string hpke_public_key_;
  const EVP_HPKE_KEM* kem_ = EVP_hpke_x25519_hkdf_sha256();
  bssl::UniquePtr<EVP_HPKE_KEY> hpke_key_;
  std::string concatenated_keys_;
  std::optional<ObliviousHttpGateway> ohttp_gateway_;
};

static int SelectAlpnCallback(SSL* /*ssl*/, const uint8_t** out,
                              uint8_t* out_len, const uint8_t* in,
                              unsigned in_len, void* /*arg*/) {
  unsigned i = 0;
  while (i < in_len) {
    uint8_t alpn_length = in[i];
    if (i + alpn_length > in_len) {
      // Client sent a malformed ALPN extension.
      break;
    }
    if (alpn_length == 2 && in[i + 1] == 'h' && in[i + 2] == '2') {
      // Found "h2".
      *out = in + i + 1;
      *out_len = alpn_length;
      return SSL_TLSEXT_ERR_OK;
    }
    i += alpn_length + 1;
  }
  *out = nullptr;
  *out_len = 0;
  return SSL_TLSEXT_ERR_ALERT_FATAL;
}

class MasqueH2SocketConnection : public QuicSocketEventListener {
 public:
  explicit MasqueH2SocketConnection(SocketFd connected_socket,
                                    QuicEventLoop* event_loop, SSL_CTX* ctx,
                                    bool is_server,
                                    MasqueH2Connection::Visitor* visitor)
      : socket_(connected_socket),
        event_loop_(event_loop),
        connection_(CreateSsl(ctx), is_server, visitor) {
    if (!event_loop_->RegisterSocket(socket_, kSocketEventReadable, this)) {
      QUICHE_LOG(FATAL)
          << "Failed to register connection socket with the event loop";
    }
  }

  ~MasqueH2SocketConnection() {
    if (socket_ != kInvalidSocketFd) {
      if (!event_loop_->UnregisterSocket(socket_)) {
        QUICHE_LOG(ERROR) << "Failed to unregister socket";
      }
      close(socket_);
      socket_ = kInvalidSocketFd;
    }
  }

  bool Start() {
    connection_.OnTransportReadable();
    return !connection_.aborted();
  }

  // From QuicSocketEventListener.
  void OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                     QuicSocketEventMask events) {
    auto cleanup = absl::MakeCleanup([this, event_loop, fd]() {
      if (!event_loop->SupportsEdgeTriggered() && !connection_.aborted()) {
        if (!event_loop->RearmSocket(
                fd, kSocketEventReadable | kSocketEventWritable)) {
          QUICHE_LOG(FATAL) << "Failed to re-arm socket " << fd;
        }
      }
    });
    if (fd != socket_ || ((events & kSocketEventReadable) == 0)) {
      return;
    }
    connection_.OnTransportReadable();
  }

  MasqueH2Connection* connection() { return &connection_; }

 private:
  SSL* CreateSsl(SSL_CTX* ctx) {
    ssl_.reset(SSL_new(ctx));
    SSL_set_accept_state(ssl_.get());
    BIO* bio = BIO_new_socket(socket_, BIO_CLOSE);
    SSL_set_bio(ssl_.get(), bio, bio);
    // `SSL_set_bio` causes `ssl_` to take ownership of `bio`.
    return ssl_.get();
  }

  SocketFd socket_;
  bssl::UniquePtr<SSL> ssl_;
  QuicEventLoop* event_loop_;  // Unowned.
  MasqueH2Connection connection_;
};

class MasqueTcpServer : public QuicSocketEventListener,
                        public MasqueH2Connection::Visitor,
                        public MasqueConnectionPool::Visitor,
                        public MasqueOhttpGateway::Visitor {
 public:
  using RequestId = MasqueConnectionPool::RequestId;
  using Message = MasqueConnectionPool::Message;

  explicit MasqueTcpServer(SSL_CTX* client_ssl_ctx,
                           bool disable_certificate_verification,
                           const MasqueConnectionPool::DnsConfig& dns_config)
      : event_loop_(GetDefaultEventLoop()->Create(QuicDefaultClock::Get())),
        connection_pool_(event_loop_.get(), client_ssl_ctx,
                         disable_certificate_verification, dns_config, this) {}

  MasqueTcpServer(const MasqueTcpServer&) = delete;
  MasqueTcpServer(MasqueTcpServer&&) = delete;
  MasqueTcpServer& operator=(const MasqueTcpServer&) = delete;
  MasqueTcpServer& operator=(MasqueTcpServer&&) = delete;

  ~MasqueTcpServer() {
    if (server_socket_ != kInvalidSocketFd) {
      if (!event_loop_->UnregisterSocket(server_socket_)) {
        QUICHE_LOG(ERROR) << "Failed to unregister socket";
      }
      close(server_socket_);
      server_socket_ = kInvalidSocketFd;
    }
  }

  bool SetupSslCtx(const std::string& server_certificate_file,
                   const std::string& server_key_file,
                   const std::string& client_root_ca_file) {
    ctx_.reset(SSL_CTX_new(TLS_method()));

    if (!SSL_CTX_use_PrivateKey_file(ctx_.get(), server_key_file.c_str(),
                                     SSL_FILETYPE_PEM)) {
      QUICHE_LOG(ERROR) << "Failed to load private key: " << server_key_file;
      return false;
    }
    if (!SSL_CTX_use_certificate_chain_file(ctx_.get(),
                                            server_certificate_file.c_str())) {
      QUICHE_LOG(ERROR) << "Failed to load cert chain: "
                        << server_certificate_file;
      return false;
    }
    if (!client_root_ca_file.empty()) {
      X509_STORE* store = SSL_CTX_get_cert_store(ctx_.get());
      if (store == nullptr) {
        QUICHE_LOG(ERROR) << "Failed to get certificate store";
        return false;
      }
      if (X509_STORE_load_locations(store, client_root_ca_file.c_str(),
                                    /*dir=*/nullptr) != 1) {
        QUICHE_LOG(ERROR) << "Failed to load client root CA file: "
                          << client_root_ca_file;
        return false;
      }
      SSL_CTX_set_verify(ctx_.get(),
                         SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                         /*callback=*/nullptr);
    }

    SSL_CTX_set_alpn_select_cb(ctx_.get(), &SelectAlpnCallback, this);

    SSL_CTX_set_min_proto_version(ctx_.get(), TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_mode(ctx_.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    return true;
  }

  bool SetupSocket(uint16_t server_port) {
    if (server_socket_ != kInvalidSocketFd) {
      QUICHE_LOG(ERROR) << "Socket already set up";
      return false;
    }

    absl::StatusOr<SocketFd> create_result = socket_api::CreateSocket(
        quiche::IpAddressFamily::IP_V6, socket_api::SocketProtocol::kTcp,
        /*blocking=*/false);
    if (!create_result.ok() || create_result.value() == kInvalidSocketFd) {
      QUICHE_LOG(ERROR) << "Failed to create socket: "
                        << create_result.status();
      return false;
    }
    server_socket_ = create_result.value();

    const int enable = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&enable, sizeof(enable)) < 0) {
      QUICHE_LOG(ERROR) << "Failed to set SO_REUSEADDR on socket";
      return false;
    }

    absl::Status bind_result = socket_api::Bind(
        server_socket_, quiche::QuicheSocketAddress(
                            quiche::QuicheIpAddress::Any6(), server_port));
    if (!bind_result.ok()) {
      QUICHE_LOG(ERROR) << "Failed to bind socket: " << bind_result;
      return false;
    }

    absl::Status listen_result = socket_api::Listen(server_socket_, SOMAXCONN);
    if (!listen_result.ok()) {
      QUICHE_LOG(ERROR) << "Failed to listen on socket: " << listen_result;
      return false;
    }

    if (!event_loop_->RegisterSocket(server_socket_, kSocketEventReadable,
                                     this)) {
      QUICHE_LOG(ERROR) << "Failed to register socket with the event loop";
      return false;
    }

    QUICHE_LOG(INFO) << "Started listening on port " << server_port;
    return true;
  }

  void Run() {
    while (true) {
      event_loop_->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
    }
  }

  void OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                     QuicSocketEventMask events) override {
    if (fd != server_socket_ || ((events & kSocketEventReadable) == 0)) {
      return;
    }
    AcceptConnection();
    if (!event_loop->SupportsEdgeTriggered()) {
      if (!event_loop->RearmSocket(server_socket_, kSocketEventReadable)) {
        QUICHE_LOG(FATAL) << "Failed to re-arm socket " << server_socket_;
      }
    }
  }

  // From MasqueH2Connection::Visitor.
  void OnConnectionReady(MasqueH2Connection* /*connection*/) override {}
  void OnConnectionFinished(MasqueH2Connection* connection,
                            absl::Status /*error*/) override {
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [connection](const auto& socket_connection) {
                         return socket_connection->connection() == connection;
                       }),
        connections_.end());
  }

  absl::Status HandleOhttpGatewayRequest(
      MasqueH2Connection* connection, int32_t stream_id,
      const std::string& encapsulated_request) {
    return masque_ohttp_gateway_->HandleRequest(
        &connection_pool_, connection, stream_id, encapsulated_request);
  }

  absl::Status HandleOhttpRelayRequest(MasqueH2Connection* connection,
                                       int32_t stream_id,
                                       const std::string& encapsulated_request,
                                       const QuicUrl& relay_gateway_url) {
    Message request;
    request.headers[":method"] = "POST";
    request.headers[":scheme"] = relay_gateway_url.scheme();
    request.headers[":authority"] = relay_gateway_url.HostPort();
    request.headers[":path"] = relay_gateway_url.PathParamsQuery();
    request.headers["content-type"] = "message/ohttp-req";
    request.body = encapsulated_request;
    absl::StatusOr<RequestId> request_id =
        connection_pool_.SendRequest(request);
    QUICHE_RETURN_IF_ERROR(request_id.status());
    QUICHE_LOG(INFO) << "Sent relayed request";
    PendingRequest pending_request;
    pending_request.connection = connection;
    pending_request.stream_id = stream_id;
    pending_requests_.insert({*request_id, std::move(pending_request)});
    return absl::OkStatus();
  }

  absl::Status HandleOhttpKeyProxyRequest(MasqueH2Connection* connection,
                                          int32_t stream_id,
                                          const QuicUrl& key_proxy_url) {
    Message request;
    request.headers[":method"] = "GET";
    request.headers[":scheme"] = key_proxy_url.scheme();
    request.headers[":authority"] = key_proxy_url.HostPort();
    request.headers[":path"] = key_proxy_url.PathParamsQuery();
    request.headers["accept"] = "application/ohttp-keys";
    absl::StatusOr<RequestId> request_id =
        connection_pool_.SendRequest(request);
    QUICHE_RETURN_IF_ERROR(request_id.status());
    QUICHE_LOG(INFO) << "Sent relayed request";
    PendingRequest pending_request;
    pending_request.connection = connection;
    pending_request.stream_id = stream_id;
    pending_requests_.insert({*request_id, std::move(pending_request)});
    return absl::OkStatus();
  }

  void OnRequest(MasqueH2Connection* connection, int32_t stream_id,
                 const quiche::HttpHeaderBlock& headers,
                 const std::string& body) override {
    quiche::HttpHeaderBlock response_headers;
    std::string response_body;
    auto path_pair = headers.find(":path");
    auto method_pair = headers.find(":method");
    if (path_pair == headers.end() || method_pair == headers.end()) {
      // This should never happen because the h2 adapter should have rejected
      // the request, but handle it gracefully just in case.
      response_headers[":status"] = "400";
      response_body = "Request missing pseudo-headers";
      connection->SendResponse(stream_id, response_headers, response_body);
      return;
    }
    std::vector<absl::string_view> path_parts =
        absl::StrSplit(path_pair->second, absl::MaxSplits('?', 1));
    absl::string_view path = path_parts[0];
    absl::string_view content_type;
    auto content_type_pair = headers.find("content-type");
    if (content_type_pair != headers.end()) {
      content_type = RemoveParameters(content_type_pair->second);
    }
    auto accept_pair = headers.find("accept");
    if (!gateway_path_.empty() && path == gateway_path_ &&
        masque_ohttp_gateway_ && method_pair->second == "GET" &&
        (accept_pair == headers.end() ||
         ListHeaderContainsValue(accept_pair->second,
                                 "application/ohttp-keys"))) {
      response_headers[":status"] = "200";
      response_headers["content-type"] = "application/ohttp-keys";
      response_body = masque_ohttp_gateway_->concatenated_keys();
    } else if (auto key_proxy_pair = key_proxy_urls_.find(path);
               key_proxy_pair != key_proxy_urls_.end() &&
               method_pair->second == "GET" &&
               (accept_pair == headers.end() ||
                ListHeaderContainsValue(accept_pair->second,
                                        "application/ohttp-keys")) &&
               body.empty()) {
      absl::Status status = HandleOhttpKeyProxyRequest(connection, stream_id,
                                                       key_proxy_pair->second);
      if (status.ok()) {
        return;
      } else {
        QUICHE_LOG(ERROR) << "Failed to handle OHTTP key proxy request for "
                          << path << ": " << status;
        response_headers[":status"] = "500";
        response_body = status.message();
      }
    } else if (auto relay_pair = relay_gateway_urls_.find(path);
               relay_pair != relay_gateway_urls_.end() &&
               method_pair->second == "POST" &&
               content_type == "message/ohttp-req") {
      absl::Status status = HandleOhttpRelayRequest(connection, stream_id, body,
                                                    relay_pair->second);
      if (status.ok()) {
        return;
      } else {
        QUICHE_LOG(ERROR) << "Failed to handle OHTTP relay request for " << path
                          << ": " << status;
        response_headers[":status"] = "500";
        response_body = status.message();
      }
    } else if (!gateway_path_.empty() && path == gateway_path_ &&
               method_pair->second == "POST" &&
               content_type == "message/ohttp-req") {
      absl::Status status =
          HandleOhttpGatewayRequest(connection, stream_id, body);
      if (status.ok()) {
        return;
      } else {
        response_headers[":status"] = "500";
        QUICHE_LOG(ERROR) << "Failed to handle OHTTP gateway request: "
                          << status;
        response_body = status.message();
      }
    } else if (method_pair->second == "GET" && path == "/") {
      response_headers[":status"] = "200";
      response_body = "<h1>This is a response body</h1>";
    } else {
      response_headers[":status"] = "404";
      response_body = "Path not found";
    }
    connection->SendResponse(stream_id, response_headers, response_body);
  }

  void OnResponse(MasqueH2Connection* /*connection*/, int32_t /*stream_id*/,
                  const quiche::HttpHeaderBlock& /*headers*/,
                  const std::string& /*body*/) override {
    QUICHE_LOG(FATAL) << "Server cannot receive responses";
  }

  void OnStreamFailure(MasqueH2Connection* /*connection*/, int32_t stream_id,
                       absl::Status error) override {
    QUICHE_LOG(ERROR) << "Stream " << stream_id << " failed: " << error;
  }

  // From MasqueConnectionPool::Visitor.
  void OnPoolResponse(MasqueConnectionPool* /*pool*/, RequestId request_id,
                      absl::StatusOr<Message>&& response) override {
    auto it = pending_requests_.find(request_id);
    if (it == pending_requests_.end()) {
      QUICHE_LOG(ERROR) << "Received unexpected response for unknown request "
                        << request_id;
      return;
    }
    PendingRequest pending_request = std::move(it->second);
    pending_requests_.erase(it);
    quiche::HttpHeaderBlock response_headers;
    std::string response_body;
    if (response.ok()) {
      if (pending_request.ohttp_context.has_value()) {
        absl::StatusOr<MasqueConnectionPool::Message> gateway_response =
            masque_ohttp_gateway_->HandleResponse(
                std::move(*response),
                std::move(*pending_request.ohttp_context));
        if (!gateway_response.ok()) {
          response_headers[":status"] = "500";
          response_body = absl::StrCat("Failed to handle gateway response: ",
                                       gateway_response.status().message());
          QUICHE_LOG(ERROR) << response_body;
        } else {
          response_headers = std::move(gateway_response->headers);
          response_body = std::move(gateway_response->body);
          QUICHE_LOG(INFO) << "Sending OHTTP response";
        }
      } else {
        QUICHE_LOG(INFO) << "Forwarding relayed response to stream ID "
                         << pending_request.stream_id;
        response_headers = std::move(response->headers);
        response_body = std::move(response->body);
      }
    } else {
      QUICHE_LOG(ERROR) << "Received relayed error response: "
                        << response.status();
      response_headers[":status"] = "500";
      response_body =
          absl::StrCat("Relayed request failed: ", response.status().message());
    }
    pending_request.connection->SendResponse(pending_request.stream_id,
                                             response_headers, response_body);
    pending_request.connection->AttemptToSend();
  }

  bool SetupGateway(const std::string& gateway_path,
                    MasqueOhttpGateway* gateway) {
    if (gateway_path.empty() != (gateway == nullptr)) {
      QUICHE_LOG(ERROR) << "Invalid gateway configuration";
      return false;
    }
    gateway_path_ = gateway_path;
    masque_ohttp_gateway_ = gateway;
    if (masque_ohttp_gateway_) {
      masque_ohttp_gateway_->set_visitor(this);
    }
    return true;
  }

  bool SetupRelay(const std::string& relay) {
    if (relay.empty()) {
      return true;
    }
    std::vector<absl::string_view> relay_split = absl::StrSplit(relay, '|');
    for (absl::string_view relay_param : relay_split) {
      std::vector<absl::string_view> relay_param_split =
          absl::StrSplit(relay_param, '>');
      if (relay_param_split.size() != 2) {
        QUICHE_LOG(ERROR) << "Invalid relay parameter: \"" << relay_param
                          << "\". It should be in the format of \"path>url\"";
        return false;
      }
      absl::string_view path = relay_param_split[0];
      absl::string_view gateway_url = relay_param_split[1];
      auto [it, inserted] = relay_gateway_urls_.insert(
          {std::string(path), QuicUrl(gateway_url, "https")});
      if (!inserted) {
        QUICHE_LOG(ERROR) << "Duplicate relay path: \"" << path << "\"";
        return false;
      }
    }
    return true;
  }

  bool SetupKeyProxy(const std::string& key_proxy) {
    if (key_proxy.empty()) {
      return true;
    }
    std::vector<absl::string_view> key_proxy_split =
        absl::StrSplit(key_proxy, '|');
    for (absl::string_view key_proxy_param : key_proxy_split) {
      std::vector<absl::string_view> key_proxy_param_split =
          absl::StrSplit(key_proxy_param, '>');
      if (key_proxy_param_split.size() != 2) {
        QUICHE_LOG(ERROR) << "Invalid key proxy parameter: \""
                          << key_proxy_param
                          << "\". It should be in the format of \"path>url\"";
        return false;
      }
      absl::string_view path = key_proxy_param_split[0];
      absl::string_view key_proxy_url = key_proxy_param_split[1];
      auto [it, inserted] = key_proxy_urls_.insert(
          {std::string(path), QuicUrl(key_proxy_url, "https")});
      if (!inserted) {
        QUICHE_LOG(ERROR) << "Duplicate relay path: \"" << path << "\"";
        return false;
      }
      QUICHE_LOG(INFO) << "Added key proxy for " << path << ": "
                       << key_proxy_url;
    }
    return true;
  }

  void SavePendingGatewayRequest(
      MasqueH2Connection* connection, int32_t stream_id,
      MasqueConnectionPool::RequestId request_id,
      ObliviousHttpRequest::Context&& ohttp_context) override {
    PendingRequest pending_request;
    pending_request.connection = connection;
    pending_request.stream_id = stream_id;
    pending_request.ohttp_context = std::move(ohttp_context);
    pending_requests_.insert({request_id, std::move(pending_request)});
  }

 private:
  struct PendingRequest {
    MasqueH2Connection* connection = nullptr;  // Not owned.
    int32_t stream_id = -1;
    std::optional<ObliviousHttpRequest::Context> ohttp_context;
  };

  void AcceptConnection() {
    absl::StatusOr<socket_api::AcceptResult> accept_result =
        socket_api::Accept(server_socket_, /*blocking=*/false);
    if (!accept_result.ok()) {
      QUICHE_LOG(ERROR) << "Failed to accept connection: "
                        << accept_result.status();
      return;
    }
    QUICHE_LOG(INFO) << "Accepted TCP connection from "
                     << accept_result.value().peer_address;

    // `connection` takes ownership of the socket.
    auto connection = std::make_unique<MasqueH2SocketConnection>(
        accept_result.value().fd, event_loop_.get(), ctx_.get(),
        /*is_server=*/true, this);
    if (!connection->Start()) {
      QUICHE_LOG(ERROR) << "Failed to start connection handler from "
                        << accept_result.value().peer_address;
      return;
    }
    QUICHE_LOG(INFO) << "Started connection from "
                     << accept_result.value().peer_address;
    connections_.push_back(std::move(connection));
  }

  std::unique_ptr<QuicEventLoop> event_loop_;
  bssl::UniquePtr<SSL_CTX> ctx_;
  std::string gateway_path_;
  MasqueOhttpGateway* masque_ohttp_gateway_;  // Unowned.
  SocketFd server_socket_ = kInvalidSocketFd;
  std::vector<std::unique_ptr<MasqueH2SocketConnection>> connections_;
  // Maps from local paths to remote gateway URLs.
  absl::flat_hash_map<std::string, QuicUrl> relay_gateway_urls_;
  // Maps from local paths to remote key fetch URLs.
  absl::flat_hash_map<std::string, QuicUrl> key_proxy_urls_;
  MasqueConnectionPool connection_pool_;
  absl::flat_hash_map<RequestId, PendingRequest> pending_requests_;
};

int RunMasqueTcpServer(int argc, char* argv[]) {
  const char* usage = "Usage: masque_server [options]";
  std::vector<std::string> non_option_args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (!non_option_args.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  std::string server_certificate_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_server_certificate_file);
  if (server_certificate_file.empty()) {
    QUICHE_LOG(ERROR) << "--server_certificate_file cannot be empty";
    return 1;
  }
  std::string server_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_server_key_file);
  if (server_key_file.empty()) {
    QUICHE_LOG(ERROR) << "--server_key_file cannot be empty";
    return 1;
  }
  std::string client_root_ca_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_root_ca_file);

  quiche::QuicheSystemEventLoop system_event_loop("masque_tcp_server");

  std::unique_ptr<MasqueOhttpGateway> masque_ohttp_gateway;
  std::string gateway_path =
      quiche::GetQuicheCommandLineFlag(FLAGS_gateway_path);
  if (!gateway_path.empty()) {
    masque_ohttp_gateway = MasqueOhttpGateway::Create(
        quiche::GetQuicheCommandLineFlag(FLAGS_ohttp_key));
    if (!masque_ohttp_gateway) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP gateway";
      return 1;
    }
  }

  const bool disable_certificate_verification =
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification);
  const std::string client_cert_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_file);
  const std::string client_cert_key_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_key_file);
  absl::StatusOr<bssl::UniquePtr<SSL_CTX>> client_ssl_ctx =
      MasqueConnectionPool::CreateSslCtx(client_cert_file,
                                         client_cert_key_file);
  if (!client_ssl_ctx.ok()) {
    QUICHE_LOG(ERROR) << "Failed to create client SSL context: "
                      << client_ssl_ctx.status();
    return 1;
  }
  MasqueConnectionPool::DnsConfig dns_config;
  absl::Status address_family_status = dns_config.SetAddressFamily(
      quiche::GetQuicheCommandLineFlag(FLAGS_address_family));
  if (!address_family_status.ok()) {
    QUICHE_LOG(ERROR) << address_family_status;
    return 1;
  }

  MasqueTcpServer server(client_ssl_ctx->get(),
                         disable_certificate_verification, dns_config);

  if (!server.SetupGateway(gateway_path, masque_ohttp_gateway.get())) {
    QUICHE_LOG(ERROR) << "Invalid gateway configuration";
    return 1;
  }
  if (!server.SetupRelay(quiche::GetQuicheCommandLineFlag(FLAGS_relay))) {
    QUICHE_LOG(ERROR) << "Invalid --relay input";
    return 1;
  }
  if (!server.SetupKeyProxy(
          quiche::GetQuicheCommandLineFlag(FLAGS_key_proxy))) {
    QUICHE_LOG(ERROR) << "Invalid --key_proxy input";
    return 1;
  }
  if (!server.SetupSslCtx(server_certificate_file, server_key_file,
                          client_root_ca_file)) {
    QUICHE_LOG(ERROR) << "Failed to setup SSL context";
    return 1;
  }
  if (!server.SetupSocket(quiche::GetQuicheCommandLineFlag(FLAGS_port))) {
    QUICHE_LOG(ERROR) << "Failed to setup socket";
    return 1;
  }
  server.Run();

  return 0;
}

}  // namespace
}  // namespace quic

int main(int argc, char* argv[]) {
  return quic::RunMasqueTcpServer(argc, argv);
}
