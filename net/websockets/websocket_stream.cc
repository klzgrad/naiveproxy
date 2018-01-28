// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_stream.h"

#include <utility>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/websockets/websocket_errors.h"
#include "net/websockets/websocket_event_interface.h"
#include "net/websockets/websocket_handshake_constants.h"
#include "net/websockets/websocket_handshake_stream_base.h"
#include "net/websockets/websocket_handshake_stream_create_helper.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("websocket_stream", R"(
        semantics {
          sender: "WebSocket Handshake"
          description:
            "Renderer process initiated WebSocket handshake. The WebSocket "
            "handshake is used to establish a connection between a web page "
            "and a consenting server for bi-directional communication."
          trigger:
            "A handshake is performed every time a new connection is "
            "established via the Javascript or PPAPI WebSocket API. Any web "
            "page or extension can create a WebSocket connection."
          data: "The path and sub-protocols requested when the WebSocket was "
                "created, plus the origin of the creating page."
          destination: OTHER
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user or per-app cookie store"
          setting: "These requests cannot be disabled."
          policy_exception_justification:
            "Not implemented. WebSocket is a core web platform API."
        })");

}  // namespace

namespace net {
namespace {

// The timeout duration of WebSocket handshake.
// It is defined as the same value as the TCP connection timeout value in
// net/socket/websocket_transport_client_socket_pool.cc to make it hard for
// JavaScript programs to recognize the timeout cause.
const int kHandshakeTimeoutIntervalInSeconds = 240;

class WebSocketStreamRequestImpl;

class Delegate : public URLRequest::Delegate {
 public:
  enum HandshakeResult {
    INCOMPLETE,
    CONNECTED,
    FAILED,
    NUM_HANDSHAKE_RESULT_TYPES,
  };

  explicit Delegate(WebSocketStreamRequestImpl* owner)
      : owner_(owner), result_(INCOMPLETE) {}
  ~Delegate() override {
    UMA_HISTOGRAM_ENUMERATION(
        "Net.WebSocket.HandshakeResult", result_, NUM_HANDSHAKE_RESULT_TYPES);
  }

  // Implementation of URLRequest::Delegate methods.
  void OnReceivedRedirect(URLRequest* request,
                          const RedirectInfo& redirect_info,
                          bool* defer_redirect) override;

  void OnResponseStarted(URLRequest* request, int net_error) override;

  void OnAuthRequired(URLRequest* request,
                      AuthChallengeInfo* auth_info) override;

  void OnCertificateRequested(URLRequest* request,
                              SSLCertRequestInfo* cert_request_info) override;

  void OnSSLCertificateError(URLRequest* request,
                             const SSLInfo& ssl_info,
                             bool fatal) override;

  void OnReadCompleted(URLRequest* request, int bytes_read) override;

 private:
  WebSocketStreamRequestImpl* owner_;
  HandshakeResult result_;
};

class WebSocketStreamRequestImpl : public WebSocketStreamRequest {
 public:
  WebSocketStreamRequestImpl(
      const GURL& url,
      const URLRequestContext* context,
      const url::Origin& origin,
      const GURL& site_for_cookies,
      const std::string& additional_headers,
      std::unique_ptr<WebSocketStream::ConnectDelegate> connect_delegate,
      std::unique_ptr<WebSocketHandshakeStreamCreateHelper> create_helper)
      : delegate_(new Delegate(this)),
        url_request_(context->CreateRequest(url,
                                            DEFAULT_PRIORITY,
                                            delegate_.get(),
                                            kTrafficAnnotation)),
        connect_delegate_(std::move(connect_delegate)),
        handshake_stream_(nullptr) {
    create_helper->set_stream_request(this);
    HttpRequestHeaders headers;
    headers.SetHeader(websockets::kUpgrade, websockets::kWebSocketLowercase);
    headers.SetHeader(HttpRequestHeaders::kConnection, websockets::kUpgrade);
    headers.SetHeader(HttpRequestHeaders::kOrigin, origin.Serialize());
    headers.SetHeader(websockets::kSecWebSocketVersion,
                      websockets::kSupportedVersion);

    headers.AddHeadersFromString(additional_headers);

    url_request_->SetExtraRequestHeaders(headers);
    url_request_->set_initiator(origin);
    url_request_->set_site_for_cookies(site_for_cookies);

    url_request_->SetUserData(
        WebSocketHandshakeStreamBase::CreateHelper::DataKey(),
        std::move(create_helper));
    url_request_->SetLoadFlags(LOAD_DISABLE_CACHE | LOAD_BYPASS_CACHE);
    connect_delegate_->OnCreateRequest(url_request_.get());
  }

