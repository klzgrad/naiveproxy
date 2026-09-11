// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_ohttp_client.h"

#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "openssl/base.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/masque/masque_connection_pool.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/binary_http/binary_http_message.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include <zlib.h>

#define ENDPOINT info_ << ": "

namespace quic {

using ::quic::MasqueConnectionPool;
using ::quic::QuicUrl;
using ::quiche::BinaryHttpRequest;
using ::quiche::BinaryHttpResponse;
using ::quiche::ChunkedObliviousHttpClient;
using ::quiche::ObliviousHttpClient;
using ::quiche::ObliviousHttpHeaderKeyConfig;
using ::quiche::ObliviousHttpKeyConfigs;
using ::quiche::ObliviousHttpRequest;
using ::quiche::ObliviousHttpResponse;
using RequestId = ::quic::MasqueConnectionPool::RequestId;
using Message = ::quic::MasqueConnectionPool::Message;

namespace {

static constexpr uint64_t kFixedSizeResponseFramingIndicator = 0x01;
// Buffer size for gzip decompression. 32KB chosen arbitrarily.
static constexpr size_t kGzipDecompressBufferSize = 32768;

absl::Status ParseHeadersIntoMap(
    const std::vector<std::string>& headers,
    std::vector<std::pair<std::string, std::string>>& headers_map) {
  for (const std::string& header : headers) {
    std::vector<absl::string_view> header_split =
        absl::StrSplit(header, absl::MaxSplits(':', 1));
    if (header_split.size() != 2) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to parse header \"", header, "\""));
    }
    std::string header_name = std::string(header_split[0]);
    absl::StripAsciiWhitespace(&header_name);
    absl::AsciiStrToLower(&header_name);
    std::string header_value = std::string(header_split[1]);
    absl::StripAsciiWhitespace(&header_value);
    headers_map.push_back({std::move(header_name), std::move(header_value)});
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<absl::string_view>> SplitIntoChunks(
    absl::string_view data, int num_chunks) {
  if (num_chunks <= 0) {
    return absl::InvalidArgumentError("num_chunks must be greater than 0");
  }
  if (data.size() < num_chunks) {
    return absl::InvalidArgumentError("data is smaller than num_chunks");
  }
  std::vector<absl::string_view> chunks;
  size_t offset = 0;
  for (size_t i = 0; i < num_chunks; ++i) {
    size_t end = (data.size() * (i + 1)) / num_chunks;
    chunks.push_back(data.substr(offset, end - offset));
    offset = end;
  }
  return chunks;
}

class PingPongResponseVisitor : public MasqueOhttpClient::ResponseVisitor {
 public:
  explicit PingPongResponseVisitor(std::vector<std::string> chunks,
                                   absl::string_view info_string)
      : chunks_(std::move(chunks)), info_(info_string) {}

  void OnRequestStarted(quic::MasqueConnectionPool::RequestId request_id,
                        MasqueOhttpClient* client) override {
    QUICHE_LOG(INFO) << ENDPOINT << "Ping-pong request " << request_id
                     << " started with " << chunks_.size() << " chunks";
    request_id_ = request_id;
    client_ = client;
    bool is_final = (chunks_.size() <= 1);
    status_ = client_->SendBodyChunk(request_id_, chunks_[0], is_final);
    if (!status_.ok()) {
      QUICHE_LOG(ERROR) << ENDPOINT << "Ping-pong request " << request_id
                        << " failed to send first chunk: " << status_.message();
      done_ = true;
      return;
    }
    current_chunk_idx_ = 1;
    if (is_final) {
      QUICHE_LOG(INFO) << ENDPOINT << "Ping-pong request " << request_id
                       << " only had one chunk, marking as done";
      done_ = true;
    }
  }

  void OnResponseChunk(quic::MasqueConnectionPool::RequestId request_id,
                       absl::string_view chunk) override {
    QUICHE_DVLOG(1) << "Ping-pong request " << request_id
                    << " got response chunk #" << current_chunk_idx_
                    << " of length " << chunk.size();
    if (request_id != request_id_ || current_chunk_idx_ >= chunks_.size()) {
      return;
    }
    bool is_final = (current_chunk_idx_ == chunks_.size() - 1);
    status_ = client_->SendBodyChunk(request_id_, chunks_[current_chunk_idx_],
                                     is_final);
    if (!status_.ok()) {
      QUICHE_LOG(ERROR) << ENDPOINT << "Ping-pong request " << request_id
                        << " failed to send chunk #" << current_chunk_idx_
                        << ": " << status_.message();
      done_ = true;
      return;
    }
    current_chunk_idx_++;
  }

  void OnResponseDone(quic::MasqueConnectionPool::RequestId request_id,
                      const MasqueOhttpClient::Message&) override {
    QUICHE_LOG(INFO) << ENDPOINT << "Ping-pong request done: " << request_id;
    if (request_id == request_id_) {
      done_ = true;
    }
  }

  void OnError(quic::MasqueConnectionPool::RequestId request_id,
               absl::Status status) override {
    QUICHE_LOG(ERROR) << ENDPOINT << "Ping-pong request " << request_id
                      << " got error: " << status.message();
    if (request_id == request_id_) {
      status_ = status;
      done_ = true;
    }
  }

  bool done() const { return done_; }
  absl::Status status() const { return status_; }

 private:
  MasqueOhttpClient* client_ = nullptr;
  std::vector<std::string> chunks_;
  size_t current_chunk_idx_ = 0;
  quic::MasqueConnectionPool::RequestId request_id_ = 0;
  bool done_ = false;
  absl::Status status_ = absl::OkStatus();
  const std::string info_;
};

absl::StatusOr<std::unique_ptr<PingPongResponseVisitor>>
CreateVisitorIfPingPong(const MasqueOhttpClient::Config& config,
                        absl::string_view info_string) {
  const MasqueOhttpClient::Config::PerRequestConfig* ping_pong_config = nullptr;

  for (const auto& req_config : config.per_request_configs()) {
    if (req_config.ping_pong_mode()) {
      ping_pong_config = &req_config;
      break;
    }
  }

  if (ping_pong_config == nullptr) {
    return nullptr;
  }

  if (config.per_request_configs().size() > 1) {
    return absl::InvalidArgumentError(
        "PingPong mode is exclusive and supports only one request at a time");
  }

  if (ping_pong_config->num_ohttp_chunks() <= 0) {
    return absl::InvalidArgumentError(
        "num_ohttp_chunks must be set and greater than 0 when "
        "ping_pong_mode is enabled");
  }

  std::string post_data = ping_pong_config->post_data();
  QUICHE_ASSIGN_OR_RETURN(
      std::vector<absl::string_view> chunks,
      SplitIntoChunks(post_data, ping_pong_config->num_ohttp_chunks()));

  std::vector<std::string> string_chunks;
  string_chunks.reserve(chunks.size());
  for (absl::string_view chunk : chunks) {
    string_chunks.push_back(std::string(chunk));
  }

  return std::make_unique<PingPongResponseVisitor>(std::move(string_chunks),
                                                   info_string);
}

}  // namespace

