// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_ohttp_client.h"

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
#include "absl/strings/numbers.h"
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

}  // namespace

absl::Status MasqueOhttpClient::Config::PerRequestConfig::AddHeaders(
    const std::vector<std::string>& headers) {
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
    headers_.push_back({std::move(header_name), std::move(header_value)});
  }
  return absl::OkStatus();
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

MasqueOhttpClient::MasqueOhttpClient(Config config,
                                     quic::QuicEventLoop* event_loop)
    : config_(std::move(config)),
      connection_pool_(event_loop, config_.key_fetch_ssl_ctx(),
                       config_.disable_certificate_verification(),
                       config_.dns_config(), this) {
  connection_pool_.SetMtlsSslCtx(config_.ohttp_ssl_ctx());
}

// static
absl::Status MasqueOhttpClient::Run(Config config) {
  if (config.per_request_configs().empty()) {
    return absl::InvalidArgumentError("No OHTTP URLs to request");
  }
  if (config.key_fetch_ssl_ctx() == nullptr) {
    QUICHE_RETURN_IF_ERROR(config.ConfigureKeyFetchClientCert("", ""));
  }
  if (config.ohttp_ssl_ctx() == nullptr) {
    QUICHE_RETURN_IF_ERROR(config.ConfigureOhttpMtls("", ""));
  }
  quiche::QuicheSystemEventLoop system_event_loop("masque_ohttp_client");
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());
  MasqueOhttpClient ohttp_client(std::move(config), event_loop.get());
  QUICHE_RETURN_IF_ERROR(ohttp_client.Start());
  while (!ohttp_client.IsDone()) {
    ohttp_client.connection_pool_.event_loop()->RunEventLoopOnce(
        quic::QuicTime::Delta::FromMilliseconds(50));
  }
  return ohttp_client.status_;
}

absl::Status MasqueOhttpClient::Start() {
  absl::Status status = StartKeyFetch(config_.key_fetch_url());
  if (!status.ok()) {
    Abort(status);
    return status;
  }
  return absl::OkStatus();
}
bool MasqueOhttpClient::IsDone() {
  if (!status_.ok()) {
    return true;
  }
  if (!ohttp_client_.has_value()) {
    // Key fetch request is still pending.
    return false;
  }
  return pending_ohttp_requests_.empty();
}

void MasqueOhttpClient::Abort(absl::Status status) {
  QUICHE_CHECK(!status.ok());
  if (!status_.ok()) {
    QUICHE_LOG(ERROR)
        << "MasqueOhttpClient already aborted, ignoring new error: "
        << status.message();
    return;
  }
  status_ = status;
  QUICHE_LOG(ERROR) << "Aborting MasqueOhttpClient: " << status_.message();
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

absl::Status MasqueOhttpClient::StartKeyFetch(const std::string& url_string) {
  QuicUrl url(url_string, "https");
  if (url.host().empty() && !absl::StrContains(url_string, "://")) {
    url = QuicUrl(absl::StrCat("https://", url_string));
  }
  if (url.host().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse OHTTP key URL \"", url_string, "\""));
  }
  Message request;
  request.headers[":method"] = "GET";
  request.headers[":scheme"] = url.scheme();
  request.headers[":authority"] = url.HostPort();
  request.headers[":path"] = url.PathParamsQuery();
  request.headers["accept"] = "application/ohttp-keys";

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
  auto status_it = response.headers.find(":status");
  if (status_it == response.headers.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat("No :status header in ", content_type, " response."));
  }
  int status_code;
  if (!absl::SimpleAtoi(status_it->second, &status_code)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse ", content_type, " status code."));
  }
  if (expected_status_code.has_value()) {
    if (status_code != *expected_status_code) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Unexpected status in ", content_type, " response: ", status_code,
          " (expected ", *expected_status_code, ")"));
    }
    if (*expected_status_code < 200 || *expected_status_code >= 300) {
      // If we expect a failure status code, skip the content-type check.
      return absl::OkStatus();
    }
  } else {
    if (status_code < 200 || status_code >= 300) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unexpected status in ", content_type,
                       " response: ", status_it->second));
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