  // Destroying this object destroys the URLRequest, which cancels the request
  // and so terminates the handshake if it is incomplete.
  ~WebSocketStreamRequestImpl() override {}

  void OnHandshakeStreamCreated(
      WebSocketHandshakeStreamBase* handshake_stream) override {
    handshake_stream_ = handshake_stream;
  }

  void OnFailure(const std::string& message) override {
    failure_message_ = message;
  }

  void Start(std::unique_ptr<base::Timer> timer) {
    DCHECK(timer);
    base::TimeDelta timeout(base::TimeDelta::FromSeconds(
        kHandshakeTimeoutIntervalInSeconds));
    timer_ = std::move(timer);
    timer_->Start(FROM_HERE, timeout,
                  base::Bind(&WebSocketStreamRequestImpl::OnTimeout,
                             base::Unretained(this)));
    url_request_->Start();
  }

  void PerformUpgrade() {
    DCHECK(timer_);
    DCHECK(handshake_stream_);

    timer_->Stop();

    std::unique_ptr<URLRequest> url_request = std::move(url_request_);
    WebSocketHandshakeStreamBase* handshake_stream = handshake_stream_;
    handshake_stream_ = nullptr;
    connect_delegate_->OnSuccess(handshake_stream->Upgrade());

    // This is safe even if |this| has already been deleted.
    url_request->CancelWithError(ERR_WS_UPGRADE);
  }

  std::string FailureMessageFromNetError(int net_error) {
    if (net_error == ERR_TUNNEL_CONNECTION_FAILED) {
      // This error is common and confusing, so special-case it.
      // TODO(ricea): Include the HostPortPair of the selected proxy server in
      // the error message. This is not currently possible because it isn't set
      // in HttpResponseInfo when a ERR_TUNNEL_CONNECTION_FAILED error happens.
      return "Establishing a tunnel via proxy server failed.";
    } else {
      return std::string("Error in connection establishment: ") +
             ErrorToString(net_error);
    }
  }

  void ReportFailure(int net_error) {
    DCHECK(timer_);
    timer_->Stop();
    if (failure_message_.empty()) {
      switch (net_error) {
        case OK:
        case ERR_IO_PENDING:
          break;
        case ERR_ABORTED:
          failure_message_ = "WebSocket opening handshake was canceled";
          break;
        case ERR_TIMED_OUT:
          failure_message_ = "WebSocket opening handshake timed out";
          break;
        default:
          failure_message_ = FailureMessageFromNetError(net_error);
          break;
      }
    }
    ReportFailureWithMessage(failure_message_);
  }

  void ReportFailureWithMessage(const std::string& failure_message) {
    connect_delegate_->OnFailure(failure_message);
  }

  void OnFinishOpeningHandshake() {
    WebSocketDispatchOnFinishOpeningHandshake(connect_delegate(),
                                              url_request_->url(),
                                              url_request_->response_headers(),
                                              url_request_->response_time());
  }

  WebSocketStream::ConnectDelegate* connect_delegate() const {
    return connect_delegate_.get();
  }

  void OnTimeout() {
    url_request_->CancelWithError(ERR_TIMED_OUT);
  }

 private:
  // |delegate_| needs to be declared before |url_request_| so that it gets
  // initialised first.
  std::unique_ptr<Delegate> delegate_;