absl::StatusOr<GzipDecompressor::DecompressedData>
GzipDecompressor::InflateLoop(int flush) {
  int ret = Z_OK;
  std::string decompressed = "";
  std::vector<unsigned char> outbuffer(kGzipDecompressBufferSize);

  // Decompress the input in chunks until we reach the end of the stream or
  // need more input.
  do {
    stream_->next_out = outbuffer.data();
    stream_->avail_out = outbuffer.size();

    ret = inflate(stream_.get(), flush);

    if (ret != Z_OK && ret != Z_STREAM_END) {
      return absl::InternalError(absl::StrCat(
          "Gzip decompression failed with error code: ", ret,
          stream_->msg ? absl::StrCat(" (", stream_->msg, ")") : ""));
    }

    // Calculate how much data was placed in the buffer.
    size_t decompressed_size = outbuffer.size() - stream_->avail_out;
    decompressed.append(reinterpret_cast<char*>(outbuffer.data()),
                        decompressed_size);
  } while (stream_->avail_out == 0 && ret != Z_STREAM_END);

  return DecompressedData{ret, std::move(decompressed)};
}

absl::StatusOr<std::unique_ptr<GzipDecompressor>> GzipDecompressor::Create() {
  auto stream = std::make_unique<z_stream>();
  // z_stream fields must be explicitly initialized, as per the zlib
  // documentation.
  memset(stream.get(), 0, sizeof(z_stream));

  // Initialize zlib for gzip decompression.
  // 16 + MAX_WBITS tells zlib to expect a gzip header and trailer, which is
  // the expectation for valid HTTP responses with `Content-Encoding: gzip`.
  int inflate_result = inflateInit2(stream.get(), 16 + MAX_WBITS);
  if (inflate_result != Z_OK) {
    return absl::InternalError(
        absl::StrCat("Failed to initialize zlib for gzip decompression, error "
                     "code: ",
                     inflate_result));
  }

  return std::unique_ptr<GzipDecompressor>(
      new GzipDecompressor(std::move(stream)));
}

absl::StatusOr<std::string> GzipDecompressor::Decompress(
    absl::string_view input, bool end_stream) {
  if (stream_ == nullptr) {
    return absl::FailedPreconditionError("Decompression has already ended");
  }
  stream_->next_in =
      reinterpret_cast<unsigned char*>(const_cast<char*>(input.data()));
  stream_->avail_in = input.size();

  QUICHE_ASSIGN_OR_RETURN(DecompressedData decompressed_data,
                          InflateLoop(end_stream ? Z_FINISH : Z_NO_FLUSH));

  if (decompressed_data.status_code == Z_STREAM_END) {
    finished_ = true;
  }

  if (end_stream && !finished_) {
    return absl::InternalError(
        "Gzip decompression failed to reach end of stream");
  }
  return decompressed_data.data;
}

void GzipDecompressor::EndDecompression() {
  if (stream_ != nullptr) {
    inflateEnd(stream_.get());
    stream_.reset();
  }
}

absl::StatusOr<std::string> GzipDecompress(absl::string_view input) {
  QUICHE_ASSIGN_OR_RETURN(std::unique_ptr<GzipDecompressor> decompressor,
                          GzipDecompressor::Create());
  return decompressor->Decompress(input, /*end_stream=*/true);
}

std::string MasqueOhttpClient::Config::PerRequestConfig::method() const {
  if (method_.has_value()) {
    return *method_;
  }
  if (!post_data_.empty()) {
    return "POST";
  }
  return "GET";
}

absl::Status MasqueOhttpClient::Config::PerRequestConfig::AddHeaders(
    const std::vector<std::string>& headers) {
  return ParseHeadersIntoMap(headers, headers_);
}

absl::Status MasqueOhttpClient::Config::PerRequestConfig::AddOuterHeaders(
    const std::vector<std::string>& headers) {
  return ParseHeadersIntoMap(headers, outer_headers_);
}

absl::Status MasqueOhttpClient::Config::AddKeyFetchHeaders(
    const std::vector<std::string>& headers) {
  return ParseHeadersIntoMap(headers, key_fetch_headers_);
}

