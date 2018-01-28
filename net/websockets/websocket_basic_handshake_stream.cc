// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_basic_handshake_stream.h"

#include <stddef.h>
#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "crypto/random.h"
#include "net/base/io_buffer.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_body_drainer.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_stream_parser.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/websocket_transport_client_socket_pool.h"
#include "net/websockets/websocket_basic_stream.h"
#include "net/websockets/websocket_deflate_parameters.h"
#include "net/websockets/websocket_deflate_predictor.h"
#include "net/websockets/websocket_deflate_predictor_impl.h"
#include "net/websockets/websocket_deflate_stream.h"
#include "net/websockets/websocket_deflater.h"
#include "net/websockets/websocket_extension_parser.h"
#include "net/websockets/websocket_handshake_challenge.h"
#include "net/websockets/websocket_handshake_constants.h"
#include "net/websockets/websocket_handshake_request_info.h"
#include "net/websockets/websocket_handshake_response_info.h"
#include "net/websockets/websocket_stream.h"

namespace net {

namespace {

const char kConnectionErrorStatusLine[] = "HTTP/1.1 503 Connection Error";

}  // namespace

// TODO(ricea): If more extensions are added, replace this with a more general
// mechanism.
struct WebSocketExtensionParams {
  bool deflate_enabled = false;
  WebSocketDeflateParameters deflate_parameters;
};

namespace {

enum GetHeaderResult {
  GET_HEADER_OK,
  GET_HEADER_MISSING,
  GET_HEADER_MULTIPLE,
};

std::string MissingHeaderMessage(const std::string& header_name) {
  return std::string("'") + header_name + "' header is missing";
}

std::string MultipleHeaderValuesMessage(const std::string& header_name) {
  return
      std::string("'") +
      header_name +
      "' header must not appear more than once in a response";
}

std::string GenerateHandshakeChallenge() {
  std::string raw_challenge(websockets::kRawChallengeLength, '\0');
  crypto::RandBytes(base::string_as_array(&raw_challenge),
                    raw_challenge.length());
  std::string encoded_challenge;
  base::Base64Encode(raw_challenge, &encoded_challenge);
  return encoded_challenge;
}

void AddVectorHeaderIfNonEmpty(const char* name,
                               const std::vector<std::string>& value,
                               HttpRequestHeaders* headers) {
  if (value.empty())
    return;
  headers->SetHeader(name, base::JoinString(value, ", "));
}

GetHeaderResult GetSingleHeaderValue(const HttpResponseHeaders* headers,
                                     const base::StringPiece& name,
                                     std::string* value) {
  size_t iter = 0;
  size_t num_values = 0;
  std::string temp_value;
  while (headers->EnumerateHeader(&iter, name, &temp_value)) {
    if (++num_values > 1)
      return GET_HEADER_MULTIPLE;
    *value = temp_value;
  }
  return num_values > 0 ? GET_HEADER_OK : GET_HEADER_MISSING;
}

bool ValidateHeaderHasSingleValue(GetHeaderResult result,
                                  const std::string& header_name,
                                  std::string* failure_message) {
  if (result == GET_HEADER_MISSING) {
    *failure_message = MissingHeaderMessage(header_name);
    return false;
  }
  if (result == GET_HEADER_MULTIPLE) {
    *failure_message = MultipleHeaderValuesMessage(header_name);
    return false;
  }
  DCHECK_EQ(result, GET_HEADER_OK);
  return true;
}

bool ValidateUpgrade(const HttpResponseHeaders* headers,
                     std::string* failure_message) {
  std::string value;
  GetHeaderResult result =
      GetSingleHeaderValue(headers, websockets::kUpgrade, &value);
  if (!ValidateHeaderHasSingleValue(result,
                                    websockets::kUpgrade,
                                    failure_message)) {
    return false;
  }

  if (!base::LowerCaseEqualsASCII(value, websockets::kWebSocketLowercase)) {
    *failure_message =
        "'Upgrade' header value is not 'WebSocket': " + value;
    return false;
  }
  return true;
}

bool ValidateSecWebSocketAccept(const HttpResponseHeaders* headers,
                                const std::string& expected,
                                std::string* failure_message) {
  std::string actual;
  GetHeaderResult result =
      GetSingleHeaderValue(headers, websockets::kSecWebSocketAccept, &actual);
  if (!ValidateHeaderHasSingleValue(result,
                                    websockets::kSecWebSocketAccept,
                                    failure_message)) {
    return false;
  }

  if (expected != actual) {
    *failure_message = "Incorrect 'Sec-WebSocket-Accept' header value";
    return false;
  }
  return true;
}

bool ValidateConnection(const HttpResponseHeaders* headers,
                        std::string* failure_message) {
  // Connection header is permitted to contain other tokens.
  if (!headers->HasHeader(HttpRequestHeaders::kConnection)) {
    *failure_message = MissingHeaderMessage(HttpRequestHeaders::kConnection);
    return false;
  }
  if (!headers->HasHeaderValue(HttpRequestHeaders::kConnection,
                               websockets::kUpgrade)) {
    *failure_message = "'Connection' header value must contain 'Upgrade'";
    return false;
  }
  return true;
}

bool ValidateSubProtocol(
    const HttpResponseHeaders* headers,
    const std::vector<std::string>& requested_sub_protocols,
    std::string* sub_protocol,
    std::string* failure_message) {
  size_t iter = 0;
  std::string value;
  std::unordered_set<std::string> requested_set(requested_sub_protocols.begin(),
                                                requested_sub_protocols.end());
  int count = 0;
  bool has_multiple_protocols = false;
  bool has_invalid_protocol = false;

  while (!has_invalid_protocol || !has_multiple_protocols) {
    std::string temp_value;
    if (!headers->EnumerateHeader(&iter, websockets::kSecWebSocketProtocol,
                                  &temp_value))
      break;
    value = temp_value;
    if (requested_set.count(value) == 0)
      has_invalid_protocol = true;
    if (++count > 1)
      has_multiple_protocols = true;
  }

  if (has_multiple_protocols) {
    *failure_message =
        MultipleHeaderValuesMessage(websockets::kSecWebSocketProtocol);
    return false;
  } else if (count > 0 && requested_sub_protocols.size() == 0) {
    *failure_message =
        std::string("Response must not include 'Sec-WebSocket-Protocol' "
                    "header if not present in request: ")
        + value;
    return false;
  } else if (has_invalid_protocol) {
    *failure_message =
        "'Sec-WebSocket-Protocol' header value '" +
        value +
        "' in response does not match any of sent values";
    return false;
  } else if (requested_sub_protocols.size() > 0 && count == 0) {
    *failure_message =
        "Sent non-empty 'Sec-WebSocket-Protocol' header "
        "but no response was received";
    return false;
  }
  *sub_protocol = value;
  return true;
}

bool ValidateExtensions(const HttpResponseHeaders* headers,
                        std::string* accepted_extensions_descriptor,
                        std::string* failure_message,
                        WebSocketExtensionParams* params) {
  size_t iter = 0;
  std::string header_value;
  std::vector<std::string> header_values;
  // TODO(ricea): If adding support for additional extensions, generalise this
  // code.
  bool seen_permessage_deflate = false;
  while (headers->EnumerateHeader(&iter, websockets::kSecWebSocketExtensions,
                                  &header_value)) {
    WebSocketExtensionParser parser;
    if (!parser.Parse(header_value)) {
      // TODO(yhirano) Set appropriate failure message.
      *failure_message =
          "'Sec-WebSocket-Extensions' header value is "
          "rejected by the parser: " +
          header_value;
      return false;
    }

    const std::vector<WebSocketExtension>& extensions = parser.extensions();
    for (const auto& extension : extensions) {
      if (extension.name() == "permessage-deflate") {
        if (seen_permessage_deflate) {
          *failure_message = "Received duplicate permessage-deflate response";
          return false;
        }
        seen_permessage_deflate = true;
        auto& deflate_parameters = params->deflate_parameters;
        if (!deflate_parameters.Initialize(extension, failure_message) ||
            !deflate_parameters.IsValidAsResponse(failure_message)) {
          *failure_message = "Error in permessage-deflate: " + *failure_message;
          return false;
        }
        // Note that we don't have to check the request-response compatibility
        // here because we send a request compatible with any valid responses.
        // TODO(yhirano): Place a DCHECK here.

        header_values.push_back(header_value);
      } else {
        *failure_message = "Found an unsupported extension '" +
                           extension.name() +
                           "' in 'Sec-WebSocket-Extensions' header";
        return false;
      }
    }
  }
  *accepted_extensions_descriptor = base::JoinString(header_values, ", ");
  params->deflate_enabled = seen_permessage_deflate;
  return true;
}

}  // namespace

WebSocketBasicHandshakeStream::WebSocketBasicHandshakeStream(
    std::unique_ptr<ClientSocketHandle> connection,
    WebSocketStream::ConnectDelegate* connect_delegate,
    bool using_proxy,
    std::vector<std::string> requested_sub_protocols,
    std::vector<std::string> requested_extensions,
    WebSocketStreamRequest* request)
    : state_(std::move(connection),
             using_proxy,
             false /* http_09_on_non_default_ports_enabled */),
      connect_delegate_(connect_delegate),
      http_response_info_(nullptr),
      requested_sub_protocols_(requested_sub_protocols),
      requested_extensions_(requested_extensions),
      stream_request_(request) {
  DCHECK(connect_delegate);
  DCHECK(request);
}

WebSocketBasicHandshakeStream::~WebSocketBasicHandshakeStream() {}

int WebSocketBasicHandshakeStream::InitializeStream(
    const HttpRequestInfo* request_info,
    RequestPriority priority,
    const NetLogWithSource& net_log,
    const CompletionCallback& callback) {
  url_ = request_info->url;
  state_.Initialize(request_info, priority, net_log, callback);
  return OK;
}

int WebSocketBasicHandshakeStream::SendRequest(
    const HttpRequestHeaders& headers,
    HttpResponseInfo* response,
    const CompletionCallback& callback) {
  DCHECK(!headers.HasHeader(websockets::kSecWebSocketKey));
  DCHECK(!headers.HasHeader(websockets::kSecWebSocketProtocol));
  DCHECK(!headers.HasHeader(websockets::kSecWebSocketExtensions));
  DCHECK(headers.HasHeader(HttpRequestHeaders::kOrigin));
  DCHECK(headers.HasHeader(websockets::kUpgrade));
  DCHECK(headers.HasHeader(HttpRequestHeaders::kConnection));
  DCHECK(headers.HasHeader(websockets::kSecWebSocketVersion));
  DCHECK(parser());

  http_response_info_ = response;

  // Create a copy of the headers object, so that we can add the
  // Sec-WebSockey-Key header.
  HttpRequestHeaders enriched_headers;
  enriched_headers.CopyFrom(headers);
  std::string handshake_challenge;
  if (handshake_challenge_for_testing_) {
    handshake_challenge = *handshake_challenge_for_testing_;
    handshake_challenge_for_testing_.reset();
  } else {
    handshake_challenge = GenerateHandshakeChallenge();
  }
  enriched_headers.SetHeader(websockets::kSecWebSocketKey, handshake_challenge);

  AddVectorHeaderIfNonEmpty(websockets::kSecWebSocketExtensions,
                            requested_extensions_,
                            &enriched_headers);
  AddVectorHeaderIfNonEmpty(websockets::kSecWebSocketProtocol,
                            requested_sub_protocols_,
                            &enriched_headers);

  handshake_challenge_response_ =
      ComputeSecWebSocketAccept(handshake_challenge);

  DCHECK(connect_delegate_);
  std::unique_ptr<WebSocketHandshakeRequestInfo> request(
      new WebSocketHandshakeRequestInfo(url_, base::Time::Now()));
  request->headers.CopyFrom(enriched_headers);
  connect_delegate_->OnStartOpeningHandshake(std::move(request));

  return parser()->SendRequest(
      state_.GenerateRequestLine(), enriched_headers, response, callback);
}

int WebSocketBasicHandshakeStream::ReadResponseHeaders(
    const CompletionCallback& callback) {
  // HttpStreamParser uses a weak pointer when reading from the
  // socket, so it won't be called back after being destroyed. The
  // HttpStreamParser is owned by HttpBasicState which is owned by this object,
  // so this use of base::Unretained() is safe.
  int rv = parser()->ReadResponseHeaders(
      base::Bind(&WebSocketBasicHandshakeStream::ReadResponseHeadersCallback,
                 base::Unretained(this),
                 callback));
  if (rv == ERR_IO_PENDING)
    return rv;
  return ValidateResponse(rv);
}

int WebSocketBasicHandshakeStream::ReadResponseBody(
    IOBuffer* buf,
    int buf_len,
    const CompletionCallback& callback) {
  return parser()->ReadResponseBody(buf, buf_len, callback);
}

void WebSocketBasicHandshakeStream::Close(bool not_reusable) {
  // This class ignores the value of |not_reusable| and never lets the socket be
  // re-used.
  if (parser())
    parser()->Close(true);
}

bool WebSocketBasicHandshakeStream::IsResponseBodyComplete() const {
  return parser()->IsResponseBodyComplete();
}

bool WebSocketBasicHandshakeStream::IsConnectionReused() const {
  return parser()->IsConnectionReused();
}

void WebSocketBasicHandshakeStream::SetConnectionReused() {
  parser()->SetConnectionReused();
}

bool WebSocketBasicHandshakeStream::CanReuseConnection() const {
  return false;
}

int64_t WebSocketBasicHandshakeStream::GetTotalReceivedBytes() const {
  return 0;
}

int64_t WebSocketBasicHandshakeStream::GetTotalSentBytes() const {
  return 0;
}

bool WebSocketBasicHandshakeStream::GetAlternativeService(
    AlternativeService* alternative_service) const {
  return false;
}

bool WebSocketBasicHandshakeStream::GetLoadTimingInfo(
    LoadTimingInfo* load_timing_info) const {
  return state_.connection()->GetLoadTimingInfo(IsConnectionReused(),
                                                load_timing_info);
}

void WebSocketBasicHandshakeStream::GetSSLInfo(SSLInfo* ssl_info) {
  parser()->GetSSLInfo(ssl_info);
}

void WebSocketBasicHandshakeStream::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) {
  parser()->GetSSLCertRequestInfo(cert_request_info);
}