  // Deleting the WebSocketStreamRequestImpl object deletes this URLRequest
  // object, cancelling the whole connection.
  std::unique_ptr<URLRequest> url_request_;

  std::unique_ptr<WebSocketStream::ConnectDelegate> connect_delegate_;

  // This is owned by the caller of
  // WebsocketHandshakeStreamCreateHelper::CreateBaseStream().  Both the stream
  // and this object will be destroyed during the destruction of the URLRequest
  // object associated with the handshake. This is only guaranteed to be a valid
  // pointer if the handshake succeeded.
  WebSocketHandshakeStreamBase* handshake_stream_;

  // The failure message supplied by WebSocketBasicHandshakeStream, if any.
  std::string failure_message_;

  // A timer for handshake timeout.
  std::unique_ptr<base::Timer> timer_;
};

class SSLErrorCallbacks : public WebSocketEventInterface::SSLErrorCallbacks {
 public:
  explicit SSLErrorCallbacks(URLRequest* url_request)
      : url_request_(url_request) {}

  void CancelSSLRequest(int error, const SSLInfo* ssl_info) override {
    if (ssl_info) {
      url_request_->CancelWithSSLError(error, *ssl_info);
    } else {
      url_request_->CancelWithError(error);
    }
  }

  void ContinueSSLRequest() override {
    url_request_->ContinueDespiteLastError();
  }

