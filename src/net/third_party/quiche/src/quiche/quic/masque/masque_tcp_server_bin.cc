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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
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
#include "quiche/quic/masque/masque_h2_connection.h"
#include "quiche/binary_http/binary_http_message.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_ip_address_family.h"
#include "quiche/common/quiche_socket_address.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, port, 9661,
                                "The port the MASQUE server will listen on.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, certificate_file, "",
                                "Path to the certificate chain.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, key_file, "",
                                "Path to the pkcs8 private key.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, client_root_ca_file, "",
                                "Path to the PEM file containing root CAs.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, ohttp_key, "",
    "Hex-encoded bytes of the OHTTP HPKE private key.");

using quiche::BinaryHttpRequest;
using quiche::BinaryHttpResponse;
using quiche::ObliviousHttpGateway;
using quiche::ObliviousHttpHeaderKeyConfig;
using quiche::ObliviousHttpKeyConfigs;
using quiche::ObliviousHttpRequest;
using quiche::ObliviousHttpResponse;

namespace quic {

namespace {

class MasqueOhttpGateway {
 public:
  MasqueOhttpGateway() {}

  bool Setup(const std::string &ohttp_key) {
    hpke_key_.reset(EVP_HPKE_KEY_new());
    if (!ohttp_key.empty()) {
      if (!absl::HexStringToBytes(ohttp_key, &hpke_private_key_)) {
        QUICHE_LOG(ERROR) << "OHTTP key is not a valid hex string";
        return false;
      }
      if (EVP_HPKE_KEY_init(
              hpke_key_.get(), kem_,
              reinterpret_cast<const uint8_t *>(hpke_private_key_.data()),
              hpke_private_key_.size()) != 1) {
        QUICHE_LOG(ERROR) << "Failed to ingest HPKE key";
        return false;
      }
    } else {
      if (EVP_HPKE_KEY_generate(hpke_key_.get(), kem_) != 1) {
        QUICHE_LOG(ERROR) << "Failed to generate new HPKE key";
        return false;
      }
      size_t private_key_len = EVP_HPKE_KEM_private_key_len(kem_);
      hpke_private_key_ = std::string(private_key_len, '0');
      if (EVP_HPKE_KEY_private_key(
              hpke_key_.get(),
              reinterpret_cast<uint8_t *>(hpke_private_key_.data()),
              &private_key_len, private_key_len) != 1 ||
          private_key_len != hpke_private_key_.size()) {
        QUICHE_LOG(ERROR) << "Failed to extract new HPKE private key";
        return false;
      }
      QUICHE_LOG(INFO) << "Generated new HPKE private key: "
                       << absl::BytesToHexString(hpke_private_key_);
    }
    size_t public_key_len = EVP_HPKE_KEM_public_key_len(kem_);
    hpke_public_key_ = std::string(public_key_len, '0');
    if (EVP_HPKE_KEY_public_key(
            hpke_key_.get(),
            reinterpret_cast<uint8_t *>(hpke_public_key_.data()),
            &public_key_len, public_key_len) != 1 ||
        public_key_len != hpke_public_key_.size()) {
      QUICHE_LOG(ERROR) << "Failed to extract new HPKE public key";
      return false;
    }
    static constexpr uint8_t kOhttpKeyId = 0x01;
    static constexpr uint16_t kOhttpKemId = EVP_HPKE_DHKEM_X25519_HKDF_SHA256;
    static constexpr uint16_t kOhttpKdfId = EVP_HPKE_HKDF_SHA256;
    static constexpr uint16_t kOhttpAeadId = EVP_HPKE_AES_128_GCM;
    absl::StatusOr<ObliviousHttpHeaderKeyConfig> ohttp_header_key_config =
        ObliviousHttpHeaderKeyConfig::Create(kOhttpKeyId, kOhttpKemId,
                                             kOhttpKdfId, kOhttpAeadId);
    if (!ohttp_header_key_config.ok()) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP header key config: "
                        << ohttp_header_key_config.status();
      return false;
    }
    QUICHE_LOG(INFO) << "Using OHTTP header key config: "
                     << ohttp_header_key_config->DebugString();
    absl::StatusOr<ObliviousHttpKeyConfigs> ohttp_key_configs =
        ObliviousHttpKeyConfigs::Create(*ohttp_header_key_config,
                                        hpke_public_key_);
    if (!ohttp_key_configs.ok()) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP key configs: "
                        << ohttp_key_configs.status();
      return false;
    }
    QUICHE_LOG(INFO) << "Using OHTTP key configs: " << std::endl
                     << ohttp_key_configs->DebugString();
    absl::StatusOr<std::string> concatenated_keys =
        ohttp_key_configs->GenerateConcatenatedKeys();
    if (!concatenated_keys.ok()) {
      QUICHE_LOG(ERROR) << "Failed to generate concatenated keys: "
                        << concatenated_keys.status();
      return false;
    }
    concatenated_keys_ = *concatenated_keys;
    absl::StatusOr<ObliviousHttpGateway> ohttp_gateway =
        ObliviousHttpGateway::Create(hpke_private_key_,
                                     *ohttp_header_key_config);
    if (!ohttp_gateway.ok()) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP gateway: "
                        << ohttp_gateway.status();
      return false;
    }
    ohttp_gateway_.emplace(std::move(*ohttp_gateway));
    return true;
  }

  bool HandleRequest(MasqueH2Connection *connection, int32_t stream_id,
                     const std::string &encapsulated_request) {
    if (!ohttp_gateway_.has_value()) {
      QUICHE_LOG(ERROR) << "Not ready to handle OHTTP request";
      return false;
    }
    absl::StatusOr<ObliviousHttpRequest> decrypted_request =
        ohttp_gateway_->DecryptObliviousHttpRequest(encapsulated_request);
    if (!decrypted_request.ok()) {
      QUICHE_LOG(ERROR) << "Failed to decrypt OHTTP request: "
                        << decrypted_request.status();
      return false;
    }
    absl::StatusOr<BinaryHttpRequest> binary_request =
        BinaryHttpRequest::Create(decrypted_request->GetPlaintextData());
    if (!binary_request.ok()) {
      QUICHE_LOG(ERROR) << "Failed to parse binary request: "
                        << binary_request.status();
      return false;
    }
    const BinaryHttpRequest::ControlData &control_data =
        binary_request->control_data();
    // TODO(dschinazi): Send the decapsulated request to the authority instead
    // of replying with a fake local response.
    absl::string_view request_body = binary_request->body();
    std::string response_body = absl::StrCat(
        "OHTTP Response! Request method: ", control_data.method,
        " scheme: ", control_data.scheme, " path: ", control_data.path,
        " authority: ", control_data.authority, " body: \"", request_body,
        "\"");

    BinaryHttpResponse binary_response(/*status_code=*/200);
    binary_response.swap_body(response_body);
    absl::StatusOr<std::string> encoded_response = binary_response.Serialize();
    if (!encoded_response.ok()) {
      QUICHE_LOG(ERROR) << "Failed to encode response: "
                        << encoded_response.status();
      return false;
    }

    auto context = std::move(*decrypted_request).ReleaseContext();
    absl::StatusOr<ObliviousHttpResponse> ohttp_response =
        ohttp_gateway_->CreateObliviousHttpResponse(*encoded_response, context);
    if (!ohttp_response.ok()) {
      QUICHE_LOG(ERROR) << "Failed to create OHTTP response: "
                        << ohttp_response.status();
      return false;
    }
    std::string encapsulated_response =
        ohttp_response->EncapsulateAndSerialize();
    QUICHE_LOG(INFO) << "Sending OHTTP response";

    quiche::HttpHeaderBlock response_headers;
    response_headers[":status"] = "200";
    response_headers["content-type"] = "message/ohttp-res";
    connection->SendResponse(stream_id, response_headers,
                             encapsulated_response);
    return true;
  }

  std::string concatenated_keys() const { return concatenated_keys_; }

 private:
  std::string hpke_private_key_;
  std::string hpke_public_key_;
  const EVP_HPKE_KEM *kem_ = EVP_hpke_x25519_hkdf_sha256();
  bssl::UniquePtr<EVP_HPKE_KEY> hpke_key_;
  std::string concatenated_keys_;
  std::optional<ObliviousHttpGateway> ohttp_gateway_;
};

