// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_OHTTP_CLIENT_H_
#define QUICHE_QUIC_MASQUE_MASQUE_OHTTP_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/masque/masque_connection_pool.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/binary_http/binary_http_message.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/common/oblivious_http_chunk_handler.h"
#include "quiche/oblivious_http/oblivious_http_client.h"

namespace quic {

// A client that sends OHTTP requests through a relay/gateway to target URLs.
class QUICHE_EXPORT MasqueOhttpClient
    : public quic::MasqueConnectionPool::Visitor {
 public:
  using RequestId = quic::MasqueConnectionPool::RequestId;
  using Message = quic::MasqueConnectionPool::Message;

  class QUICHE_NO_EXPORT Config {
   public:
    class QUICHE_NO_EXPORT PerRequestConfig {
     public:
      explicit PerRequestConfig(const std::string& url) : url_(url) {}
      // Copyable and movable.
      PerRequestConfig(const PerRequestConfig& other) = default;
      PerRequestConfig& operator=(const PerRequestConfig& other) = default;
      PerRequestConfig(PerRequestConfig&& other) = default;
      PerRequestConfig& operator=(PerRequestConfig&& other) = default;

      void SetPostData(const std::string& post_data) { post_data_ = post_data; }
      absl::Status AddHeaders(const std::vector<std::string>& headers);
      absl::Status AddPrivateToken(const std::string& private_token);
      void SetUseChunkedOhttp(bool use_chunked_ohttp) {
        use_chunked_ohttp_ = use_chunked_ohttp;
      }
      void SetUseIndeterminateLength(
          std::optional<bool> use_indeterminate_length) {
        use_indeterminate_length_ = use_indeterminate_length;
      }
      void SetExpectedGatewayError(const std::string& expected_gateway_error) {
        expected_gateway_error_ = expected_gateway_error;
      }
      void SetExpectedGatewayStatusCode(uint16_t status_code) {
        expected_gateway_status_code_ = status_code;
      }
      void SetExpectedEncapsulatedStatusCode(uint16_t status_code) {
        expected_encapsulated_status_code_ = status_code;
      }
      void SetExpectedEncapsulatedResponseBody(
          const std::string& expected_encapsulated_response_body) {
        expected_encapsulated_response_body_ =
            expected_encapsulated_response_body;
      }

      std::string url() const { return url_; }
      std::string post_data() const { return post_data_; }
      const std::vector<std::pair<std::string, std::string>>& headers() const {
        return headers_;
      }
      bool use_chunked_ohttp() const { return use_chunked_ohttp_; }
      std::optional<bool> use_indeterminate_length() const {
        return use_indeterminate_length_;
      }
      std::optional<std::string> expected_gateway_error() const {
        return expected_gateway_error_;
      }
      std::optional<uint16_t> expected_gateway_status_code() const {
        return expected_gateway_status_code_;
      }
      std::optional<uint16_t> expected_encapsulated_status_code() const {
        return expected_encapsulated_status_code_;
      }
      std::optional<std::string> expected_encapsulated_response_body() const {
        return expected_encapsulated_response_body_;
      }

     private:
      std::string url_;
      std::string post_data_;
      std::vector<std::pair<std::string, std::string>> headers_;
      bool use_chunked_ohttp_ = false;
      std::optional<bool> use_indeterminate_length_;
      std::optional<std::string> expected_gateway_error_;
      std::optional<uint16_t> expected_gateway_status_code_;
      std::optional<uint16_t> expected_encapsulated_status_code_;
      std::optional<std::string> expected_encapsulated_response_body_;
    };

    explicit Config(const std::string& key_fetch_url,
                    const std::string& relay_url)
        : key_fetch_url_(key_fetch_url), relay_url_(relay_url) {}
    // Movable but not copyable.
    Config(const Config& other) = delete;
    Config& operator=(const Config& other) = delete;
    Config(Config&& other) = default;
    Config& operator=(Config&& other) = default;

    absl::Status ConfigureKeyFetchClientCert(
        const std::string& client_cert_file,
        const std::string& client_cert_key_file);
    absl::Status ConfigureKeyFetchClientCertFromData(
        const std::string& client_cert_pem_data,
        const std::string& client_cert_key_data);
    absl::Status ConfigureOhttpMtls(const std::string& client_cert_file,
                                    const std::string& client_cert_key_file);
    absl::Status ConfigureOhttpMtlsFromData(
        const std::string& client_cert_pem_data,
        const std::string& client_cert_key_data);
    void SetDisableCertificateVerification(
        bool disable_certificate_verification) {
      disable_certificate_verification_ = disable_certificate_verification;
    }
    void SetDnsConfig(const MasqueConnectionPool::DnsConfig& dns_config) {
      dns_config_ = dns_config;
    }
    void AddPerRequestConfig(const PerRequestConfig& per_request_config) {
      per_request_configs_.push_back(per_request_config);
    }

    const std::string& key_fetch_url() const { return key_fetch_url_; }
    const std::string& relay_url() const { return relay_url_; }
    SSL_CTX* key_fetch_ssl_ctx() const { return key_fetch_ssl_ctx_.get(); }
    SSL_CTX* ohttp_ssl_ctx() const { return ohttp_ssl_ctx_.get(); }
    const std::vector<PerRequestConfig>& per_request_configs() const {
      return per_request_configs_;
    }
    bool disable_certificate_verification() const {
      return disable_certificate_verification_;
    }
    const MasqueConnectionPool::DnsConfig& dns_config() const {
      return dns_config_;
    }

   private:
    std::string key_fetch_url_;
    std::string relay_url_;
    bssl::UniquePtr<SSL_CTX> key_fetch_ssl_ctx_;
    bssl::UniquePtr<SSL_CTX> ohttp_ssl_ctx_;
    bool disable_certificate_verification_ = false;
    MasqueConnectionPool::DnsConfig dns_config_;
    std::vector<PerRequestConfig> per_request_configs_;
  };

  // Starts by fetching the HPKE keys and then runs the client until all
  // requests are complete or aborted.
  static absl::Status Run(Config config);

 protected:
  // From quic::MasqueConnectionPool::Visitor.
  void OnPoolResponse(quic::MasqueConnectionPool* /*pool*/,
                      RequestId request_id,
                      absl::StatusOr<Message>&& response) override;

  // Fetch key from the key URL.
  absl::Status StartKeyFetch(const std::string& url_string);

  // Handles the key response and starts the OHTTP request.
  absl::Status HandleKeyResponse(const absl::StatusOr<Message>& response);

  // Sends the OHTTP request for the given URL.
  absl::Status SendOhttpRequest(
      const Config::PerRequestConfig& per_request_config);

  // Signals the client to abort.
  void Abort(absl::Status status);

 private:
  class QUICHE_NO_EXPORT ChunkHandler
      : public quiche::ObliviousHttpChunkHandler,
        public quiche::BinaryHttpResponse::IndeterminateLengthDecoder::
            MessageSectionHandler {
   public:
    explicit ChunkHandler();
    // Neither copyable nor movable to ensure pointer stability as required for
    // quiche::ObliviousHttpChunkHandler.
    ChunkHandler(const ChunkHandler& other) = delete;
    ChunkHandler& operator=(const ChunkHandler& other) = delete;
    ChunkHandler(ChunkHandler&& other) = delete;
    ChunkHandler& operator=(ChunkHandler&& other) = delete;

    // Decrypts the full chunked response and returns the encapsulated response.
    absl::StatusOr<Message> DecryptFullResponse(
        absl::string_view encrypted_response);

    void SetChunkedClient(quiche::ChunkedObliviousHttpClient chunked_client) {
      chunked_client_.emplace(std::move(chunked_client));
    }

    Message ExtractResponse() && { return std::move(response_); }

    // From quiche::ObliviousHttpChunkHandler.
    absl::Status OnDecryptedChunk(absl::string_view decrypted_chunk) override;
    absl::Status OnChunksDone() override;

    // From quiche::BinaryHttpResponse::
    // IndeterminateLengthDecoder::MessageSectionHandler.
    absl::Status OnInformationalResponseStatusCode(
        uint16_t status_code) override;
    absl::Status OnInformationalResponseHeader(
        absl::string_view name, absl::string_view value) override;
    absl::Status OnInformationalResponseDone() override;
    absl::Status OnInformationalResponsesSectionDone() override;
    absl::Status OnFinalResponseStatusCode(uint16_t status_code) override;
    absl::Status OnFinalResponseHeader(absl::string_view name,
                                       absl::string_view value) override;
    absl::Status OnFinalResponseHeadersDone() override;
    absl::Status OnBodyChunk(absl::string_view body_chunk) override;
    absl::Status OnBodyChunksDone() override;
    absl::Status OnTrailer(absl::string_view name,
                           absl::string_view value) override;
    absl::Status OnTrailersDone() override;

   private:
    std::optional<quiche::ChunkedObliviousHttpClient> chunked_client_;
    quiche::BinaryHttpResponse::IndeterminateLengthDecoder decoder_;
    Message response_;
    std::string buffered_binary_response_;
    std::optional<bool> is_chunked_response_;
  };

  struct PendingRequest {
    explicit PendingRequest(const Config::PerRequestConfig& per_request_config)
        : per_request_config(per_request_config) {}

    const Config::PerRequestConfig& per_request_config;
    // `context` is only used for non-chunked OHTTP requests.
    std::optional<quiche::ObliviousHttpRequest::Context> context;
    // `chunk_handler` is only used for chunked OHTTP requests. We use
    // std::unique_ptr to ensure pointer stability since this object is used as
    // a callback target.
    std::unique_ptr<ChunkHandler> chunk_handler;
  };

  explicit MasqueOhttpClient(Config config, quic::QuicEventLoop* event_loop);

  // Starts fetching for the key and sends the OHTTP request.
  absl::Status Start();

  // Returns true if the client has completed all requests.
  bool IsDone();

  absl::StatusOr<Message> TryExtractEncapsulatedResponse(
      RequestId request_id, quiche::ObliviousHttpRequest::Context& context,
      const Message& response);
  absl::Status ProcessOhttpResponse(RequestId request_id,
                                    const absl::StatusOr<Message>& response);
  absl::Status CheckStatusAndContentType(
      const Message& response, const std::string& content_type,
      std::optional<uint16_t> expected_status_code);

  Config config_;
  quic::MasqueConnectionPool connection_pool_;
  std::optional<RequestId> key_fetch_request_id_;
  absl::Status status_ = absl::OkStatus();
  std::optional<quiche::ObliviousHttpClient> ohttp_client_;
  quic::QuicUrl relay_url_;
  absl::flat_hash_map<RequestId, PendingRequest> pending_ohttp_requests_;
};
}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_OHTTP_CLIENT_H_