absl::Status MasqueOhttpClient::Config::PerRequestConfig::AddPrivateToken(
    const std::string& private_token) {
  // Private tokens require padded base64url and we allow any encoding for
  // convenience, so we need to unescape and re-escape.
  // https://www.rfc-editor.org/rfc/rfc9577#section-2.2.2
  std::string formatted_token;
  if (!absl::Base64Unescape(private_token, &formatted_token) &&
      !absl::WebSafeBase64Unescape(private_token, &formatted_token)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid base64 encoding in private token: \"", private_token, "\""));
  }
  formatted_token = absl::Base64Escape(formatted_token);
  absl::StrReplaceAll({{"+", "-"}, {"/", "_"}}, &formatted_token);
  headers_.push_back({"authorization", absl::StrCat("PrivateToken token=\"",
                                                    formatted_token, "\"")});
  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::Config::ConfigureKeyFetchClientCert(
    const std::string& client_cert_file,
    const std::string& client_cert_key_file) {
  QUICHE_ASSIGN_OR_RETURN(key_fetch_ssl_ctx_,
                          MasqueConnectionPool::CreateSslCtx(
                              client_cert_file, client_cert_key_file));
  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::Config::ConfigureKeyFetchClientCertFromData(
    const std::string& client_cert_pem_data,
    const std::string& client_cert_key_data) {
  QUICHE_ASSIGN_OR_RETURN(key_fetch_ssl_ctx_,
                          MasqueConnectionPool::CreateSslCtxFromData(
                              client_cert_pem_data, client_cert_key_data));
  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::Config::ConfigureOhttpMtls(
    const std::string& client_cert_file,
    const std::string& client_cert_key_file) {
  QUICHE_ASSIGN_OR_RETURN(
      ohttp_ssl_ctx_, MasqueConnectionPool::CreateSslCtx(client_cert_file,
                                                         client_cert_key_file));
  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::Config::ConfigureOhttpMtlsFromData(
    const std::string& client_cert_pem_data,
    const std::string& client_cert_key_data) {
  QUICHE_ASSIGN_OR_RETURN(ohttp_ssl_ctx_,
                          MasqueConnectionPool::CreateSslCtxFromData(
                              client_cert_pem_data, client_cert_key_data));
  return absl::OkStatus();
}

bool MasqueOhttpClient::Config::skip_ohttp() const {
  static constexpr absl::string_view kSkip = "skip";
  return key_fetch_url_ == kSkip && relay_url_ == kSkip;
}

MasqueOhttpClient::MasqueOhttpClient(Config config,
                                     quic::QuicEventLoop* event_loop,
                                     absl::string_view info_string)
    : config_(std::move(config)),
      info_(info_string),
      connection_pool_(event_loop, config_.key_fetch_ssl_ctx(),
                       config_.disable_certificate_verification(),
                       config_.dns_config(), this, info_) {
  connection_pool_.SetMtlsSslCtx(config_.ohttp_ssl_ctx());
}

// static
absl::StatusOr<MasqueOhttpClient::RunDetails> MasqueOhttpClient::RunInner(
    Config config, absl::string_view info_string) {
  if (config.per_request_configs().empty()) {
    return absl::InvalidArgumentError("No OHTTP URLs to request");
  }
  if (config.key_fetch_ssl_ctx() == nullptr) {
    QUICHE_RETURN_IF_ERROR(config.ConfigureKeyFetchClientCert("", ""));
  }
  if (config.ohttp_ssl_ctx() == nullptr) {
    QUICHE_RETURN_IF_ERROR(config.ConfigureOhttpMtls("", ""));
  }
  QUICHE_ASSIGN_OR_RETURN(
      std::unique_ptr<PingPongResponseVisitor> ping_pong_visitor,
      CreateVisitorIfPingPong(config, info_string));

  quiche::QuicheSystemEventLoop system_event_loop("masque_ohttp_client");
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());
  MasqueOhttpClient ohttp_client(std::move(config), event_loop.get(),
                                 info_string);
  if (ping_pong_visitor) {
    ohttp_client.set_response_visitor(ping_pong_visitor.get());
  }

  ohttp_client.Start();
  while (!ohttp_client.IsDone()) {
    if (ping_pong_visitor && ping_pong_visitor->done()) {
      if (!ping_pong_visitor->status().ok()) {
        ohttp_client.Abort(ping_pong_visitor->status());
      }
      break;
    }
    ohttp_client.connection_pool_.event_loop()->RunEventLoopOnce(
        quic::QuicTime::Delta::FromMilliseconds(50));
  }
  return std::move(ohttp_client.run_details_);
}

// static
MasqueOhttpClient::RunDetails MasqueOhttpClient::Run(
    Config config, absl::string_view info_string) {
  absl::StatusOr<MasqueOhttpClient::RunDetails> run_result =
      RunInner(std::move(config), info_string);
  if (!run_result.ok()) {
    RunDetails run_details;
    run_details.status = run_result.status();
    return run_details;
  }
  return *std::move(run_result);
}

void MasqueOhttpClient::Start() {
  absl::Status status = StartKeyFetch();
  if (!status.ok()) {
    Abort(status);
  }
}
bool MasqueOhttpClient::IsDone() {
  if (!run_details_.status.ok()) {
    return true;
  }
  if (!config_.skip_ohttp() && !ohttp_client_.has_value()) {
    // Key fetch request is still pending.
    return false;
  }
  return unstarted_requests_.empty() && pending_ohttp_requests_.empty();
}

void MasqueOhttpClient::Abort(absl::Status status) {
  QUICHE_CHECK(!status.ok());
  if (!run_details_.status.ok()) {
    QUICHE_LOG(ERROR)
        << ENDPOINT << "MasqueOhttpClient already aborted, ignoring new error: "
        << status.message();
    return;
  }
  run_details_.status = status;
  QUICHE_LOG(ERROR) << ENDPOINT
                    << "Aborting MasqueOhttpClient: " << status.message();
}

absl::StatusOr<QuicUrl> ParseUrl(const std::string& url_string) {
  QuicUrl url(url_string, "https");
  if (url.host().empty() && !absl::StrContains(url_string, "://")) {
    url = QuicUrl(absl::StrCat("https://", url_string));
  }
  if (url.host().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse OHTTP key URL ", url_string));
  }
  return url;
}

absl::Status MasqueOhttpClient::StartKeyFetch() {
  if (config_.skip_ohttp()) {
    return HandleKeyData("");
  }
  std::string decoded_key_data;
  if (absl::HexStringToBytes(config_.key_fetch_url(), &decoded_key_data) &&
      !decoded_key_data.empty()) {
    return HandleKeyData(decoded_key_data);
  }
  QuicUrl url(config_.key_fetch_url(), "https");
  if (url.host().empty() &&
      !absl::StrContains(config_.key_fetch_url(), "://")) {
    url = QuicUrl(absl::StrCat("https://", config_.key_fetch_url()));
  }
  if (url.host().empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse OHTTP key URL \"", config_.key_fetch_url(), "\""));
  }
  Message request;
  request.headers[":method"] = "GET";
  request.headers[":scheme"] = url.scheme();
  request.headers[":authority"] = url.HostPort();
  request.headers[":path"] = url.PathParamsQuery();
  request.headers["accept"] = "application/ohttp-keys";
  for (const std::pair<std::string, std::string>& header :
       config_.key_fetch_headers()) {
    request.headers[header.first] = header.second;
  }

  absl::StatusOr<RequestId> request_id =
      connection_pool_.SendRequest(request, /*mtls=*/false);
  if (!request_id.ok()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to send OHTTP key fetch request: ",
                     request_id.status().message()));
  }
  key_fetch_request_id_ = *request_id;
  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::CheckStatusAndContentType(
    const Message& response, const std::string& content_type,
    std::optional<uint16_t> expected_status_code) {
  int16_t status_code = MasqueConnectionPool::GetStatusCode(response);
  if (expected_status_code.has_value()) {
    if (status_code != *expected_status_code) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Unexpected status code in ", content_type,
          " response: ", status_code, ", expected: ", *expected_status_code));
    }
    if (*expected_status_code < 200 || *expected_status_code >= 300) {
      // If we expect a failure status code, skip the content-type check.
      return absl::OkStatus();
    }
  } else {
    if (status_code < 200 || status_code >= 300) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Bad status code in ", content_type, " response: ", status_code));
    }
  }
  auto content_type_it = response.headers.find("content-type");
  if (content_type_it == response.headers.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat("No content-type header in ", content_type, " response."));
  }
  std::vector<absl::string_view> content_type_split =
      absl::StrSplit(content_type_it->second, absl::MaxSplits(';', 1));
  absl::string_view content_type_without_params = content_type_split[0];
  quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(
      &content_type_without_params);
  if (content_type_without_params != content_type) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unexpected content-type in ", content_type,
                     " response: ", content_type_it->second));
  }
  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::CheckEncapsulatedStatus(
    const Message& response, std::optional<uint16_t> expected_status_code) {
  int16_t encapsulated_status_code =
      MasqueConnectionPool::GetStatusCode(response);
  absl::Status status = absl::OkStatus();
  if (expected_status_code.has_value()) {
    if (encapsulated_status_code != *expected_status_code) {
      status = absl::InvalidArgumentError(absl::StrCat(
          "Unexpected encapsulated status code: ", encapsulated_status_code,
          ", expected: ", *expected_status_code));
    }
  } else if (encapsulated_status_code < 200 ||
             encapsulated_status_code >= 300) {
    status = absl::InvalidArgumentError(absl::StrCat(
        "Bad encapsulated status code: ", encapsulated_status_code));
  }
  if (!status.ok() && !response.body.empty()) {
    QUICHE_LOG(INFO) << ENDPOINT << "Body:" << std::endl << response.body;
  }
  return status;
}

