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
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "openssl/boringssl/src/include/openssl/base.h"
#include "openssl/boringssl/src/include/openssl/bio.h"
#include "openssl/boringssl/src/include/openssl/err.h"
#include "openssl/boringssl/src/include/openssl/pool.h"
#include "openssl/boringssl/src/include/openssl/ssl.h"
#include "openssl/boringssl/src/include/openssl/stack.h"
#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/event_loop_socket_factory.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/simple_buffer_allocator.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int, address_family, 0,
                                "IP address family to use. Must be 0, 4 or 6. "
                                "Defaults to 0 which means any.");

namespace quic {

namespace {

class MasqueTlsTcpClientHandler : public ConnectingClientSocket::AsyncVisitor {
 public:
  explicit MasqueTlsTcpClientHandler(QuicEventLoop *event_loop, QuicUrl url,
                                     bool disable_certificate_verification,
                                     int address_family_for_lookup)
      : event_loop_(event_loop),
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

    ssl_.reset((SSL_new(MasqueSslCtx())));

    SSL_set_min_proto_version(ssl_.get(), TLS1_2_VERSION);
    SSL_set_max_proto_version(ssl_.get(), TLS1_3_VERSION);
    if (SSL_set_app_data(ssl_.get(), this) != 1) {
      QUICHE_LOG(FATAL) << "SSL_set_app_data failed";
      return;
    }
    SSL_set_custom_verify(ssl_.get(), SSL_VERIFY_PEER, &VerifyCallback);

    if (SSL_set_tlsext_host_name(ssl_.get(), url_.host().c_str()) != 1) {
      QUICHE_LOG(FATAL) << "SSL_set_tlsext_host_name failed";
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
        QUICHE_LOG(INFO) << "SSL_connect will require another read";
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
    std::cout << buffer << std::endl;
    done_ = true;
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

 private:
  static SSL_CTX *MasqueSslCtx() {
    static bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
    return ctx.get();
  }

  static void PrintSSLError(const char *msg, int ssl_err, int ret) {
    switch (ssl_err) {
      case SSL_ERROR_SSL:
        QUICHE_LOG(ERROR) << msg << ": "
                          << ERR_reason_error_string(ERR_peek_error());
        break;
      case SSL_ERROR_SYSCALL:
        if (ret == 0) {
          QUICHE_LOG(ERROR) << msg << ": peer closed connection";
        } else {
          QUICHE_LOG(ERROR) << msg << ": " << strerror(errno);
        }
        break;
      case SSL_ERROR_ZERO_RETURN:
        QUICHE_LOG(ERROR) << msg << ": received close_notify";
        break;
      default:
        QUICHE_LOG(ERROR) << msg << ": unexpected error: "
                          << SSL_error_description(ssl_err);
        break;
    }
    ERR_print_errors_fp(stderr);
  }

  void MaybeSendRequest() {
    if (!tls_connected_) {
      return;
    }
    std::string request = absl::StrCat("GET ", url_.path(),
                                       " HTTP/1.1\r\nHost: ", url_.HostPort(),
                                       "\r\nConnection: close\r\n\r\n");
    const int request_length = request.size();
    int ssl_write_ret = SSL_write(ssl_.get(), request.c_str(), request_length);
    if (ssl_write_ret <= 0) {
      int ssl_err = SSL_get_error(ssl_.get(), ssl_write_ret);
      PrintSSLError("Error while writing request to TLS", ssl_err,
                    ssl_write_ret);
      done_ = true;
      return;
    }
    if (ssl_write_ret != request_length) {
      QUICHE_LOG(ERROR) << "Request TLS short write " << ssl_write_ret << " < "
                        << request_length;
      done_ = true;
      return;
    }
    QUICHE_DVLOG(1) << "Sent request to TLS";
    SendToTransport();
  }

  void SendToTransport() {
    char buffer[kBioBufferSize];
    int read_ret = BIO_read(transport_io_, buffer, sizeof(buffer));
    if (read_ret == 0) {
      QUICHE_LOG(ERROR) << "TCP closed while TLS waiting for handshake read";
    } else if (read_ret < 0) {
      QUICHE_LOG(ERROR) << "Error while reading from transport_io_: "
                        << read_ret;
    } else {
      QUICHE_DVLOG(1) << "TLS wrote " << read_ret << " bytes to transport";
      socket_->SendAsync(std::string(buffer, read_ret));
    }
  }

  static constexpr size_t kBioBufferSize = 16384;
  QuicEventLoop *event_loop_;  // Not owned.
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
  bool done_ = false;
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

  MasqueTlsTcpClientHandler tls_handler(event_loop.get(), url,
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
