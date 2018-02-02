// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_stream_create_test_base.h"

#include <utility>

#include "base/callback.h"
#include "base/macros.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_with_source.h"
#include "net/websockets/websocket_basic_handshake_stream.h"
#include "net/websockets/websocket_handshake_request_info.h"
#include "net/websockets/websocket_handshake_response_info.h"
#include "net/websockets/websocket_handshake_stream_create_helper.h"
#include "net/websockets/websocket_stream.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

using HeaderKeyValuePair = WebSocketStreamCreateTestBase::HeaderKeyValuePair;

// A sub-class of WebSocketHandshakeStreamCreateHelper which always sets a
// deterministic key to use in the WebSocket handshake.
class DeterministicKeyWebSocketHandshakeStreamCreateHelper
    : public WebSocketHandshakeStreamCreateHelper {
 public:
  DeterministicKeyWebSocketHandshakeStreamCreateHelper(
      WebSocketStream::ConnectDelegate* connect_delegate,
      const std::vector<std::string>& requested_subprotocols)
      : WebSocketHandshakeStreamCreateHelper(connect_delegate,
                                             requested_subprotocols) {}

  void OnBasicStreamCreated(WebSocketBasicHandshakeStream* stream) override {
    stream->SetWebSocketKeyForTesting("dGhlIHNhbXBsZSBub25jZQ==");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(
      DeterministicKeyWebSocketHandshakeStreamCreateHelper);
};

class WebSocketStreamCreateTestBase::TestConnectDelegate
    : public WebSocketStream::ConnectDelegate {
 public:
  TestConnectDelegate(WebSocketStreamCreateTestBase* owner,
                      const base::Closure& done_callback)
      : owner_(owner), done_callback_(done_callback) {}

  void OnCreateRequest(URLRequest* request) override {
    owner_->url_request_ = request;
  }

  void OnSuccess(std::unique_ptr<WebSocketStream> stream) override {
    stream.swap(owner_->stream_);
    done_callback_.Run();
  }

  void OnFailure(const std::string& message) override {
    owner_->has_failed_ = true;
    owner_->failure_message_ = message;
    done_callback_.Run();
  }

  void OnStartOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeRequestInfo> request) override {
    // Can be called multiple times (in the case of HTTP auth). Last call
    // wins.
    owner_->request_info_ = std::move(request);
  }

  void OnFinishOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeResponseInfo> response) override {
    if (owner_->response_info_)
      ADD_FAILURE();
    owner_->response_info_ = std::move(response);
  }

  void OnSSLCertificateError(
      std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks>
          ssl_error_callbacks,
      const SSLInfo& ssl_info,
      bool fatal) override {
    owner_->ssl_error_callbacks_ = std::move(ssl_error_callbacks);
    owner_->ssl_info_ = ssl_info;
    owner_->ssl_fatal_ = fatal;
  }

 private:
  WebSocketStreamCreateTestBase* owner_;
  base::Closure done_callback_;
  DISALLOW_COPY_AND_ASSIGN(TestConnectDelegate);
};

WebSocketStreamCreateTestBase::WebSocketStreamCreateTestBase()
    : has_failed_(false), ssl_fatal_(false), url_request_(nullptr) {}

WebSocketStreamCreateTestBase::~WebSocketStreamCreateTestBase() = default;

void WebSocketStreamCreateTestBase::CreateAndConnectStream(
    const GURL& socket_url,
    const std::vector<std::string>& sub_protocols,
    const url::Origin& origin,
    const GURL& site_for_cookies,
    const std::string& additional_headers,
    std::unique_ptr<base::Timer> timer) {
  for (size_t i = 0; i < ssl_data_.size(); ++i) {
    url_request_context_host_.AddSSLSocketDataProvider(std::move(ssl_data_[i]));
  }
  ssl_data_.clear();
  std::unique_ptr<WebSocketStream::ConnectDelegate> connect_delegate(
      new TestConnectDelegate(this, connect_run_loop_.QuitClosure()));
  WebSocketStream::ConnectDelegate* delegate = connect_delegate.get();
  std::unique_ptr<WebSocketHandshakeStreamCreateHelper> create_helper(
      new DeterministicKeyWebSocketHandshakeStreamCreateHelper(delegate,
                                                               sub_protocols));
  stream_request_ = WebSocketStream::CreateAndConnectStreamForTesting(
      socket_url, std::move(create_helper), origin, site_for_cookies,
      additional_headers, url_request_context_host_.GetURLRequestContext(),
      NetLogWithSource(), std::move(connect_delegate),
      timer ? std::move(timer)
            : std::unique_ptr<base::Timer>(new base::Timer(false, false)));
}

std::vector<HeaderKeyValuePair>
WebSocketStreamCreateTestBase::RequestHeadersToVector(
    const HttpRequestHeaders& headers) {
  HttpRequestHeaders::Iterator it(headers);
  std::vector<HeaderKeyValuePair> result;
  while (it.GetNext())
    result.push_back(HeaderKeyValuePair(it.name(), it.value()));
  return result;
}

std::vector<HeaderKeyValuePair>
WebSocketStreamCreateTestBase::ResponseHeadersToVector(
    const HttpResponseHeaders& headers) {
  size_t iter = 0;
  std::string name, value;
  std::vector<HeaderKeyValuePair> result;
  while (headers.EnumerateHeaderLines(&iter, &name, &value))
    result.push_back(HeaderKeyValuePair(name, value));
  return result;
}

void WebSocketStreamCreateTestBase::WaitUntilConnectDone() {
  connect_run_loop_.Run();
}

std::vector<std::string> WebSocketStreamCreateTestBase::NoSubProtocols() {
  return std::vector<std::string>();
}

}  // namespace net