absl::Status MasqueOhttpClient::HandleKeyResponse(
    absl::StatusOr<Message>&& response) {
  key_fetch_request_id_ = std::nullopt;

  if (!response.ok()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to fetch OHTTP keys: ", response.status().message()));
  }
  QUICHE_LOG(INFO) << ENDPOINT << "Received OHTTP keys response: "
                   << response->headers.DebugString();
  QUICHE_RETURN_IF_ERROR(CheckStatusAndContentType(
      *response, "application/ohttp-keys", std::nullopt));

  run_details_.key_fetch_response = std::move(*response);
  return HandleKeyData(run_details_.key_fetch_response.body);
}

absl::Status MasqueOhttpClient::HandleKeyData(const std::string& key_data) {
  if (config_.skip_ohttp()) {
    for (const auto& per_request_config : config_.per_request_configs()) {
      QUICHE_RETURN_IF_ERROR(SendDirectRequest(per_request_config));
    }
    return absl::OkStatus();
  }
  absl::StatusOr<ObliviousHttpKeyConfigs> key_configs =
      ObliviousHttpKeyConfigs::ParseConcatenatedKeys(key_data);
  if (!key_configs.ok()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to parse OHTTP keys: ", key_configs.status().message()));
  }
  QUICHE_LOG(INFO) << ENDPOINT << "Successfully got " << key_configs->NumKeys()
                   << " OHTTP keys: " << std::endl
                   << key_configs->DebugString();
  if (config_.per_request_configs().empty()) {
    return absl::InvalidArgumentError("No OHTTP URLs to request, exiting.");
  }
  relay_url_ = QuicUrl(config_.relay_url(), "https");
  if (relay_url_.host().empty() &&
      !absl::StrContains(config_.relay_url(), "://")) {
    relay_url_ = QuicUrl(absl::StrCat("https://", config_.relay_url()));
  }
  QUICHE_LOG(INFO) << ENDPOINT << "Using relay URL: " << relay_url_.ToString();
  ObliviousHttpHeaderKeyConfig key_config = key_configs->PreferredConfig();
  if (key_configs->NumKeys() > 1) {
    QUICHE_LOG(INFO) << ENDPOINT << "Using key ID: "
                     << static_cast<int>(key_config.GetKeyId());
  }
  absl::StatusOr<absl::string_view> public_key =
      key_configs->GetPublicKeyForId(key_config.GetKeyId());
  if (!public_key.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to get OHTTP public key for key ID ",
                     static_cast<int>(key_config.GetKeyId()), ": ",
                     public_key.status().message()));
  }

  absl::StatusOr<ObliviousHttpClient> ohttp_client =
      ObliviousHttpClient::Create(*public_key, key_config);
  if (!ohttp_client.ok()) {
    return absl::InternalError(absl::StrCat("Failed to create OHTTP client: ",
                                            ohttp_client.status().message()));
  }
  ohttp_client_.emplace(std::move(*ohttp_client));

  bool first_request = true;
  for (size_t i = 0; i < config_.per_request_configs().size(); ++i) {
    if (first_request || config_.send_requests_in_parallel()) {
      QUICHE_RETURN_IF_ERROR(
          SendOhttpRequest(config_.per_request_configs()[i]));
      first_request = false;
    } else {
      unstarted_requests_.push_back(i);
    }
  }
  return absl::OkStatus();
}

void MasqueOhttpClient::MaybeStartNextRequest() {
  if (unstarted_requests_.empty()) {
    return;
  }
  absl::Status status = SendOhttpRequest(
      config_.per_request_configs()[unstarted_requests_.front()]);
  unstarted_requests_.pop_front();
  if (!status.ok()) {
    Abort(status);
  }
}