bool WebSocketBasicHandshakeStream::GetRemoteEndpoint(IPEndPoint* endpoint) {
  if (!state_.connection() || !state_.connection()->socket())
    return false;

  return state_.connection()->socket()->GetPeerAddress(endpoint) == OK;
}

void WebSocketBasicHandshakeStream::PopulateNetErrorDetails(
    NetErrorDetails* /*details*/) {
  return;
}

Error WebSocketBasicHandshakeStream::GetTokenBindingSignature(
    crypto::ECPrivateKey* key,
    TokenBindingType tb_type,
    std::vector<uint8_t>* out) {
  NOTREACHED();
  return ERR_NOT_IMPLEMENTED;
}

void WebSocketBasicHandshakeStream::Drain(HttpNetworkSession* session) {
  HttpResponseBodyDrainer* drainer = new HttpResponseBodyDrainer(this);
  drainer->Start(session);
  // |drainer| will delete itself.
}

void WebSocketBasicHandshakeStream::SetPriority(RequestPriority priority) {
  // TODO(ricea): See TODO comment in HttpBasicStream::SetPriority(). If it is
  // gone, then copy whatever has happened there over here.
}

HttpStream* WebSocketBasicHandshakeStream::RenewStreamForAuth() {
  // Return null because we don't support renewing the stream.
  return nullptr;
}

