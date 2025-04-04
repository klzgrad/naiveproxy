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
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/oghttp2_adapter.h"
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
#include "quiche/common/quiche_text_utils.h"
#include "quiche/common/simple_buffer_allocator.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int, address_family, 0,
                                "IP address family to use. Must be 0, 4 or 6. "
                                "Defaults to 0 which means any.");

using http2::adapter::Header;
using http2::adapter::HeaderRep;
using http2::adapter::Http2ErrorCode;
using http2::adapter::Http2KnownSettingsId;
using http2::adapter::Http2PingId;
using http2::adapter::Http2Setting;
using http2::adapter::Http2SettingsIdToString;
using http2::adapter::Http2StreamId;
using http2::adapter::Http2VisitorInterface;
using http2::adapter::OgHttp2Adapter;

namespace quic {

namespace {

class MasqueTlsTcpClientHandler : public ConnectingClientSocket::AsyncVisitor,
                                  public Http2VisitorInterface {
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
      if (h2_selected_) {
        QUICHE_DVLOG(1) << "TLS read " << ssl_read_ret << " bytes of h2 data";
        h2_adapter_->ProcessBytes(absl::string_view(
            reinterpret_cast<const char *>(buffer), ssl_read_ret));
      } else {
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
    OgHttp2Adapter::Options options;
    h2_adapter_ = OgHttp2Adapter::Create(*this, options);
    std::vector<Http2Setting> settings;
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::HEADER_TABLE_SIZE, 4096});
    settings.push_back(Http2Setting{Http2KnownSettingsId::ENABLE_PUSH, 0});
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::MAX_CONCURRENT_STREAMS, 100});
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::INITIAL_WINDOW_SIZE, 65535});
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::MAX_FRAME_SIZE, 16384});
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::MAX_HEADER_LIST_SIZE, 65535});
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::ENABLE_CONNECT_PROTOCOL, 1});
    h2_adapter_->SubmitSettings(settings);
    std::vector<Header> headers;
    headers.push_back(std::make_pair(HeaderRep(std::string(":method")),
                                     HeaderRep(std::string("GET"))));
    headers.push_back(std::make_pair(HeaderRep(std::string(":scheme")),
                                     HeaderRep(url_.scheme())));
    headers.push_back(std::make_pair(HeaderRep(std::string(":authority")),
                                     HeaderRep(url_.HostPort())));
    headers.push_back(std::make_pair(HeaderRep(std::string(":path")),
                                     HeaderRep(url_.path())));
    headers.push_back(std::make_pair(HeaderRep(std::string("host")),
                                     HeaderRep(url_.HostPort())));
    stream_id_ =
        h2_adapter_->SubmitRequest(headers,
                                   /*end_stream=*/true, /*user_data=*/nullptr);
    int h2_send_result = h2_adapter_->Send();
    if (h2_send_result != 0) {
      QUICHE_LOG(ERROR) << "h2 send failed";
      done_ = true;
      return;
    }
    QUICHE_LOG(INFO) << "Sent h2 request on stream " << stream_id_;
  }

  // From Http2VisitorInterface.
  using Http2VisitorInterface::ConnectionError;
  using Http2VisitorInterface::DataFrameHeaderInfo;
  using Http2VisitorInterface::InvalidFrameError;
  using Http2VisitorInterface::OnHeaderResult;

  int64_t OnReadyToSend(absl::string_view serialized) override {
    QUICHE_DVLOG(1) << "Writing " << serialized.size()
                    << " bytes of h2 data to TLS";
    int write_res = WriteDataToTls(serialized);
    if (write_res < 0) {
      return kSendError;
    }
    return write_res;
  }

  DataFrameHeaderInfo OnReadyToSendDataForStream(
      Http2StreamId /*stream_id*/, size_t /*max_length*/) override {
    // We'll need to implement this to support POST or CONNECT requests.
    QUICHE_LOG(FATAL) << "Sending h2 DATA not implemented";
    return DataFrameHeaderInfo{};
  }

  bool SendDataFrame(Http2StreamId /*stream_id*/,
                     absl::string_view /*frame_header*/,
                     size_t /*payload_bytes*/) override {
    // We'll need to implement this to support POST or CONNECT requests.
    QUICHE_LOG(FATAL) << "Sending h2 DATA not implemented";
    return false;
  }

  void OnConnectionError(ConnectionError error) override {
    QUICHE_LOG(ERROR) << "OnConnectionError: " << static_cast<int>(error);
    done_ = true;
  }

  void OnSettingsStart() override {}

  void OnSetting(Http2Setting setting) override {
    QUICHE_LOG(INFO) << "Received " << Http2SettingsIdToString(setting.id)
                     << " = " << setting.value;
  }

  void OnSettingsEnd() override {}
  void OnSettingsAck() override {}

  bool OnBeginHeadersForStream(Http2StreamId stream_id) override {
    QUICHE_DVLOG(1) << "OnBeginHeadersForStream " << stream_id;
    return true;
  }

  OnHeaderResult OnHeaderForStream(Http2StreamId stream_id,
                                   absl::string_view key,
                                   absl::string_view value) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id << " received header " << key
                     << " = " << value;
    return OnHeaderResult::HEADER_OK;
  }

  bool OnEndHeadersForStream(Http2StreamId stream_id) override {
    QUICHE_DVLOG(1) << "OnEndHeadersForStream " << stream_id;
    return true;
  }

  bool OnBeginDataForStream(Http2StreamId stream_id,
                            size_t payload_length) override {
    QUICHE_DVLOG(1) << "OnBeginDataForStream " << stream_id
                    << " payload_length: " << payload_length;
    return true;
  }

  bool OnDataPaddingLength(Http2StreamId stream_id,
                           size_t padding_length) override {
    QUICHE_LOG(INFO) << "OnDataPaddingLength stream_id: " << stream_id
                     << " padding_length: " << padding_length;
    return true;
  }

  bool OnDataForStream(Http2StreamId stream_id,
                       absl::string_view data) override {
    QUICHE_DVLOG(1) << "OnDataForStream " << stream_id
                    << " data length: " << data.size() << std::endl
                    << data;
    return true;
  }

  bool OnEndStream(Http2StreamId stream_id) override {
    QUICHE_DVLOG(1) << "OnEndStream " << stream_id;
    return true;
  }

  void OnRstStream(Http2StreamId stream_id,
                   Http2ErrorCode error_code) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id << " reset with error code "
                     << Http2ErrorCodeToString(error_code);
  }

  bool OnCloseStream(Http2StreamId stream_id,
                     Http2ErrorCode error_code) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id << " closed with error code "
                     << Http2ErrorCodeToString(error_code);
    if (stream_id == stream_id_) {
      done_ = true;
    }
    return true;
  }

  void OnPriorityForStream(Http2StreamId stream_id,
                           Http2StreamId parent_stream_id, int weight,
                           bool exclusive) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id << " received priority "
                     << weight << (exclusive ? " exclusive" : "") << " parent "
                     << parent_stream_id;
  }

  void OnPing(Http2PingId ping_id, bool is_ack) override {
    QUICHE_LOG(INFO) << "Received ping " << ping_id << (is_ack ? " ack" : "");
  }

  void OnPushPromiseForStream(Http2StreamId stream_id,
                              Http2StreamId promised_stream_id) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id
                     << " received push promise for stream "
                     << promised_stream_id;
  }

  bool OnGoAway(Http2StreamId last_accepted_stream_id,
                Http2ErrorCode error_code,
                absl::string_view opaque_data) override {
    QUICHE_LOG(INFO) << "Received GOAWAY frame with last_accepted_stream_id: "
                     << last_accepted_stream_id
                     << " error_code: " << Http2ErrorCodeToString(error_code)
                     << " opaque_data length: " << opaque_data.size();
    return true;
  }

  void OnWindowUpdate(Http2StreamId stream_id, int window_increment) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id << " received window update "
                     << window_increment;
  }

  int OnBeforeFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                        size_t length, uint8_t flags) override {
    QUICHE_DVLOG(1) << "OnBeforeFrameSent frame_type: "
                    << static_cast<int>(frame_type)
                    << " stream_id: " << stream_id << " length: " << length
                    << " flags: " << static_cast<int>(flags);
    return 0;
  }

  int OnFrameSent(uint8_t frame_type, Http2StreamId stream_id, size_t length,
                  uint8_t flags, uint32_t error_code) override {
    QUICHE_DVLOG(1) << "OnFrameSent frame_type: "
                    << static_cast<int>(frame_type)
                    << " stream_id: " << stream_id << " length: " << length
                    << " flags: " << static_cast<int>(flags)
                    << " error_code: " << error_code;
    return 0;
  }

  bool OnInvalidFrame(Http2StreamId stream_id,
                      InvalidFrameError error) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id
                     << " received invalid frame error "
                     << static_cast<int>(error);
    if (stream_id == stream_id_) {
      done_ = true;
      return false;
    }
    return true;
  }

  void OnBeginMetadataForStream(Http2StreamId stream_id,
                                size_t payload_length) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id
                     << " about to receive metadata of length "
                     << payload_length;
  }

  bool OnMetadataForStream(Http2StreamId stream_id,
                           absl::string_view metadata) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id
                     << " received metadata of length " << metadata.size();
    return true;
  }

  bool OnMetadataEndForStream(Http2StreamId stream_id) override {
    QUICHE_LOG(INFO) << "Stream " << stream_id << " done receiving metadata";
    return true;
  }

  void OnErrorDebug(absl::string_view message) override {
    QUICHE_LOG(ERROR) << "OnErrorDebug: " << message;
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
  bool h2_selected_ = false;
  int32_t stream_id_ = -1;
  bool request_sent_ = false;
  bool done_ = false;
  std::unique_ptr<OgHttp2Adapter> h2_adapter_;
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