absl::Status MasqueOhttpClient::SendOhttpRequest(
    const Config::PerRequestConfig& per_request_config) {
  QuicUrl url(per_request_config.url(), "https");
  if (url.host().empty() &&
      !absl::StrContains(per_request_config.url(), "://")) {
    url = QuicUrl(absl::StrCat("https://", per_request_config.url()));
  }
  if (url.host().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse URL ", per_request_config.url()));
  }
  BinaryHttpRequest::ControlData control_data;
  std::string post_data = per_request_config.post_data();
  control_data.method = per_request_config.method();
  control_data.scheme = url.scheme();
  control_data.authority = url.HostPort();
  control_data.path = url.PathParamsQuery();
  std::string encrypted_data;
  PendingRequest pending_request(per_request_config);
  if (!ohttp_client_.has_value()) {
    QUICHE_LOG(FATAL) << ENDPOINT
                      << "Cannot send OHTTP request without OHTTP client";
    return absl::InternalError(
        "Cannot send OHTTP request without OHTTP client");
  }
  std::string encoded_data;
  int num_bhttp_chunks = per_request_config.num_bhttp_chunks();
  if (num_bhttp_chunks < 0) {
    num_bhttp_chunks = (per_request_config.num_ohttp_chunks() > 0 ? 1 : 0);
  }
  if (num_bhttp_chunks > 0) {
    pending_request.encoder.emplace();
    QUICHE_ASSIGN_OR_RETURN(
        encoded_data, pending_request.encoder->EncodeControlData(control_data));
    std::vector<quiche::BinaryHttpMessage::FieldView> headers;
    for (const std::pair<std::string, std::string>& header :
         per_request_config.headers()) {
      headers.push_back({header.first, header.second});
    }
    if (config_.handle_gzip_response()) {
      headers.push_back({"accept-encoding", "gzip"});
    }
    QUICHE_ASSIGN_OR_RETURN(
        std::string encoded_headers,
        pending_request.encoder->EncodeHeaders(absl::MakeSpan(headers)));
    encoded_data += encoded_headers;
    if (!per_request_config.ping_pong_mode()) {
      if (!post_data.empty()) {
        QUICHE_ASSIGN_OR_RETURN(std::vector<absl::string_view> body_chunks,
                                SplitIntoChunks(post_data, num_bhttp_chunks));
        QUICHE_ASSIGN_OR_RETURN(std::string encoded_body,
                                pending_request.encoder->EncodeBodyChunks(
                                    absl::MakeSpan(body_chunks),
                                    /*body_chunks_done=*/false));
        encoded_data += encoded_body;
      }
      QUICHE_ASSIGN_OR_RETURN(std::string encoded_final_chunk,
                              pending_request.encoder->EncodeBodyChunks(
                                  {}, /*body_chunks_done=*/true));
      encoded_data += encoded_final_chunk;
      std::vector<quiche::BinaryHttpMessage::FieldView> trailers;
      QUICHE_ASSIGN_OR_RETURN(
          std::string encoded_trailers,
          pending_request.encoder->EncodeTrailers(absl::MakeSpan(trailers)));
      encoded_data += encoded_trailers;
    }
  } else {
    BinaryHttpRequest binary_request(control_data);
    for (const std::pair<std::string, std::string>& header :
         per_request_config.headers()) {
      binary_request.AddHeaderField({header.first, header.second});
    }
    if (config_.handle_gzip_response()) {
      binary_request.AddHeaderField({"accept-encoding", "gzip"});
    }
    binary_request.set_body(post_data);
    QUICHE_ASSIGN_OR_RETURN(encoded_data, binary_request.Serialize());
  }
  int num_ohttp_chunks = per_request_config.num_ohttp_chunks();
  if (num_ohttp_chunks > 0) {
    pending_request.chunk_handler =
        std::make_unique<ChunkHandler>(config_.handle_gzip_response(), info_);
    QUICHE_ASSIGN_OR_RETURN(
        ChunkedObliviousHttpClient chunked_client,
        ChunkedObliviousHttpClient::Create(
            ohttp_client_->GetPublicKey(), ohttp_client_->GetKeyConfig(),
            pending_request.chunk_handler.get()));
    QUICHE_ASSIGN_OR_RETURN(std::vector<absl::string_view> ohttp_chunks,
                            SplitIntoChunks(encoded_data, num_ohttp_chunks));

    for (size_t i = 0; i < ohttp_chunks.size(); i++) {
      bool is_final_chunk = (i == ohttp_chunks.size() - 1 &&
                             !per_request_config.ping_pong_mode());
      QUICHE_ASSIGN_OR_RETURN(
          std::string ohttp_chunk,
          chunked_client.EncryptRequestChunk(ohttp_chunks[i], is_final_chunk));
      encrypted_data += ohttp_chunk;
    }

    pending_request.chunk_handler->SetChunkedClient(std::move(chunked_client));
  } else {
    QUICHE_ASSIGN_OR_RETURN(
        ObliviousHttpRequest ohttp_request,
        ohttp_client_->CreateObliviousHttpRequest(encoded_data));
    encrypted_data = ohttp_request.EncapsulateAndSerialize();
    pending_request.context.emplace(std::move(ohttp_request).ReleaseContext());
  }
  Message request;
  request.headers[":method"] = "POST";
  request.headers[":scheme"] = relay_url_.scheme();
  request.headers[":authority"] = relay_url_.HostPort();
  request.headers[":path"] = relay_url_.PathParamsQuery();
  request.headers["content-type"] =
      num_ohttp_chunks > 0 ? "message/ohttp-chunked-req" : "message/ohttp-req";
  for (const std::pair<std::string, std::string>& header :
       per_request_config.outer_headers()) {
    request.headers[header.first] = header.second;
  }
  QUICHE_VLOG(1) << "Sending encrypted request: "
                 << absl::BytesToHexString(encrypted_data);
  request.body = encrypted_data;
  bool end_stream =
      num_ohttp_chunks <= 0 || !per_request_config.ping_pong_mode();
  bool stream_response = num_ohttp_chunks > 0;
  absl::StatusOr<RequestId> request_id = connection_pool_.SendRequest(
      request, /*mtls=*/true, end_stream, stream_response);
  if (!request_id.ok()) {
    return absl::InternalError(absl::StrCat("Failed to send OHTTP request: ",
                                            request_id.status().message()));
  }

  QUICHE_LOG(INFO) << ENDPOINT << "Sent OHTTP request for "
                   << per_request_config.url();

  if (pending_request.chunk_handler) {
    pending_request.chunk_handler->SetResponseChunkCallback(
        [this, request_id = *request_id](absl::string_view chunk) {
          if (response_visitor_) {
            response_visitor_->OnResponseChunk(request_id, chunk);
          }
        });
  }
  pending_ohttp_requests_.insert({*request_id, std::move(pending_request)});

  if (response_visitor_) {
    response_visitor_->OnRequestStarted(*request_id, this);
  }

  return absl::OkStatus();
}

absl::Status MasqueOhttpClient::SendDirectRequest(
    const Config::PerRequestConfig& per_request_config) {
  QuicUrl url(per_request_config.url(), "https");
  if (url.host().empty() &&
      !absl::StrContains(per_request_config.url(), "://")) {
    url = QuicUrl(absl::StrCat("https://", per_request_config.url()));
  }
  if (url.host().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse URL ", per_request_config.url()));
  }
  Message request;
  std::string post_data = per_request_config.post_data();
  request.headers[":method"] = per_request_config.method();
  request.headers[":scheme"] = url.scheme();
  request.headers[":authority"] = url.HostPort();
  request.headers[":path"] = url.PathParamsQuery();
  if (config_.handle_gzip_response()) {
    request.headers["accept-encoding"] = "gzip";
  }
  for (const std::pair<std::string, std::string>& header :
       per_request_config.headers()) {
    request.headers[header.first] = header.second;
  }
  request.body = per_request_config.post_data();
  absl::StatusOr<RequestId> request_id = connection_pool_.SendRequest(
      request, /*mtls=*/true, /*end_stream=*/true, /*stream_response=*/false);
  if (!request_id.ok()) {
    return absl::InternalError(absl::StrCat("Failed to send direct request: ",
                                            request_id.status().message()));
  }

  QUICHE_LOG(INFO) << ENDPOINT << "Sent direct request for "
                   << per_request_config.url();

  pending_ohttp_requests_.insert(
      {*request_id, PendingRequest(per_request_config)});

  return absl::OkStatus();
}