absl::Status MasqueOhttpClient::HandleKeyResponse(
    const absl::StatusOr<Message>& response) {
  key_fetch_request_id_ = std::nullopt;

  if (!response.ok()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to fetch OHTTP keys: ", response.status().message()));
  }
  QUICHE_LOG(INFO) << "Received OHTTP keys response: "
                   << response->headers.DebugString();
  QUICHE_RETURN_IF_ERROR(CheckStatusAndContentType(
      *response, "application/ohttp-keys", std::nullopt));
  absl::StatusOr<ObliviousHttpKeyConfigs> key_configs =
      ObliviousHttpKeyConfigs::ParseConcatenatedKeys(response->body);
  if (!key_configs.ok()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to parse OHTTP keys: ", key_configs.status().message()));
  }
  QUICHE_LOG(INFO) << "Successfully got " << key_configs->NumKeys()
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
  QUICHE_LOG(INFO) << "Using relay URL: " << relay_url_.ToString();
  ObliviousHttpHeaderKeyConfig key_config = key_configs->PreferredConfig();
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

  for (const auto& per_request_config : config_.per_request_configs()) {
    QUICHE_RETURN_IF_ERROR(SendOhttpRequest(per_request_config));
  }
  return absl::OkStatus();
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
  control_data.method = post_data.empty() ? "GET" : "POST";
  control_data.scheme = url.scheme();
  control_data.authority = url.HostPort();
  control_data.path = url.PathParamsQuery();
  std::string encrypted_data;
  PendingRequest pending_request(per_request_config);
  if (!ohttp_client_.has_value()) {
    QUICHE_LOG(FATAL) << "Cannot send OHTTP request without OHTTP client";
    return absl::InternalError(
        "Cannot send OHTTP request without OHTTP client");
  }
  std::string encoded_data;
  const bool use_indeterminate_length =
      per_request_config.use_indeterminate_length().value_or(
          per_request_config.use_chunked_ohttp());
  if (use_indeterminate_length) {
    BinaryHttpRequest::IndeterminateLengthEncoder encoder;

    QUICHE_ASSIGN_OR_RETURN(encoded_data,
                            encoder.EncodeControlData(control_data));
    std::vector<quiche::BinaryHttpMessage::FieldView> headers;
    for (const std::pair<std::string, std::string>& header :
         per_request_config.headers()) {
      headers.push_back({header.first, header.second});
    }
    QUICHE_ASSIGN_OR_RETURN(std::string encoded_headers,
                            encoder.EncodeHeaders(absl::MakeSpan(headers)));
    encoded_data += encoded_headers;
    if (!post_data.empty()) {
      absl::string_view body = post_data;
      std::vector<absl::string_view> body_chunks;
      if (body.size() > 1) {
        // Intentionally split the data into two chunks to test body chunking.
        body_chunks.push_back(body.substr(0, body.size() / 2));
        body_chunks.push_back(body.substr(body.size() / 2));
      } else {
        body_chunks.push_back(body);
      }
      QUICHE_ASSIGN_OR_RETURN(
          std::string encoded_body,
          encoder.EncodeBodyChunks(absl::MakeSpan(body_chunks),
                                   /*body_chunks_done=*/false));
      encoded_data += encoded_body;
    }
    std::vector<absl::string_view> empty_body_chunks;
    QUICHE_ASSIGN_OR_RETURN(
        std::string encoded_final_chunk,
        encoder.EncodeBodyChunks(absl::MakeSpan(empty_body_chunks),
                                 /*body_chunks_done=*/true));
    encoded_data += encoded_final_chunk;
    std::vector<quiche::BinaryHttpMessage::FieldView> trailers;
    QUICHE_ASSIGN_OR_RETURN(std::string encoded_trailers,
                            encoder.EncodeTrailers(absl::MakeSpan(trailers)));
    encoded_data += encoded_trailers;
  } else {
    BinaryHttpRequest binary_request(control_data);
    for (const std::pair<std::string, std::string>& header :
         per_request_config.headers()) {
      binary_request.AddHeaderField({header.first, header.second});
    }
    binary_request.set_body(post_data);
    QUICHE_ASSIGN_OR_RETURN(encoded_data, binary_request.Serialize());
  }
  if (pending_request.per_request_config.use_chunked_ohttp()) {
    pending_request.chunk_handler = std::make_unique<ChunkHandler>();
    QUICHE_ASSIGN_OR_RETURN(
        ChunkedObliviousHttpClient chunked_client,
        ChunkedObliviousHttpClient::Create(
            ohttp_client_->GetPublicKey(), ohttp_client_->GetKeyConfig(),
            pending_request.chunk_handler.get()));
    // Intentionally split the data into two chunks to test encryption chunking.
    QUICHE_ASSIGN_OR_RETURN(encrypted_data,
                            chunked_client.EncryptRequestChunk(
                                absl::string_view(encoded_data).substr(0, 1),
                                /*is_final_chunk=*/false));
    QUICHE_ASSIGN_OR_RETURN(std::string encrypted_data2,
                            chunked_client.EncryptRequestChunk(
                                absl::string_view(encoded_data).substr(1),
                                /*is_final_chunk=*/true));
    encrypted_data += encrypted_data2;

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
      pending_request.per_request_config.use_chunked_ohttp()
          ? "message/ohttp-chunked-req"
          : "message/ohttp-req";
  request.body = encrypted_data;
  absl::StatusOr<RequestId> request_id =
      connection_pool_.SendRequest(request, /*mtls=*/true);
  if (!request_id.ok()) {
    return absl::InternalError(absl::StrCat("Failed to send request: ",
                                            request_id.status().message()));
  }
  QUICHE_LOG(INFO) << "Sent OHTTP request for " << per_request_config.url();

  pending_ohttp_requests_.insert({*request_id, std::move(pending_request)});
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
  QUICHE_LOG(INFO) << "Received OHTTP response for " << request_id;
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
  ChunkHandler chunk_handler;
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
    RequestId request_id, const absl::StatusOr<Message>& response) {
  auto it = pending_ohttp_requests_.find(request_id);
  if (it == pending_ohttp_requests_.end()) {
    return absl::InternalError(absl::StrCat(
        "Received unexpected response for unknown request ", request_id));
  }
  auto cleanup =
      absl::MakeCleanup([this, it]() { pending_ohttp_requests_.erase(it); });
  if (!response.ok()) {
    if (it->second.per_request_config.expected_gateway_error().has_value() &&
        absl::StrContains(
            response.status().message(),
            *it->second.per_request_config.expected_gateway_error())) {
      return absl::OkStatus();
    }
    return response.status();
  }
  int16_t gateway_status_code = MasqueConnectionPool::GetStatusCode(*response);
  if (it->second.per_request_config.expected_gateway_status_code()
          .has_value()) {
    if (gateway_status_code !=
        *it->second.per_request_config.expected_gateway_status_code()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Unexpected gateway status code: ", gateway_status_code, " != ",
          *it->second.per_request_config.expected_gateway_status_code()));
    }
  } else if (gateway_status_code < 200 || gateway_status_code >= 300) {
    return absl::InvalidArgumentError(
        absl::StrCat("Bad gateway status code: ", gateway_status_code));
  }
  std::string content_type = it->second.per_request_config.use_chunked_ohttp()
                                 ? "message/ohttp-chunked-res"
                                 : "message/ohttp-res";
  std::optional<uint16_t> expected_gateway_status_code =
      it->second.per_request_config.expected_gateway_status_code();
  absl::Status status = CheckStatusAndContentType(*response, content_type,
                                                  expected_gateway_status_code);
  if (!status.ok()) {
    if (!response->body.empty()) {
      QUICHE_LOG(ERROR) << "Bad " << content_type << " with body:" << std::endl
                        << response->body;
    } else {
      QUICHE_LOG(ERROR) << "Bad " << content_type << " with empty body";
    }
    return status;
  }
  if (expected_gateway_status_code.has_value() &&
      (*expected_gateway_status_code < 200 ||
       *expected_gateway_status_code >= 300)) {
    // If we expect a failure status code, skip decapsulation.
    return absl::OkStatus();
  }
  std::optional<Message> encapsulated_response;
  QUICHE_VLOG(2) << "Received encrypted response body: "
                 << absl::BytesToHexString(response->body);
  if (it->second.per_request_config.use_chunked_ohttp()) {
    QUICHE_ASSIGN_OR_RETURN(
        encapsulated_response,
        it->second.chunk_handler->DecryptFullResponse(response->body));
  } else {
    if (!it->second.context.has_value()) {
      QUICHE_LOG(FATAL) << "Received OHTTP response without OHTTP context";
      return absl::InternalError(
          "Received OHTTP response without OHTTP context");
    }
    QUICHE_ASSIGN_OR_RETURN(encapsulated_response,
                            TryExtractEncapsulatedResponse(
                                request_id, *it->second.context, *response));
  }
  QUICHE_LOG(INFO) << "Successfully decapsulated response for request ID "
                   << request_id << ". Body length is "
                   << encapsulated_response->body.size() << ". Headers:"
                   << encapsulated_response->headers.DebugString();
  std::cout << encapsulated_response->body;
  int16_t encapsulated_status_code =
      MasqueConnectionPool::GetStatusCode(*encapsulated_response);
  if (it->second.per_request_config.expected_encapsulated_status_code()
          .has_value()) {
    if (encapsulated_status_code !=
        *it->second.per_request_config.expected_encapsulated_status_code()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Unexpected encapsulated status code: ", encapsulated_status_code,
          " != ",
          *it->second.per_request_config.expected_encapsulated_status_code()));
    }
  } else if (encapsulated_status_code < 200 ||
             encapsulated_status_code >= 300) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Bad encapsulated status code: ", encapsulated_status_code));
  }
  if (it->second.per_request_config.expected_encapsulated_response_body()
          .has_value() &&
      encapsulated_response->body !=
          *it->second.per_request_config
               .expected_encapsulated_response_body()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unexpected encapsulated response body: \"",
        encapsulated_response->body, "\" != \"",
        *it->second.per_request_config.expected_encapsulated_response_body(),
        "\""));
  }
  return absl::OkStatus();
}