static int SelectAlpnCallback(SSL * /*ssl*/, const uint8_t **out,
                              uint8_t *out_len, const uint8_t *in,
                              unsigned in_len, void * /*arg*/) {
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
                                    QuicEventLoop *event_loop, SSL_CTX *ctx,
                                    bool is_server,
                                    MasqueH2Connection::Visitor *visitor)
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
  void OnSocketEvent(QuicEventLoop * /*event_loop*/, SocketFd fd,
                     QuicSocketEventMask events) {
    if (fd != socket_ || ((events & kSocketEventReadable) == 0)) {
      return;
    }
    connection_.OnTransportReadable();
  }

  MasqueH2Connection *connection() { return &connection_; }

 private:
  SSL *CreateSsl(SSL_CTX *ctx) {
    ssl_.reset(SSL_new(ctx));
    SSL_set_accept_state(ssl_.get());
    BIO *bio = BIO_new_socket(socket_, BIO_CLOSE);
    SSL_set_bio(ssl_.get(), bio, bio);
    // `SSL_set_bio` causes `ssl_` to take ownership of `bio`.
    return ssl_.get();
  }

  SocketFd socket_;
  bssl::UniquePtr<SSL> ssl_;
  QuicEventLoop *event_loop_;  // Unowned.
  MasqueH2Connection connection_;
};