absl::StatusOr<Message> MasqueOhttpClient::TryExtractEncapsulatedResponse(
    RequestId request_id, quiche::ObliviousHttpRequest::Context& context,
    const Message& response) {
  if (!ohttp_client_.has_value()) {
    QUICHE_LOG(FATAL) << "Received OHTTP response without OHTTP client";
    return absl::InternalError("Received OHTTP response without OHTTP client");
  }
  QUICHE_ASSIGN_OR_RETURN(
      ObliviousHttpResponse ohttp_response,
      ohttp_client_->DecryptObliviousHttpResponse(response.body, context));
  QUICHE_LOG(INFO) << ENDPOINT << "Received OHTTP response for " << request_id;
  QUICHE_VLOG(2) << "Decrypted unchunked response body: "
                 << absl::BytesToHexString(ohttp_response.GetPlaintextData());
  quiche::QuicheDataReader reader(ohttp_response.GetPlaintextData());
  uint64_t framing_indicator;
  if (!reader.ReadVarInt62(&framing_indicator)) {
    return absl::InvalidArgumentError(
        "Failed to read framing indicator for unchunked response");
  }
  if (framing_indicator == kFixedSizeResponseFramingIndicator) {
    absl::StatusOr<BinaryHttpResponse> binary_response =
        BinaryHttpResponse::Create(ohttp_response.GetPlaintextData());
    QUICHE_RETURN_IF_ERROR(binary_response.status());
    Message encapsulated_response;
    encapsulated_response.headers[":status"] =
        absl::StrCat(binary_response->status_code());
    for (const quiche::BinaryHttpMessage::Field& field :
         binary_response->GetHeaderFields()) {
      encapsulated_response.headers[field.name] = field.value;
    }
    encapsulated_response.body = binary_response->body();
    return encapsulated_response;
  }
  ChunkHandler chunk_handler(config_.handle_gzip_response(), info_);
  chunk_handler.SetResponseChunkCallback(
      [this, request_id](absl::string_view chunk) {
        if (response_visitor_) {
          response_visitor_->OnResponseChunk(request_id, chunk);
        }
      });
  QUICHE_RETURN_IF_ERROR(
      chunk_handler.OnDecryptedChunk(ohttp_response.GetPlaintextData()));
  QUICHE_RETURN_IF_ERROR(chunk_handler.OnChunksDone());
  QUICHE_ASSIGN_OR_RETURN(ChunkedObliviousHttpClient chunked_client,
                          ChunkedObliviousHttpClient::Create(
                              ohttp_client_->GetPublicKey(),
                              ohttp_client_->GetKeyConfig(), &chunk_handler));
  QUICHE_RETURN_IF_ERROR(
      chunked_client.DecryptResponse(ohttp_response.GetPlaintextData(),
                                     /*end_stream=*/true));
  return std::move(chunk_handler).ExtractResponse();
}

absl::Status MasqueOhttpClient::ProcessOhttpResponse(
    RequestId request_id, absl::StatusOr<Message>&& response, bool end_stream) {
  auto it = pending_ohttp_requests_.find(request_id);
  if (it == pending_ohttp_requests_.end()) {
    return absl::InternalError(absl::StrCat(
        "Received unexpected response for unknown request ", request_id));
  }
  auto cleanup = absl::MakeCleanup([this, it, end_stream]() {
    if (end_stream) {
      pending_ohttp_requests_.erase(it);
      MaybeStartNextRequest();
    }
  });
  if (!response.ok()) {
    if (it->second.per_request_config.expected_gateway_error().has_value()) {
      std::string expected_errors =
          *it->second.per_request_config.expected_gateway_error();
      std::vector<absl::string_view> expected_error_parts =
          absl::StrSplit(expected_errors, '|');
      for (const absl::string_view expected_error : expected_error_parts) {
        if (absl::StrContains(response.status().message(), expected_error)) {
          return absl::OkStatus();
        }
      }
    }
    return response.status();
  }
  run_details_.ohttp_responses.push_back(RunDetails::OhttpResponse{});
  if (config_.skip_ohttp()) {
    return ProcessEncapsulatedResponse(request_id, *response,
                                       it->second.per_request_config);
  }
  run_details_.ohttp_responses.back().gateway_response = std::move(*response);
  Message& resp = run_details_.ohttp_responses.back().gateway_response;
  std::string content_type =
      it->second.per_request_config.num_ohttp_chunks() > 0
          ? "message/ohttp-chunked-res"
          : "message/ohttp-res";
  std::optional<uint16_t> expected_gateway_status_code =
      it->second.per_request_config.expected_gateway_status_code();
  absl::Status status = CheckStatusAndContentType(resp, content_type,
                                                  expected_gateway_status_code);
  if (!status.ok()) {
    if (!resp.body.empty()) {
      QUICHE_LOG(ERROR) << ENDPOINT << "Bad " << content_type
                        << " with body:" << std::endl
                        << resp.body;
    } else {
      QUICHE_LOG(ERROR) << ENDPOINT << "Bad " << content_type
                        << " with empty body";
    }
    return status;
  }
  if (expected_gateway_status_code.has_value() &&
      (*expected_gateway_status_code < 200 ||
       *expected_gateway_status_code >= 300)) {
    // If we expect a failure status code, skip decapsulation.
    return absl::OkStatus();
  }

  if (!end_stream) {
    // We delivered headers. For chunked OHTTP, we just wait for data chunks.
    if (it->second.per_request_config.num_ohttp_chunks() <= 0) {
      QUICHE_LOG(ERROR) << ENDPOINT
                        << "Received partial response for non-chunked OHTTP";
      return absl::InternalError(
          "Received partial response for non-chunked OHTTP");
    }
    return absl::OkStatus();
  }

  std::optional<Message> encapsulated_response;
  QUICHE_VLOG(2) << "Received encrypted response body: "
                 << absl::BytesToHexString(resp.body);
  if (it->second.per_request_config.num_ohttp_chunks() > 0) {
    absl::Status decrypt_status = it->second.chunk_handler->DecryptChunk(
        /*encrypted_chunk=*/"", /*end_stream=*/true);
    if (!decrypt_status.ok()) {
      return decrypt_status;
    }
    encapsulated_response =
        std::move(*it->second.chunk_handler).ExtractResponse();
  } else {
    if (!it->second.context.has_value()) {
      QUICHE_LOG(FATAL) << "Received OHTTP response without OHTTP context";
      return absl::InternalError(
          "Received OHTTP response without OHTTP context");
    }
    QUICHE_ASSIGN_OR_RETURN(
        encapsulated_response,
        TryExtractEncapsulatedResponse(request_id, *it->second.context, resp));
  }
  QUICHE_LOG(INFO) << ENDPOINT
                   << "Successfully decapsulated response for request ID "
                   << request_id << ". Body length is "
                   << encapsulated_response->body.size() << ". Headers:"
                   << encapsulated_response->headers.DebugString();

  return ProcessEncapsulatedResponse(request_id, *encapsulated_response,
                                     it->second.per_request_config);
}