void MasqueOhttpClient::OnPoolResponse(MasqueConnectionPool* /*pool*/,
                                       RequestId request_id,
                                       absl::StatusOr<Message>&& response) {
  if (key_fetch_request_id_.has_value() &&
      *key_fetch_request_id_ == request_id) {
    absl::Status status = HandleKeyResponse(response);
    if (!status.ok()) {
      Abort(status);
    }
  } else {
    absl::Status status = ProcessOhttpResponse(request_id, response);
    if (!status.ok()) {
      Abort(status);
    }
  }
}

MasqueOhttpClient::ChunkHandler::ChunkHandler() : decoder_(this) {}

absl::StatusOr<Message> MasqueOhttpClient::ChunkHandler::DecryptFullResponse(
    absl::string_view encrypted_response) {
  if (!chunked_client_.has_value()) {
    QUICHE_LOG(FATAL) << "DecryptFullResponse called without a chunked client";
    return absl::InternalError(
        "DecryptFullResponse called without a chunked client");
  }
  QUICHE_RETURN_IF_ERROR(chunked_client_->DecryptResponse(encrypted_response,
                                                          /*end_stream=*/true));
  return std::move(response_);
}

absl::Status MasqueOhttpClient::ChunkHandler::OnDecryptedChunk(
    absl::string_view decrypted_chunk) {
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
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnBodyChunk(
    absl::string_view body_chunk) {
  response_.body += body_chunk;
  return absl::OkStatus();
}
absl::Status MasqueOhttpClient::ChunkHandler::OnBodyChunksDone() {
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
