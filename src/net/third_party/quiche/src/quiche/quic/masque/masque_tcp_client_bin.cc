// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This binary contains minimal code to send an HTTP/1.1 over TLS over TCP
// request. It will be refactored to allow layering, with the goal of being able
// to use MASQUE over HTTP/2, and CONNECT in our MASQUE code.

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/bio.h"
#include "openssl/pool.h"
#include "openssl/ssl.h"
#include "openssl/stack.h"
#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/event_loop_socket_factory.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/masque/masque_h2_connection.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/common/simple_buffer_allocator.h"

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

namespace quic {
namespace {

std::optional<bssl::UniquePtr<SSL_CTX>> CreateSslCtx(
    const std::string &client_cert_file,
    const std::string &client_cert_key_file) {
  if (client_cert_file.empty() != client_cert_key_file.empty()) {
    QUICHE_LOG(ERROR) << "Both private key and certificate chain are required "
                         "when using client certificates";
    return std::nullopt;
  }
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));

  if (!client_cert_key_file.empty() &&
      !SSL_CTX_use_PrivateKey_file(ctx.get(), client_cert_key_file.c_str(),
                                   SSL_FILETYPE_PEM)) {
    QUICHE_LOG(ERROR) << "Failed to load client certificate private key: "
                      << client_cert_key_file;
    return std::nullopt;
  }
  if (!client_cert_file.empty() && !SSL_CTX_use_certificate_chain_file(
                                       ctx.get(), client_cert_file.c_str())) {
    QUICHE_LOG(ERROR) << "Failed to load client certificate chain: "
                      << client_cert_file;
    return std::nullopt;
  }

  SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ctx.get(), TLS1_3_VERSION);

  return ctx;
}