absl::Status MasqueOhttpClient::ProcessEncapsulatedResponse(
    RequestId request_id, Message& response,
    const Config::PerRequestConfig& per_request_config) {
  run_details_.ohttp_responses.back().encapsulated_response =
      std::move(response);
  Message& resp = run_details_.ohttp_responses.back().encapsulated_response;
  if (config_.handle_gzip_response()) {
    auto content_encoding_it = resp.headers.find("content-encoding");
    if (content_encoding_it != resp.headers.end() &&
        absl::EqualsIgnoreCase(content_encoding_it->second, "gzip")) {
      size_t compressed_size = resp.body.size();
      QUICHE_ASSIGN_OR_RETURN(std::string decompressed_body,
                              GzipDecompress(resp.body));
      QUICHE_LOG(INFO) << ENDPOINT
                       << "Successfully decompressed gzip response from size "
                       << compressed_size << " to size "
                       << decompressed_body.size();
      resp.body = std::move(decompressed_body);
    }
  }
  std::cout << resp.body;
  if (!resp.body.empty() && resp.body[resp.body.size() - 1] != '\n') {
    std::cout << std::endl;
  }
  QUICHE_RETURN_IF_ERROR(CheckEncapsulatedStatus(
      resp, per_request_config.expected_encapsulated_status_code()));
  if (const auto& callback =
          per_request_config.encapsulated_response_body_callback();
      callback) {
    QUICHE_RETURN_IF_ERROR(callback(resp.body));
  }

  if (response_visitor_) {
    response_visitor_->OnResponseDone(request_id, resp);
  }

  return absl::OkStatus();
}

void MasqueOhttpClient::OnPoolResponse(MasqueConnectionPool* /*pool*/,
                                       RequestId request_id,
                                       absl::StatusOr<Message>&& response,
                                       bool end_stream) {
  if (key_fetch_request_id_.has_value() &&
      *key_fetch_request_id_ == request_id) {
    absl::Status status = HandleKeyResponse(std::move(response));
    if (!status.ok()) {
      Abort(status);
    }
  } else {
    absl::Status status =
        ProcessOhttpResponse(request_id, std::move(response), end_stream);
    if (!status.ok()) {
      Abort(status);
      if (response_visitor_) {
        response_visitor_->OnError(request_id, status);
      }
    }
  }
}

void MasqueOhttpClient::OnPoolData(MasqueConnectionPool* /*pool*/,
                                   RequestId request_id, absl::string_view data,
                                   bool end_stream) {
  if (key_fetch_request_id_.has_value() &&
      *key_fetch_request_id_ == request_id) {
    Abort(absl::InternalError(absl::StrCat(
        "Received data for non-streamed key fetch request ", request_id)));
    return;
  }
  auto it = pending_ohttp_requests_.find(request_id);
  if (it == pending_ohttp_requests_.end()) {
    Abort(absl::InternalError(absl::StrCat(
        "Received data for unknown or non-OHTTP request: ", request_id)));
    return;
  }
  PendingRequest& pending_request = it->second;

  if (!pending_request.chunk_handler) {
    Abort(absl::InternalError(
        absl::StrCat("Received data for non-streamed request ", request_id)));
    return;
  }

  auto cleanup = absl::MakeCleanup([this, it, end_stream]() {
    if (end_stream) {
      pending_ohttp_requests_.erase(it);
      MaybeStartNextRequest();
    }
  });

  run_details_.ohttp_responses.back().gateway_response.body += data;

  absl::Status status =
      pending_request.chunk_handler->DecryptChunk(data, end_stream);
  if (!status.ok()) {
    Abort(status);
    if (response_visitor_) {
      response_visitor_->OnError(request_id, status);
    }
    return;
  }
  if (end_stream) {
    run_details_.ohttp_responses.back().encapsulated_response =
        std::move(*pending_request.chunk_handler).ExtractResponse();
    const Message& response =
        run_details_.ohttp_responses.back().encapsulated_response;
    status = CheckEncapsulatedStatus(
        response,
        pending_request.per_request_config.expected_encapsulated_status_code());
    if (!status.ok()) {
      Abort(status);
      if (response_visitor_) {
        response_visitor_->OnError(request_id, status);
      }
      return;
    }

    if (const auto& callback = pending_request.per_request_config
                                   .encapsulated_response_body_callback();
        callback) {
      status = callback(response.body);
      if (!status.ok()) {
        Abort(status);
        if (response_visitor_) {
          response_visitor_->OnError(request_id, status);
        }
        return;
      }
    }
    if (response_visitor_) {
      response_visitor_->OnResponseDone(request_id, response);
    }
  }
}

