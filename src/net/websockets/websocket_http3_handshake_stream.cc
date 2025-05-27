// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_http3_handshake_stream.h"

#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "net/base/ip_endpoint.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_status_code.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/websockets/websocket_basic_stream.h"
#include "net/websockets/websocket_deflate_predictor_impl.h"
#include "net/websockets/websocket_deflate_stream.h"
#include "net/websockets/websocket_handshake_constants.h"
#include "net/websockets/websocket_handshake_request_info.h"

namespace net {
struct AlternativeService;

namespace {

bool ValidateStatus(const HttpResponseHeaders* headers) {
  return headers->GetStatusLine() == "HTTP/1.1 200";
}

}  // namespace

WebSocketHttp3HandshakeStream::WebSocketHttp3HandshakeStream(
    std::unique_ptr<QuicChromiumClientSession::Handle> session,
    WebSocketStream::ConnectDelegate* connect_delegate,
    std::vector<std::string> requested_sub_protocols,
    std::vector<std::string> requested_extensions,
    WebSocketStreamRequestAPI* request,
    std::set<std::string> dns_aliases)
    : session_(std::move(session)),
      connect_delegate_(connect_delegate),
      requested_sub_protocols_(std::move(requested_sub_protocols)),
      requested_extensions_(std::move(requested_extensions)),
      stream_request_(request),
      dns_aliases_(std::move(dns_aliases)) {
  DCHECK(connect_delegate);
  DCHECK(request);
}

WebSocketHttp3HandshakeStream::~WebSocketHttp3HandshakeStream() {
  RecordHandshakeResult(result_);
}

void WebSocketHttp3HandshakeStream::RegisterRequest(
    const HttpRequestInfo* request_info) {
  DCHECK(request_info);
  DCHECK(request_info->traffic_annotation.is_valid());
  request_info_ = request_info;
}

int WebSocketHttp3HandshakeStream::InitializeStream(
    bool can_send_early,
    RequestPriority priority,
    const NetLogWithSource& net_log,
    CompletionOnceCallback callback) {
  priority_ = priority;
  net_log_ = net_log;
  request_time_ = base::Time::Now();

  int ret = OK;
  if (!can_send_early) {
    ret = session_->WaitForHandshakeConfirmation(
        base::BindOnce(&WebSocketHttp3HandshakeStream::OnHandshakeConfirmed,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }
  return ret;
}

void WebSocketHttp3HandshakeStream::OnHandshakeConfirmed(
    CompletionOnceCallback callback,
    int rv) {
  std::move(callback).Run(rv);
}

int WebSocketHttp3HandshakeStream::SendRequest(
    const HttpRequestHeaders& request_headers,
    HttpResponseInfo* response,
    CompletionOnceCallback callback) {
  DCHECK(!request_headers.HasHeader(websockets::kSecWebSocketKey));
  DCHECK(!request_headers.HasHeader(websockets::kSecWebSocketProtocol));
  DCHECK(!request_headers.HasHeader(websockets::kSecWebSocketExtensions));
  DCHECK(request_headers.HasHeader(HttpRequestHeaders::kOrigin));
  DCHECK(request_headers.HasHeader(websockets::kUpgrade));
  DCHECK(request_headers.HasHeader(HttpRequestHeaders::kConnection));
  DCHECK(request_headers.HasHeader(websockets::kSecWebSocketVersion));

  if (!session_) {
    constexpr int rv = ERR_CONNECTION_CLOSED;
    OnFailure("Connection closed before sending request.", rv, std::nullopt);
    return rv;
  }

  http_response_info_ = response;

  IPEndPoint address;
  int result = session_->GetPeerAddress(&address);
  if (result != OK) {
    OnFailure("Error getting IP address.", result, std::nullopt);
    return result;
  }
  http_response_info_->remote_endpoint = address;

  auto request = std::make_unique<WebSocketHandshakeRequestInfo>(
      request_info_->url, base::Time::Now());
  request->headers = request_headers;

  AddVectorHeaders(requested_extensions_, requested_sub_protocols_,
                   &request->headers);

  CreateSpdyHeadersFromHttpRequestForWebSocket(
      request_info_->url, request->headers, &http3_request_headers_);

  connect_delegate_->OnStartOpeningHandshake(std::move(request));

  callback_ = std::move(callback);

  std::unique_ptr<WebSocketQuicStreamAdapter> stream_adapter =
      session_->CreateWebSocketQuicStreamAdapter(
          this,
          base::BindOnce(
              &WebSocketHttp3HandshakeStream::ReceiveAdapterAndStartRequest,
              base::Unretained(this)),
          NetworkTrafficAnnotationTag(request_info_->traffic_annotation));
  if (!stream_adapter) {
    return ERR_IO_PENDING;
  }
  ReceiveAdapterAndStartRequest(std::move(stream_adapter));
  return OK;
}

int WebSocketHttp3HandshakeStream::ReadResponseHeaders(
    CompletionOnceCallback callback) {
  if (stream_closed_) {
    return stream_error_;
  }

  if (response_headers_complete_) {
    return ValidateResponse();
  }

  callback_ = std::move(callback);
  return ERR_IO_PENDING;
}

// TODO(momoka): Implement this.
int WebSocketHttp3HandshakeStream::ReadResponseBody(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback) {
  return OK;
}

void WebSocketHttp3HandshakeStream::Close(bool not_reusable) {
  if (stream_adapter_) {
    stream_adapter_->Disconnect();
    stream_closed_ = true;
    stream_error_ = ERR_CONNECTION_CLOSED;
  }
}

// TODO(momoka): Implement this.
bool WebSocketHttp3HandshakeStream::IsResponseBodyComplete() const {
  return false;
}

// TODO(momoka): Implement this.
bool WebSocketHttp3HandshakeStream::IsConnectionReused() const {
  return true;
}

// TODO(momoka): Implement this.
void WebSocketHttp3HandshakeStream::SetConnectionReused() {}

// TODO(momoka): Implement this.
bool WebSocketHttp3HandshakeStream::CanReuseConnection() const {
  return false;
}

// TODO(momoka): Implement this.
int64_t WebSocketHttp3HandshakeStream::GetTotalReceivedBytes() const {
  return 0;
}

// TODO(momoka): Implement this.
int64_t WebSocketHttp3HandshakeStream::GetTotalSentBytes() const {
  return 0;
}

// TODO(momoka): Implement this.
bool WebSocketHttp3HandshakeStream::GetAlternativeService(
    AlternativeService* alternative_service) const {
  return false;
}

// TODO(momoka): Implement this.
bool WebSocketHttp3HandshakeStream::GetLoadTimingInfo(
    LoadTimingInfo* load_timing_info) const {
  return false;
}

// TODO(momoka): Implement this.
void WebSocketHttp3HandshakeStream::GetSSLInfo(SSLInfo* ssl_info) {}

// TODO(momoka): Implement this.
int WebSocketHttp3HandshakeStream::GetRemoteEndpoint(IPEndPoint* endpoint) {
  return 0;
}

// TODO(momoka): Implement this.
void WebSocketHttp3HandshakeStream::Drain(HttpNetworkSession* session) {}

// TODO(momoka): Implement this.
void WebSocketHttp3HandshakeStream::SetPriority(RequestPriority priority) {}

// TODO(momoka): Implement this.
void WebSocketHttp3HandshakeStream::PopulateNetErrorDetails(
    NetErrorDetails* details) {}

// TODO(momoka): Implement this.
std::unique_ptr<HttpStream>
WebSocketHttp3HandshakeStream::RenewStreamForAuth() {
  return nullptr;
}

// TODO(momoka): Implement this.
const std::set<std::string>& WebSocketHttp3HandshakeStream::GetDnsAliases()
    const {
  return dns_aliases_;
}

// TODO(momoka): Implement this.
std::string_view WebSocketHttp3HandshakeStream::GetAcceptChViaAlps() const {
  return {};
}

// WebSocketHandshakeStreamBase methods.

// TODO(momoka): Implement this.
std::unique_ptr<WebSocketStream> WebSocketHttp3HandshakeStream::Upgrade() {
  DCHECK(extension_params_.get());

  stream_adapter_->clear_delegate();
  std::unique_ptr<WebSocketStream> basic_stream =
      std::make_unique<WebSocketBasicStream>(std::move(stream_adapter_),
                                             nullptr, sub_protocol_,
                                             extensions_, net_log_);

  if (!extension_params_->deflate_enabled) {
    return basic_stream;
  }

  return std::make_unique<WebSocketDeflateStream>(
      std::move(basic_stream), extension_params_->deflate_parameters,
      std::make_unique<WebSocketDeflatePredictorImpl>());
}

bool WebSocketHttp3HandshakeStream::CanReadFromStream() const {
  return stream_adapter_ && stream_adapter_->is_initialized();
}

base::WeakPtr<WebSocketHandshakeStreamBase>
WebSocketHttp3HandshakeStream::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void WebSocketHttp3HandshakeStream::OnHeadersSent() {
  std::move(callback_).Run(OK);
}

void WebSocketHttp3HandshakeStream::OnHeadersReceived(
    const quiche::HttpHeaderBlock& response_headers) {
  DCHECK(!response_headers_complete_);
  DCHECK(http_response_info_);

  response_headers_complete_ = true;

  const int rv =
      SpdyHeadersToHttpResponse(response_headers, http_response_info_);
  DCHECK_NE(rv, ERR_INCOMPLETE_HTTP2_HEADERS);

  // Do not store SSLInfo in the response here, HttpNetworkTransaction will take
  // care of that part.
  http_response_info_->was_alpn_negotiated = true;
  http_response_info_->response_time =
      http_response_info_->original_response_time = base::Time::Now();
  http_response_info_->request_time = request_time_;
  http_response_info_->connection_info = HttpConnectionInfo::kHTTP2;
  http_response_info_->alpn_negotiated_protocol =
      HttpConnectionInfoToString(http_response_info_->connection_info);

  if (callback_) {
    std::move(callback_).Run(ValidateResponse());
  }
}

void WebSocketHttp3HandshakeStream::OnClose(int status) {
  DCHECK(stream_adapter_);
  DCHECK_GT(ERR_IO_PENDING, status);

  stream_closed_ = true;
  stream_error_ = status;

  stream_adapter_.reset();

  // If response headers have already been received,
  // then ValidateResponse() sets `result_`.
  if (!response_headers_complete_) {
    result_ = HandshakeResult::HTTP3_FAILED;
  }

  OnFailure(base::StrCat({"Stream closed with error: ", ErrorToString(status)}),
            status, std::nullopt);

  if (callback_) {
    std::move(callback_).Run(status);
  }
}

void WebSocketHttp3HandshakeStream::ReceiveAdapterAndStartRequest(
    std::unique_ptr<WebSocketQuicStreamAdapter> adapter) {
  stream_adapter_ = std::move(adapter);
  // WriteHeaders returns synchronously.
  stream_adapter_->WriteHeaders(std::move(http3_request_headers_), false);
}

int WebSocketHttp3HandshakeStream::ValidateResponse() {
  DCHECK(http_response_info_);
  const HttpResponseHeaders* headers = http_response_info_->headers.get();
  const int response_code = headers->response_code();
  switch (response_code) {
    case HTTP_OK:
      return ValidateUpgradeResponse(headers);

    // We need to pass these through for authentication to work.
    case HTTP_UNAUTHORIZED:
    case HTTP_PROXY_AUTHENTICATION_REQUIRED:
      return OK;

    // Other status codes are potentially risky (see the warnings in the
    // WHATWG WebSocket API spec) and so are dropped by default.
    default:
      OnFailure(
          base::StringPrintf(
              "Error during WebSocket handshake: Unexpected response code: %d",
              headers->response_code()),
          ERR_FAILED, headers->response_code());
      result_ = HandshakeResult::HTTP3_INVALID_STATUS;
      return ERR_INVALID_RESPONSE;
  }
}

int WebSocketHttp3HandshakeStream::ValidateUpgradeResponse(
    const HttpResponseHeaders* headers) {
  extension_params_ = std::make_unique<WebSocketExtensionParams>();
  std::string failure_message;
  if (!ValidateStatus(headers)) {
    result_ = HandshakeResult::HTTP3_INVALID_STATUS;
  } else if (!ValidateSubProtocol(headers, requested_sub_protocols_,
                                  &sub_protocol_, &failure_message)) {
    result_ = HandshakeResult::HTTP3_FAILED_SUBPROTO;
  } else if (!ValidateExtensions(headers, &extensions_, &failure_message,
                                 extension_params_.get())) {
    result_ = HandshakeResult::HTTP3_FAILED_EXTENSIONS;
  } else {
    result_ = HandshakeResult::HTTP3_CONNECTED;
    return OK;
  }

  const int rv = ERR_INVALID_RESPONSE;
  OnFailure("Error during WebSocket handshake: " + failure_message, rv,
            std::nullopt);
  return rv;
}

// TODO(momoka): Implement this.
void WebSocketHttp3HandshakeStream::OnFailure(
    const std::string& message,
    int net_error,
    std::optional<int> response_code) {
  stream_request_->OnFailure(message, net_error, response_code);
}

}  // namespace net