class MasqueTcpServer : public QuicSocketEventListener,
                        public MasqueH2Connection::Visitor {
 public:
  explicit MasqueTcpServer(MasqueOhttpGateway *masque_ohttp_gateway)
      : event_loop_(GetDefaultEventLoop()->Create(QuicDefaultClock::Get())),
        masque_ohttp_gateway_(masque_ohttp_gateway) {}

  MasqueTcpServer(const MasqueTcpServer &) = delete;
  MasqueTcpServer(MasqueTcpServer &&) = delete;
  MasqueTcpServer &operator=(const MasqueTcpServer &) = delete;
  MasqueTcpServer &operator=(MasqueTcpServer &&) = delete;

  ~MasqueTcpServer() {
    if (server_socket_ != kInvalidSocketFd) {
      if (!event_loop_->UnregisterSocket(server_socket_)) {
        QUICHE_LOG(ERROR) << "Failed to unregister socket";
      }
      close(server_socket_);
      server_socket_ = kInvalidSocketFd;
    }
  }

  bool SetupSslCtx(const std::string &certificate_file,
                   const std::string &key_file,
                   const std::string &client_root_ca_file) {
    ctx_.reset(SSL_CTX_new(TLS_method()));

    if (!SSL_CTX_use_PrivateKey_file(ctx_.get(), key_file.c_str(),
                                     SSL_FILETYPE_PEM)) {
      QUICHE_LOG(ERROR) << "Failed to load private key: " << key_file;
      return false;
    }
    if (!SSL_CTX_use_certificate_chain_file(ctx_.get(),
                                            certificate_file.c_str())) {
      QUICHE_LOG(ERROR) << "Failed to load cert chain: " << certificate_file;
      return false;
    }
    if (!client_root_ca_file.empty()) {
      X509_STORE *store = SSL_CTX_get_cert_store(ctx_.get());
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
                   (const char *)&enable, sizeof(enable)) < 0) {
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

  void OnSocketEvent(QuicEventLoop * /*event_loop*/, SocketFd fd,
                     QuicSocketEventMask events) override {
    if (fd != server_socket_ || ((events & kSocketEventReadable) == 0)) {
      return;
    }
    AcceptConnection();
  }

  // From MasqueH2Connection::Visitor.
  void OnConnectionReady(MasqueH2Connection * /*connection*/) override {}
  void OnConnectionFinished(MasqueH2Connection *connection) override {
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [connection](const auto &socket_connection) {
                         return socket_connection->connection() == connection;
                       }),
        connections_.end());
  }

  bool HandleOhttpRequest(MasqueH2Connection *connection, int32_t stream_id,
                          const std::string &encapsulated_request) {
    return masque_ohttp_gateway_->HandleRequest(connection, stream_id,
                                                encapsulated_request);
  }

  void OnRequest(MasqueH2Connection *connection, int32_t stream_id,
                 const quiche::HttpHeaderBlock &headers,
                 const std::string &body) override {
    quiche::HttpHeaderBlock response_headers;
    std::string response_body;
    auto path_pair = headers.find(":path");
    auto method_pair = headers.find(":method");
    auto content_type_pair = headers.find("content-type");
    if (path_pair == headers.end() || method_pair == headers.end()) {
      // This should never happen because the h2 adapter should have rejected
      // the request, but handle it gracefully just in case.
      response_headers[":status"] = "400";
      response_body = "Request missing pseudo-headers";
    } else if (method_pair->second == "GET" &&
               content_type_pair != headers.end() &&
               content_type_pair->second == "application/ohttp-keys") {
      response_headers[":status"] = "200";
      response_headers["content-type"] = "application/ohttp-keys";
      response_body = masque_ohttp_gateway_->concatenated_keys();
    } else if (method_pair->second == "POST" &&
               content_type_pair != headers.end() &&
               content_type_pair->second == "message/ohttp-req") {
      if (HandleOhttpRequest(connection, stream_id, body)) {
        return;
      } else {
        response_headers[":status"] = "500";
        response_body = "Failed to handle OHTTP request";
      }
    } else if (method_pair->second == "GET" && path_pair->second == "/") {
      response_headers[":status"] = "200";
      response_body = "<h1>This is a response body</h1>";
    } else {
      response_headers[":status"] = "404";
      response_body = "Path not found";
    }
    connection->SendResponse(stream_id, response_headers, response_body);
  }

  void OnResponse(MasqueH2Connection * /*connection*/, int32_t /*stream_id*/,
                  const quiche::HttpHeaderBlock & /*headers*/,
                  const std::string & /*body*/) override {
    QUICHE_LOG(FATAL) << "Server cannot receive responses";
  }

 private:
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
  MasqueOhttpGateway *masque_ohttp_gateway_;  // Unowned.
  SocketFd server_socket_ = kInvalidSocketFd;
  std::vector<std::unique_ptr<MasqueH2SocketConnection>> connections_;
};