class MasqueTlsTcpClientHandler : public ConnectingClientSocket::AsyncVisitor,
                                  public MasqueH2Connection::Visitor {
 public:
  explicit MasqueTlsTcpClientHandler(QuicEventLoop *event_loop, SSL_CTX *ctx,
                                     QuicUrl url,
                                     bool disable_certificate_verification,
                                     int address_family_for_lookup)
      : event_loop_(event_loop),
        ctx_(ctx),
        socket_factory_(std::make_unique<EventLoopSocketFactory>(
            event_loop_, quiche::SimpleBufferAllocator::Get())),
        url_(url),
        disable_certificate_verification_(disable_certificate_verification),
        address_family_for_lookup_(address_family_for_lookup) {}

  ~MasqueTlsTcpClientHandler() override {
    if (socket_) {
      socket_->Disconnect();
    }
  }

  bool Start() {
    if (disable_certificate_verification_) {
      proof_verifier_ = std::make_unique<FakeProofVerifier>();
    } else {
      proof_verifier_ = CreateDefaultProofVerifier(url_.host());
    }
    socket_address_ = tools::LookupAddress(
        address_family_for_lookup_, url_.host(), absl::StrCat(url_.port()));
    if (!socket_address_.IsInitialized()) {
      QUICHE_LOG(ERROR) << "Failed to resolve address for \"" << url_.host()
                        << "\"";
      return false;
    }
    socket_ = socket_factory_->CreateTcpClientSocket(socket_address_,
                                                     /*receive_buffer_size=*/0,
                                                     /*send_buffer_size=*/0,
                                                     /*async_visitor=*/this);
    if (!socket_) {
      QUICHE_LOG(ERROR) << "Failed to create TCP socket for "
                        << socket_address_;
      return false;
    }
    socket_->ConnectAsync();
    return true;
  }

  static enum ssl_verify_result_t VerifyCallback(SSL *ssl, uint8_t *out_alert) {
    return static_cast<MasqueTlsTcpClientHandler *>(SSL_get_app_data(ssl))
        ->VerifyCertificate(out_alert);
  }

  enum ssl_verify_result_t VerifyCertificate(uint8_t *out_alert) {
    const STACK_OF(CRYPTO_BUFFER) *cert_chain =
        SSL_get0_peer_certificates(ssl_.get());
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
    SSL_get0_ocsp_response(ssl_.get(), &ocsp_response_raw, &ocsp_response_len);
    std::string ocsp_response(reinterpret_cast<const char *>(ocsp_response_raw),
                              ocsp_response_len);
    const uint8_t *sct_list_raw;
    size_t sct_list_len;
    SSL_get0_signed_cert_timestamp_list(ssl_.get(), &sct_list_raw,
                                        &sct_list_len);
    std::string cert_sct(reinterpret_cast<const char *>(sct_list_raw),
                         sct_list_len);
    std::string error_details;
    std::unique_ptr<ProofVerifyDetails> details;
    QuicAsyncStatus verify_status = proof_verifier_->VerifyCertChain(
        url_.host(), url_.port(), certs, ocsp_response, cert_sct,
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

  // From ConnectingClientSocket::AsyncVisitor.
  void ConnectComplete(absl::Status status) override {
    if (!status.ok()) {
      QUICHE_LOG(ERROR) << "Failed to TCP connect to " << socket_address_
                        << ": " << status;
      done_ = true;
      return;
    }

    QUICHE_LOG(INFO) << "TCP connected to " << socket_address_;

    ssl_.reset((SSL_new(ctx_)));

    if (SSL_set_app_data(ssl_.get(), this) != 1) {
      QUICHE_LOG(FATAL) << "SSL_set_app_data failed";
      return;
    }
    SSL_set_custom_verify(ssl_.get(), SSL_VERIFY_PEER, &VerifyCallback);

    if (SSL_set_tlsext_host_name(ssl_.get(), url_.host().c_str()) != 1) {
      QUICHE_LOG(FATAL) << "SSL_set_tlsext_host_name failed";
      return;
    }

    static constexpr uint8_t kAlpnProtocols[] = {
        0x02, 'h', '2',                                // h2
        0x08, 'h', 't', 't', 'p', '/', '1', '.', '1',  // http/1.1
    };
    if (SSL_set_alpn_protos(ssl_.get(), kAlpnProtocols,
                            sizeof(kAlpnProtocols)) != 0) {
      QUICHE_LOG(FATAL) << "SSL_set_alpn_protos failed";
      return;
    }

    BIO *tls_io = nullptr;
    if (BIO_new_bio_pair(&transport_io_, kBioBufferSize, &tls_io,
                         kBioBufferSize) != 1) {
      QUICHE_LOG(FATAL) << "BIO_new_bio_pair failed";
      return;
    }
    SSL_set_bio(ssl_.get(), tls_io, tls_io);
    BIO_free(tls_io);

    int ret = SSL_connect(ssl_.get());
    if (ret != 1) {
      int ssl_err = SSL_get_error(ssl_.get(), ret);
      if (ssl_err == SSL_ERROR_WANT_READ) {
        QUICHE_DVLOG(1) << "SSL_connect will require another read";
        SendToTransport();
        socket_->ReceiveAsync(kBioBufferSize);
        return;
      }
      PrintSSLError("Error while TLS connecting", ssl_err, ret);
      done_ = true;
      return;
    }
    QUICHE_LOG(INFO) << "TLS connected";

    tls_connected_ = true;
    MaybeSendRequest();
    socket_->ReceiveAsync(kBioBufferSize);
  }

  void ReceiveComplete(absl::StatusOr<quiche::QuicheMemSlice> data) override {
    if (!data.ok()) {
      QUICHE_LOG(ERROR) << "Failed to receive transport data: "
                        << data.status();
      done_ = true;
      return;
    }
    if (data->empty()) {
      QUICHE_LOG(INFO) << "Transport read closed";
      done_ = true;
      return;
    }
    QUICHE_DVLOG(1) << "Transport received " << data->length() << " bytes";
    int write_ret = BIO_write(transport_io_, data->data(), data->length());
    if (write_ret < 0) {
      QUICHE_LOG(ERROR) << "Failed to write data from transport to TLS";
      int ssl_err = SSL_get_error(ssl_.get(), write_ret);
      PrintSSLError("Error while writing data from transport to TLS", ssl_err,
                    write_ret);
      done_ = true;
      return;
    }
    if (write_ret != static_cast<int>(data->length())) {
      QUICHE_LOG(ERROR) << "Short write from transport to TLS: " << write_ret
                        << " != " << data->length();
      done_ = true;
      return;
    }
    QUICHE_DVLOG(1) << "Wrote " << data->length()
                    << " bytes from transport to TLS";
    if (h2_selected_) {
      h2_connection_->OnTransportReadable();
      socket_->ReceiveAsync(kBioBufferSize);
      return;
    }
    int handshake_ret = SSL_do_handshake(ssl_.get());
    if (handshake_ret != 1) {
      int ssl_err = SSL_get_error(ssl_.get(), handshake_ret);
      if (ssl_err == SSL_ERROR_WANT_READ) {
        SendToTransport();
        socket_->ReceiveAsync(kBioBufferSize);
        return;
      }
      PrintSSLError("Error while performing TLS handshake", ssl_err,
                    handshake_ret);
      done_ = true;
      return;
    }
    tls_connected_ = true;
    MaybeSendRequest();
    uint8_t buffer[kBioBufferSize] = {};
    while (true) {
      int ssl_read_ret = SSL_read(ssl_.get(), buffer, sizeof(buffer) - 1);
      if (ssl_read_ret < 0) {
        int ssl_err = SSL_get_error(ssl_.get(), ssl_read_ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
          SendToTransport();
          socket_->ReceiveAsync(kBioBufferSize);
          return;
        }
        PrintSSLError("Error while reading from TLS", ssl_err, ssl_read_ret);
        done_ = true;
        return;
      }
      if (ssl_read_ret == 0) {
        QUICHE_LOG(INFO) << "TLS read closed";
        done_ = true;
        return;
      }
      if (!h2_selected_) {
        QUICHE_DVLOG(1) << "TLS read " << ssl_read_ret
                        << " bytes of h1 response";
        std::cout << buffer << std::endl;
        done_ = true;
        return;
      }
    }
  }

  void SendComplete(absl::Status status) override {
    if (!status.ok()) {
      QUICHE_LOG(ERROR) << "Transport send failed: " << status;
      done_ = true;
      return;
    }
    SendToTransport();
  }

  bool IsDone() const { return done_; }

  // From MasqueH2Connection::Visitor.
  void OnConnectionReady(MasqueH2Connection * /*connection*/) override {}
  void OnConnectionFinished(MasqueH2Connection * /*connection*/) override {
    done_ = true;
  }

  void OnRequest(MasqueH2Connection * /*connection*/, int32_t /*stream_id*/,
                 const quiche::HttpHeaderBlock & /*headers*/,
                 const std::string & /*body*/) override {
    QUICHE_LOG(FATAL) << "Client cannot receive requests";
  }

  void OnResponse(MasqueH2Connection *connection, int32_t stream_id,
                  const quiche::HttpHeaderBlock &headers,
                  const std::string &body) override {
    if (connection != h2_connection_.get()) {
      QUICHE_LOG(FATAL) << "Unexpected connection";
    }
    if (stream_id != stream_id_) {
      QUICHE_LOG(FATAL) << "Unexpected stream id";
    }
    QUICHE_LOG(INFO) << "Received h2 response headers: "
                     << headers.DebugString() << " body: " << body;
    done_ = true;
  }

 private:
  void MaybeSendRequest() {
    if (request_sent_ || done_ || !tls_connected_) {
      return;
    }
    const uint8_t *alpn_data;
    unsigned alpn_len;
    SSL_get0_alpn_selected(ssl_.get(), &alpn_data, &alpn_len);
    if (alpn_len != 0) {
      std::string alpn(reinterpret_cast<const char *>(alpn_data), alpn_len);
      if (alpn == "h2") {
        h2_selected_ = true;
      }
      QUICHE_DVLOG(1) << "ALPN selected: "
                      << std::string(reinterpret_cast<const char *>(alpn_data),
                                     alpn_len);
    } else {
      QUICHE_DVLOG(1) << "No ALPN selected";
    }
    QUICHE_LOG(INFO) << "Using " << (h2_selected_ ? "h2" : "http/1.1");
    if (h2_selected_) {
      SendH2Request();
    } else {
      SendH1Request();
    }
    request_sent_ = true;
  }

  void SendToTransport() {
    char buffer[kBioBufferSize];
    int read_ret = BIO_read(transport_io_, buffer, sizeof(buffer));
    if (read_ret == 0) {
      QUICHE_LOG(ERROR) << "TCP closed while TLS waiting for handshake read";
    } else if (read_ret < 0) {
      int ssl_err = SSL_get_error(ssl_.get(), read_ret);
      if (ssl_err == SSL_ERROR_WANT_READ) {
        QUICHE_DVLOG(1) << "TLS needs more bytes from underlying socket";
      } else if (ssl_err == SSL_ERROR_SYSCALL && errno == 0) {
        QUICHE_DVLOG(1) << "TLS recoverable failure from underlying socket";
      } else {
        PrintSSLError("Error while reading from transport_io_", ssl_err,
                      read_ret);
      }
    } else {
      QUICHE_DVLOG(1) << "TLS wrote " << read_ret << " bytes to transport";
      socket_->SendAsync(std::string(buffer, read_ret));
    }
  }

  int WriteDataToTls(absl::string_view data) {
    QUICHE_DVLOG(2) << "Writing " << data.size()
                    << " app bytes to TLS:" << std::endl
                    << quiche::QuicheTextUtils::HexDump(data);
    int ssl_write_ret = SSL_write(ssl_.get(), data.data(), data.size());
    if (ssl_write_ret <= 0) {
      int ssl_err = SSL_get_error(ssl_.get(), ssl_write_ret);
      PrintSSLError("Error while writing request to TLS", ssl_err,
                    ssl_write_ret);
      done_ = true;
      return -1;
    } else {
      if (ssl_write_ret == static_cast<int>(data.size())) {
        QUICHE_DVLOG(1) << "Wrote " << data.size() << " bytes to TLS";
      } else {
        QUICHE_DVLOG(1) << "Wrote " << ssl_write_ret << " / " << data.size()
                        << "bytes to TLS";
      }
      SendToTransport();
    }
    return ssl_write_ret;
  }

  void SendH1Request() {
    std::string request = absl::StrCat("GET ", url_.path(),
                                       " HTTP/1.1\r\nHost: ", url_.HostPort(),
                                       "\r\nConnection: close\r\n\r\n");
    QUICHE_DVLOG(1) << "Sending h1 request of length " << request.size()
                    << " to TLS";
    int write_res = WriteDataToTls(request);
    if (write_res < 0) {
      QUICHE_LOG(ERROR) << "Failed to write request to TLS";
      done_ = true;
      return;
    } else if (write_res != static_cast<int>(request.size())) {
      QUICHE_LOG(ERROR) << "Request TLS short write " << write_res << " < "
                        << request.size();
      done_ = true;
      return;
    }
  }

  void SendH2Request() {
    h2_connection_ =
        std::make_unique<MasqueH2Connection>(ssl_.get(),
                                             /*is_server=*/false, this);
    h2_connection_->OnTransportReadable();
    quiche::HttpHeaderBlock headers;
    headers[":method"] = "GET";
    headers[":scheme"] = url_.scheme();
    headers[":authority"] = url_.HostPort();
    headers[":path"] = url_.path();
    headers["host"] = url_.HostPort();
    stream_id_ = h2_connection_->SendRequest(headers, std::string());
    h2_connection_->AttemptToSend();
    if (stream_id_ >= 0) {
      QUICHE_LOG(INFO) << "Wrote h2 request to stream " << stream_id_
                       << ", now sending to transport";
      SendToTransport();
    } else {
      QUICHE_LOG(ERROR) << "Failed to send h2 request";
      done_ = true;
    }
  }

  static constexpr size_t kBioBufferSize = 16384;
  QuicEventLoop *event_loop_;  // Not owned.
  SSL_CTX *ctx_;               // Not owned.
  std::unique_ptr<EventLoopSocketFactory> socket_factory_;
  QuicUrl url_;
  bool disable_certificate_verification_;
  int address_family_for_lookup_;
  std::unique_ptr<ProofVerifier> proof_verifier_;
  QuicSocketAddress socket_address_;
  std::unique_ptr<ConnectingClientSocket> socket_;
  BIO *transport_io_ = nullptr;
  bssl::UniquePtr<SSL> ssl_;
  bool tls_connected_ = false;
  bool h2_selected_ = false;
  bool request_sent_ = false;
  bool done_ = false;
  int32_t stream_id_ = -1;
  std::unique_ptr<MasqueH2Connection> h2_connection_;
};

int RunMasqueTcpClient(int argc, char *argv[]) {
  const char *usage = "Usage: masque_tcp_client <url>";
  std::vector<std::string> urls =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (urls.size() != 1) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  quiche::QuicheSystemEventLoop system_event_loop("masque_client");
  const bool disable_certificate_verification =
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification);

  std::optional<bssl::UniquePtr<SSL_CTX>> ssl_ctx = CreateSslCtx(
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_file),
      quiche::GetQuicheCommandLineFlag(FLAGS_client_cert_key_file));
  if (!ssl_ctx.has_value()) {
    return 1;
  }

  const int address_family =
      quiche::GetQuicheCommandLineFlag(FLAGS_address_family);
  int address_family_for_lookup;
  if (address_family == 0) {
    address_family_for_lookup = AF_UNSPEC;
  } else if (address_family == 4) {
    address_family_for_lookup = AF_INET;
  } else if (address_family == 6) {
    address_family_for_lookup = AF_INET6;
  } else {
    QUICHE_LOG(ERROR) << "Invalid address_family " << address_family;
    return 1;
  }
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());

  QuicUrl url(urls[0], "https");
  if (url.host().empty() && !absl::StrContains(urls[0], "://")) {
    url = QuicUrl(absl::StrCat("https://", urls[0]));
  }
  if (url.host().empty()) {
    QUICHE_LOG(ERROR) << "Failed to parse URL \"" << urls[0] << "\"";
    return 1;
  }

  MasqueTlsTcpClientHandler tls_handler(event_loop.get(), ssl_ctx->get(), url,
                                        disable_certificate_verification,
                                        address_family_for_lookup);
  if (!tls_handler.Start()) {
    return 1;
  }
  while (!tls_handler.IsDone()) {
    event_loop->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
  }

  return 0;
}

}  // namespace

}  // namespace quic

int main(int argc, char *argv[]) {
  return quic::RunMasqueTcpClient(argc, argv);
}