std::unique_ptr<WebSocketStream> WebSocketBasicHandshakeStream::Upgrade() {
  // The HttpStreamParser object has a pointer to our ClientSocketHandle. Make
  // sure it does not touch it again before it is destroyed.
  state_.DeleteParser();
  WebSocketTransportClientSocketPool::UnlockEndpoint(state_.connection());
  std::unique_ptr<WebSocketStream> basic_stream(
      new WebSocketBasicStream(state_.ReleaseConnection(), state_.read_buf(),
                               sub_protocol_, extensions_));
  DCHECK(extension_params_.get());
  if (extension_params_->deflate_enabled) {
    UMA_HISTOGRAM_ENUMERATION(
        "Net.WebSocket.DeflateMode",
        extension_params_->deflate_parameters.client_context_take_over_mode(),
        WebSocketDeflater::NUM_CONTEXT_TAKEOVER_MODE_TYPES);

    return std::unique_ptr<WebSocketStream>(new WebSocketDeflateStream(
        std::move(basic_stream), extension_params_->deflate_parameters,
        std::unique_ptr<WebSocketDeflatePredictor>(
            new WebSocketDeflatePredictorImpl)));
  } else {
    return basic_stream;
  }
}

void WebSocketBasicHandshakeStream::SetWebSocketKeyForTesting(
    const std::string& key) {
  handshake_challenge_for_testing_.reset(new std::string(key));
}