 private:
  URLRequest* url_request_;
};

void Delegate::OnReceivedRedirect(URLRequest* request,
                                  const RedirectInfo& redirect_info,
                                  bool* defer_redirect) {
  // This code should never be reached for externally generated redirects,
  // as WebSocketBasicHandshakeStream is responsible for filtering out
  // all response codes besides 101, 401, and 407. As such, the URLRequest
  // should never see a redirect sent over the network. However, internal
  // redirects also result in this method being called, such as those
  // caused by HSTS.
  // Because it's security critical to prevent externally-generated
  // redirects in WebSockets, perform additional checks to ensure this
  // is only internal.
  GURL::Replacements replacements;
  replacements.SetSchemeStr("wss");
  GURL expected_url = request->original_url().ReplaceComponents(replacements);
  if (redirect_info.new_method != "GET" ||
      redirect_info.new_url != expected_url) {
    // This should not happen.
    DLOG(FATAL) << "Unauthorized WebSocket redirect to "
                << redirect_info.new_method << " "
                << redirect_info.new_url.spec();
    request->Cancel();
  }
}

void Delegate::OnResponseStarted(URLRequest* request, int net_error) {
  DCHECK_NE(ERR_IO_PENDING, net_error);
  // All error codes, including OK and ABORTED, as with
  // Net.ErrorCodesForMainFrame3
  UMA_HISTOGRAM_SPARSE_SLOWLY("Net.WebSocket.ErrorCodes", -net_error);
  if (net::IsLocalhost(request->url().HostNoBrackets())) {
    UMA_HISTOGRAM_SPARSE_SLOWLY("Net.WebSocket.ErrorCodes_Localhost",
                                -net_error);
  } else {
    UMA_HISTOGRAM_SPARSE_SLOWLY("Net.WebSocket.ErrorCodes_NotLocalhost",
                                -net_error);
  }

  if (net_error != OK) {
    DVLOG(3) << "OnResponseStarted (request failed)";
    owner_->ReportFailure(net_error);
    return;
  }
  const int response_code = request->GetResponseCode();
  DVLOG(3) << "OnResponseStarted (response code " << response_code << ")";
  switch (response_code) {
    case HTTP_SWITCHING_PROTOCOLS:
      result_ = CONNECTED;
      owner_->PerformUpgrade();
      return;

    case HTTP_UNAUTHORIZED:
      result_ = FAILED;
      owner_->OnFinishOpeningHandshake();
      owner_->ReportFailureWithMessage(
          "HTTP Authentication failed; no valid credentials available");
      return;

    case HTTP_PROXY_AUTHENTICATION_REQUIRED:
      result_ = FAILED;
      owner_->OnFinishOpeningHandshake();
      owner_->ReportFailureWithMessage("Proxy authentication failed");
      return;

    default:
      result_ = FAILED;
      owner_->ReportFailure(net_error);
  }
}

void Delegate::OnAuthRequired(URLRequest* request,
                              AuthChallengeInfo* auth_info) {
  // This should only be called if credentials are not already stored.
  request->CancelAuth();
}

void Delegate::OnCertificateRequested(URLRequest* request,
                                      SSLCertRequestInfo* cert_request_info) {
  // This method is called when a client certificate is requested, and the
  // request context does not already contain a client certificate selection for
  // the endpoint. In this case, a main frame resource request would pop-up UI
  // to permit selection of a client certificate, but since WebSockets are
  // sub-resources they should not pop-up UI and so there is nothing more we can
  // do.
  request->Cancel();
}

void Delegate::OnSSLCertificateError(URLRequest* request,
                                     const SSLInfo& ssl_info,
                                     bool fatal) {
  owner_->connect_delegate()->OnSSLCertificateError(
      std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks>(
          new SSLErrorCallbacks(request)),
      ssl_info, fatal);
}

void Delegate::OnReadCompleted(URLRequest* request, int bytes_read) {
  NOTREACHED();
}

}  // namespace

WebSocketStreamRequest::~WebSocketStreamRequest() {}

WebSocketStream::WebSocketStream() {}
WebSocketStream::~WebSocketStream() {}

WebSocketStream::ConnectDelegate::~ConnectDelegate() {}

std::unique_ptr<WebSocketStreamRequest> WebSocketStream::CreateAndConnectStream(
    const GURL& socket_url,
    std::unique_ptr<WebSocketHandshakeStreamCreateHelper> create_helper,
    const url::Origin& origin,
    const GURL& site_for_cookies,
    const std::string& additional_headers,
    URLRequestContext* url_request_context,
    const NetLogWithSource& net_log,
    std::unique_ptr<ConnectDelegate> connect_delegate) {
  std::unique_ptr<WebSocketStreamRequestImpl> request(
      new WebSocketStreamRequestImpl(socket_url, url_request_context, origin,
                                     site_for_cookies, additional_headers,
                                     std::move(connect_delegate),
                                     std::move(create_helper)));
  request->Start(std::unique_ptr<base::Timer>(new base::Timer(false, false)));
  return std::move(request);
}

std::unique_ptr<WebSocketStreamRequest>
WebSocketStream::CreateAndConnectStreamForTesting(
    const GURL& socket_url,
    std::unique_ptr<WebSocketHandshakeStreamCreateHelper> create_helper,
    const url::Origin& origin,
    const GURL& site_for_cookies,
    const std::string& additional_headers,
    URLRequestContext* url_request_context,
    const NetLogWithSource& net_log,
    std::unique_ptr<WebSocketStream::ConnectDelegate> connect_delegate,
    std::unique_ptr<base::Timer> timer) {
  std::unique_ptr<WebSocketStreamRequestImpl> request(
      new WebSocketStreamRequestImpl(socket_url, url_request_context, origin,
                                     site_for_cookies, additional_headers,
                                     std::move(connect_delegate),
                                     std::move(create_helper)));
  request->Start(std::move(timer));
  return std::move(request);
}

void WebSocketDispatchOnFinishOpeningHandshake(
    WebSocketStream::ConnectDelegate* connect_delegate,
    const GURL& url,
    const scoped_refptr<HttpResponseHeaders>& headers,
    base::Time response_time) {
  DCHECK(connect_delegate);
  if (headers.get()) {
    connect_delegate->OnFinishOpeningHandshake(
        std::make_unique<WebSocketHandshakeResponseInfo>(
            url, headers->response_code(), headers->GetStatusText(), headers,
            response_time));
  }
}

}  // namespace net