int RunMasqueTcpServer(int argc, char *argv[]) {
  const char *usage = "Usage: masque_server [options]";
  std::vector<std::string> non_option_args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (!non_option_args.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  std::string certificate_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_certificate_file);
  if (certificate_file.empty()) {
    QUICHE_LOG(ERROR) << "--certificate_file cannot be empty";
    return 1;
  }
  std::string key_file = quiche::GetQuicheCommandLineFlag(FLAGS_key_file);
  if (key_file.empty()) {
    QUICHE_LOG(ERROR) << "--key_file cannot be empty";
    return 1;
  }
  std::string client_root_ca_file =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_root_ca_file);

  quiche::QuicheSystemEventLoop system_event_loop("masque_tcp_server");

  MasqueOhttpGateway masque_ohttp_gateway;
  if (!masque_ohttp_gateway.Setup(
          quiche::GetQuicheCommandLineFlag(FLAGS_ohttp_key))) {
    QUICHE_LOG(ERROR) << "Failed to setup OHTTP";
    return 1;
  }

  MasqueTcpServer server(&masque_ohttp_gateway);
  if (!server.SetupSslCtx(certificate_file, key_file, client_root_ca_file)) {
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

int main(int argc, char *argv[]) {
  return quic::RunMasqueTcpServer(argc, argv);
}