void WebSocketBasicHandshakeStream::ReadResponseHeadersCallback(
    const CompletionCallback& callback,
    int result) {
  callback.Run(ValidateResponse(result));
}

void WebSocketBasicHandshakeStream::OnFinishOpeningHandshake() {
  DCHECK(http_response_info_);
  WebSocketDispatchOnFinishOpeningHandshake(connect_delegate_,
                                            url_,
                                            http_response_info_->headers,
                                            http_response_info_->response_time);
}

int WebSocketBasicHandshakeStream::ValidateResponse(int rv) {
  DCHECK(http_response_info_);
  // Most net errors happen during connection, so they are not seen by this
  // method. The histogram for error codes is created in
  // Delegate::OnResponseStarted in websocket_stream.cc instead.
  if (rv >= 0) {
    const HttpResponseHeaders* headers = http_response_info_->headers.get();
    const int response_code = headers->response_code();
    UMA_HISTOGRAM_SPARSE_SLOWLY("Net.WebSocket.ResponseCode", response_code);
    switch (response_code) {
      case HTTP_SWITCHING_PROTOCOLS:
        OnFinishOpeningHandshake();
        return ValidateUpgradeResponse(headers);

      // We need to pass these through for authentication to work.
      case HTTP_UNAUTHORIZED:
      case HTTP_PROXY_AUTHENTICATION_REQUIRED:
        return OK;

      // Other status codes are potentially risky (see the warnings in the
      // WHATWG WebSocket API spec) and so are dropped by default.
      default:
        // A WebSocket server cannot be using HTTP/0.9, so if we see version
        // 0.9, it means the response was garbage.
        // Reporting "Unexpected response code: 200" in this case is not
        // helpful, so use a different error message.
        if (headers->GetHttpVersion() == HttpVersion(0, 9)) {
          OnFailure("Error during WebSocket handshake: Invalid status line");
        } else {
          OnFailure(base::StringPrintf(
              "Error during WebSocket handshake: Unexpected response code: %d",
              headers->response_code()));
        }
        OnFinishOpeningHandshake();
        return ERR_INVALID_RESPONSE;
    }
  } else {
    if (rv == ERR_EMPTY_RESPONSE) {
      OnFailure("Connection closed before receiving a handshake response");
      return rv;
    }
    OnFailure(std::string("Error during WebSocket handshake: ") +
              ErrorToString(rv));
    OnFinishOpeningHandshake();
    // Some error codes (for example ERR_CONNECTION_CLOSED) get changed to OK at
    // higher levels. To prevent an unvalidated connection getting erroneously
    // upgraded, don't pass through the status code unchanged if it is
    // HTTP_SWITCHING_PROTOCOLS.
    if (http_response_info_->headers &&
        http_response_info_->headers->response_code() ==
            HTTP_SWITCHING_PROTOCOLS) {
      http_response_info_->headers->ReplaceStatusLine(
          kConnectionErrorStatusLine);
    }
    return rv;
  }
}

int WebSocketBasicHandshakeStream::ValidateUpgradeResponse(
    const HttpResponseHeaders* headers) {
  extension_params_.reset(new WebSocketExtensionParams);
  std::string failure_message;
  if (ValidateUpgrade(headers, &failure_message) &&
      ValidateSecWebSocketAccept(
          headers, handshake_challenge_response_, &failure_message) &&
      ValidateConnection(headers, &failure_message) &&
      ValidateSubProtocol(headers,
                          requested_sub_protocols_,
                          &sub_protocol_,
                          &failure_message) &&
      ValidateExtensions(headers,
                         &extensions_,
                         &failure_message,
                         extension_params_.get())) {
    return OK;
  }
  OnFailure("Error during WebSocket handshake: " + failure_message);
  return ERR_INVALID_RESPONSE;
}

void WebSocketBasicHandshakeStream::OnFailure(const std::string& message) {
  stream_request_->OnFailure(message);
}

}  // namespace net