absl::Status MasqueOhttpClient::SendBodyChunk(RequestId request_id,
                                              absl::string_view chunk,
                                              bool is_final) {
  if (chunk.empty() && !is_final) {
    return absl::OkStatus();
  }
  auto it = pending_ohttp_requests_.find(request_id);
  if (it == pending_ohttp_requests_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Request ", request_id, " not found"));
  }
  PendingRequest& pending_request = it->second;
  if (!pending_request.encoder.has_value()) {
    if (!chunk.empty()) {
      return absl::FailedPreconditionError(
          "Cannot send non-empty body chunks for known-length requests");
    }
    std::string encoded_data;
    auto& chunked_client = pending_request.chunk_handler->chunked_client();
    if (!chunked_client.has_value()) {
      return absl::FailedPreconditionError("Chunked client not initialized");
    }
    QUICHE_ASSIGN_OR_RETURN(
        std::string encrypted_chunk,
        chunked_client->EncryptRequestChunk(encoded_data, is_final));
    return connection_pool_.SendBodyChunk(request_id, encrypted_chunk,
                                          is_final);
  }

  std::string encoded_data;
  std::vector<absl::string_view> body_chunks;
  if (!chunk.empty()) {
    body_chunks.push_back(chunk);
  }

  QUICHE_ASSIGN_OR_RETURN(
      encoded_data,
      pending_request.encoder->EncodeBodyChunks(absl::MakeSpan(body_chunks),
                                                /*body_chunks_done=*/is_final));

  if (is_final) {
    std::vector<quiche::BinaryHttpMessage::FieldView> trailers;
    QUICHE_ASSIGN_OR_RETURN(
        std::string encoded_trailers,
        pending_request.encoder->EncodeTrailers(absl::MakeSpan(trailers)));
    encoded_data += encoded_trailers;
  }

  auto& chunked_client = pending_request.chunk_handler->chunked_client();
  if (!chunked_client.has_value()) {
    return absl::FailedPreconditionError("Chunked client not initialized");
  }

  QUICHE_ASSIGN_OR_RETURN(
      std::string encrypted_chunk,
      chunked_client->EncryptRequestChunk(encoded_data, is_final));

  return connection_pool_.SendBodyChunk(request_id, encrypted_chunk, is_final);
}

absl::Status MasqueOhttpClient::ChunkHandler::DecryptChunk(
    absl::string_view encrypted_chunk, bool end_stream) {
  if (!chunked_client_.has_value()) {
    QUICHE_LOG(FATAL) << "DecryptChunk called without a chunked client";
    return absl::InternalError("DecryptChunk called without a chunked client");
  }
  return chunked_client_->DecryptResponse(encrypted_chunk, end_stream);
}

absl::Status MasqueOhttpClient::ChunkHandler::OnDecryptedChunk(
    absl::string_view decrypted_chunk) {
  decrypted_chunk_count_++;
  QUICHE_LOG(INFO) << ENDPOINT << "Received decrypted chunk #"
                   << decrypted_chunk_count_ << " of size "
                   << decrypted_chunk.size();
  absl::StrAppend(&buffered_binary_response_, decrypted_chunk);
  if (!is_chunked_response_.has_value()) {
    quiche::QuicheDataReader reader(buffered_binary_response_);
    uint64_t framing_indicator;
    if (!reader.ReadVarInt62(&framing_indicator)) {
      // Not enough data to read the framing indicator yet.
      return absl::OkStatus();
    }
    is_chunked_response_ =
        framing_indicator != kFixedSizeResponseFramingIndicator;
  }
  if (*is_chunked_response_) {
    return decoder_.Decode(decrypted_chunk, /*end_stream=*/false);
  } else {
    // Buffer and wait for OnChunksDone().
    return absl::OkStatus();
  }
}
absl::Status MasqueOhttpClient::ChunkHandler::OnChunksDone() {
  QUICHE_VLOG(2) << "Decrypted chunked response body: "
                 << absl::BytesToHexString(buffered_binary_response_);
  if (!is_chunked_response_.has_value()) {
    return absl::InvalidArgumentError(
        "OnChunksDone called without framing indicator");
  }
  if (*is_chunked_response_) {
    return decoder_.Decode("", /*end_stream=*/true);
  } else {
    absl::StatusOr<BinaryHttpResponse> binary_response =
        BinaryHttpResponse::Create(buffered_binary_response_);
    QUICHE_RETURN_IF_ERROR(binary_response.status());
    response_.headers[":status"] = absl::StrCat(binary_response->status_code());
    for (const quiche::BinaryHttpMessage::Field& field :
         binary_response->GetHeaderFields()) {
      response_.headers[field.name] = field.value;
    }
    response_.body = binary_response->body();
    return absl::OkStatus();
  }
}

absl::Status MasqueOhttpClient::ChunkHandler::OnInformationalResponseStatusCode(
    uint16_t status_code) {
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnInformationalResponseHeader(
    absl::string_view name, absl::string_view value) {
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnInformationalResponseDone() {
  return absl::OkStatus();
}
absl::Status
MasqueOhttpClient::ChunkHandler::OnInformationalResponsesSectionDone() {
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnFinalResponseStatusCode(
    uint16_t status_code) {
  response_.headers[":status"] = absl::StrCat(status_code);
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnFinalResponseHeader(
    absl::string_view name, absl::string_view value) {
  response_.headers[name] = value;
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnFinalResponseHeadersDone() {
  QUICHE_LOG(INFO) << ENDPOINT << "Received chunked OHTTP response headers: "
                   << response_.headers.DebugString();
  if (handle_gzip_response_) {
    auto it = response_.headers.find("content-encoding");
    if (it != response_.headers.end() &&
        absl::EqualsIgnoreCase(it->second, "gzip")) {
      is_gzipped_ = true;
    }
  }
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnBodyChunk(
    absl::string_view body_chunk) {
  body_chunk_count_++;
  QUICHE_LOG(INFO) << ENDPOINT << "Received body chunk #" << body_chunk_count_
                   << " of size " << body_chunk.size();

  if (body_chunk.empty()) {
    return absl::OkStatus();
  }

  std::string decompressed = "";
  absl::string_view processed_chunk = body_chunk;
  if (is_gzipped_) {
    if (!decompressor_) {
      QUICHE_ASSIGN_OR_RETURN(decompressor_, GzipDecompressor::Create());
    }
    QUICHE_ASSIGN_OR_RETURN(
        decompressed,
        decompressor_->Decompress(body_chunk, /*end_stream=*/false));
    processed_chunk = decompressed;
    QUICHE_LOG(INFO) << ENDPOINT << "Decompressed chunk to size "
                     << processed_chunk.size();
    if (processed_chunk.empty()) {
      return absl::OkStatus();
    }
  }

  std::cout << processed_chunk << std::flush;
  last_chunk_ended_without_newline_ =
      !processed_chunk.empty() &&
      processed_chunk[processed_chunk.size() - 1] != '\n';

  response_.body += processed_chunk;

  if (response_chunk_callback_) {
    response_chunk_callback_(processed_chunk);
  }

  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnBodyChunksDone() {
  if (decompressor_ != nullptr) {
    if (!decompressor_->IsFinished()) {
      return absl::InternalError("Gzip stream truncated");
    }
    decompressor_->EndDecompression();
  }
  if (last_chunk_ended_without_newline_) {
    std::cout << std::endl;
  }
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnTrailer(
    absl::string_view name, absl::string_view value) {
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnTrailersDone() {
  return absl::OkStatus();
}

}  // namespace quic

#undef ENDPOINT
