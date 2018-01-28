// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_proxy_client_socket.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/auth.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/proxy_delegate.h"
#include "net/http/http_basic_stream.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_stream_parser.h"
#include "net/http/proxy_connect_redirect_http_stream.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"
#include "net/socket/client_socket_handle.h"
#include "url/gurl.h"

namespace net {

HttpProxyClientSocket::HttpProxyClientSocket(
    ClientSocketHandle* transport_socket,
    const std::string& user_agent,
    const HostPortPair& endpoint,
    const HostPortPair& proxy_server,
    HttpAuthController* http_auth_controller,
    bool tunnel,
    bool using_spdy,
    NextProto negotiated_protocol,
    ProxyDelegate* proxy_delegate,
    bool is_https_proxy)
    : io_callback_(base::Bind(&HttpProxyClientSocket::OnIOComplete,
                              base::Unretained(this))),
      next_state_(STATE_NONE),
      transport_(transport_socket),
      endpoint_(endpoint),
      auth_(http_auth_controller),
      tunnel_(tunnel),
      using_spdy_(using_spdy),
      negotiated_protocol_(negotiated_protocol),
      is_https_proxy_(is_https_proxy),
      redirect_has_load_timing_info_(false),
      proxy_server_(proxy_server),
      proxy_delegate_(proxy_delegate),
      net_log_(transport_socket->socket()->NetLog()) {
  // Synthesize the bits of a request that we actually use.
  request_.url = GURL("https://" + endpoint.ToString());
  request_.method = "CONNECT";
  if (!user_agent.empty())
    request_.extra_headers.SetHeader(HttpRequestHeaders::kUserAgent,
                                     user_agent);
}

HttpProxyClientSocket::~HttpProxyClientSocket() {
  Disconnect();
}

int HttpProxyClientSocket::RestartWithAuth(const CompletionCallback& callback) {
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(user_callback_.is_null());

  int rv = PrepareForAuthRestart();
  if (rv != OK)
    return rv;

  rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    if (!callback.is_null())
      user_callback_ =  callback;
  }

  return rv;
}

const scoped_refptr<HttpAuthController>&
HttpProxyClientSocket::GetAuthController() const {
  return auth_;
}

bool HttpProxyClientSocket::IsUsingSpdy() const {
  return using_spdy_;
}

NextProto HttpProxyClientSocket::GetProxyNegotiatedProtocol() const {
  return negotiated_protocol_;
}

const HttpResponseInfo* HttpProxyClientSocket::GetConnectResponseInfo() const {
  return response_.headers.get() ? &response_ : NULL;
}

std::unique_ptr<HttpStream>
HttpProxyClientSocket::CreateConnectResponseStream() {
  return std::make_unique<ProxyConnectRedirectHttpStream>(
      redirect_has_load_timing_info_ ? &redirect_load_timing_info_ : nullptr);
}


int HttpProxyClientSocket::Connect(const CompletionCallback& callback) {
  DCHECK(transport_.get());
  DCHECK(transport_->socket());
  DCHECK(user_callback_.is_null());

  // TODO(rch): figure out the right way to set up a tunnel with SPDY.
  // This approach sends the complete HTTPS request to the proxy
  // which allows the proxy to see "private" data.  Instead, we should
  // create an SSL tunnel to the origin server using the CONNECT method
  // inside a single SPDY stream.
  if (using_spdy_ || !tunnel_)
    next_state_ = STATE_DONE;
  if (next_state_ == STATE_DONE)
    return OK;

  DCHECK_EQ(STATE_NONE, next_state_);
  next_state_ = STATE_GENERATE_AUTH_TOKEN;

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    user_callback_ = callback;
  return rv;
}

void HttpProxyClientSocket::Disconnect() {
  if (transport_.get())
    transport_->socket()->Disconnect();

  // Reset other states to make sure they aren't mistakenly used later.
  // These are the states initialized by Connect().
  next_state_ = STATE_NONE;
  user_callback_.Reset();
}

bool HttpProxyClientSocket::IsConnected() const {
  return next_state_ == STATE_DONE && transport_->socket()->IsConnected();
}

bool HttpProxyClientSocket::IsConnectedAndIdle() const {
  return next_state_ == STATE_DONE &&
    transport_->socket()->IsConnectedAndIdle();
}

const NetLogWithSource& HttpProxyClientSocket::NetLog() const {
  return net_log_;
}

void HttpProxyClientSocket::SetSubresourceSpeculation() {
  if (transport_.get() && transport_->socket()) {
    transport_->socket()->SetSubresourceSpeculation();
  } else {
    NOTREACHED();
  }
}

void HttpProxyClientSocket::SetOmniboxSpeculation() {
  if (transport_.get() && transport_->socket()) {
    transport_->socket()->SetOmniboxSpeculation();
  } else {
    NOTREACHED();
  }
}

bool HttpProxyClientSocket::WasEverUsed() const {
  if (transport_.get() && transport_->socket()) {
    return transport_->socket()->WasEverUsed();
  }
  NOTREACHED();
  return false;
}

bool HttpProxyClientSocket::WasAlpnNegotiated() const {
  if (transport_.get() && transport_->socket()) {
    return transport_->socket()->WasAlpnNegotiated();
  }
  NOTREACHED();
  return false;
}

NextProto HttpProxyClientSocket::GetNegotiatedProtocol() const {
  if (transport_.get() && transport_->socket()) {
    return transport_->socket()->GetNegotiatedProtocol();
  }
  NOTREACHED();
  return kProtoUnknown;
}

bool HttpProxyClientSocket::GetSSLInfo(SSLInfo* ssl_info) {
  if (transport_.get() && transport_->socket()) {
    return transport_->socket()->GetSSLInfo(ssl_info);
  }
  NOTREACHED();
  return false;
}

void HttpProxyClientSocket::GetConnectionAttempts(
    ConnectionAttempts* out) const {
  out->clear();
}

int64_t HttpProxyClientSocket::GetTotalReceivedBytes() const {
  return transport_->socket()->GetTotalReceivedBytes();
}

int HttpProxyClientSocket::Read(IOBuffer* buf, int buf_len,
                                const CompletionCallback& callback) {
  DCHECK(user_callback_.is_null());
  if (next_state_ != STATE_DONE) {
    // We're trying to read the body of the response but we're still trying
    // to establish an SSL tunnel through the proxy.  We can't read these
    // bytes when establishing a tunnel because they might be controlled by
    // an active network attacker.  We don't worry about this for HTTP
    // because an active network attacker can already control HTTP sessions.
    // We reach this case when the user cancels a 407 proxy auth prompt.
    // See http://crbug.com/8473.
    DCHECK_EQ(407, response_.headers->response_code());
    LogBlockedTunnelResponse();

    return ERR_TUNNEL_CONNECTION_FAILED;
  }

  return transport_->socket()->Read(buf, buf_len, callback);
}

int HttpProxyClientSocket::Write(IOBuffer* buf, int buf_len,
                                 const CompletionCallback& callback) {
  DCHECK_EQ(STATE_DONE, next_state_);
  DCHECK(user_callback_.is_null());

  return transport_->socket()->Write(buf, buf_len, callback);
}

int HttpProxyClientSocket::SetReceiveBufferSize(int32_t size) {
  return transport_->socket()->SetReceiveBufferSize(size);
}

int HttpProxyClientSocket::SetSendBufferSize(int32_t size) {
  return transport_->socket()->SetSendBufferSize(size);
}

int HttpProxyClientSocket::GetPeerAddress(IPEndPoint* address) const {
  return transport_->socket()->GetPeerAddress(address);
}

int HttpProxyClientSocket::GetLocalAddress(IPEndPoint* address) const {
  return transport_->socket()->GetLocalAddress(address);
}

int HttpProxyClientSocket::PrepareForAuthRestart() {
  if (!response_.headers.get())
    return ERR_CONNECTION_RESET;

  // If the connection can't be reused, return
  // ERR_UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH.  The request will be retried
  // at a higher layer.
  if (!response_.headers->IsKeepAlive() ||
      !http_stream_parser_->CanFindEndOfResponse() ||
      !transport_->socket()->IsConnected()) {
    transport_->socket()->Disconnect();
    return ERR_UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH;
  }

  // If the auth request had a body, need to drain it before reusing the socket.
  if (!http_stream_parser_->IsResponseBodyComplete()) {
    next_state_ = STATE_DRAIN_BODY;
    drain_buf_ = new IOBuffer(kDrainBodyBufferSize);
    return OK;
  }

  return DidDrainBodyForAuthRestart();
}

int HttpProxyClientSocket::DidDrainBodyForAuthRestart() {
  // Can't reuse the socket if there's still unread data on it.
  if (!transport_->socket()->IsConnectedAndIdle())
    return ERR_UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH;

  next_state_ = STATE_GENERATE_AUTH_TOKEN;
  transport_->set_reuse_type(ClientSocketHandle::REUSED_IDLE);

  // Reset the other member variables.
  drain_buf_ = nullptr;
  parser_buf_ = nullptr;
  http_stream_parser_.reset();
  request_line_.clear();
  request_headers_.Clear();
  response_ = HttpResponseInfo();
  return OK;
}

void HttpProxyClientSocket::LogBlockedTunnelResponse() const {
  ProxyClientSocket::LogBlockedTunnelResponse(
      response_.headers->response_code(),
      is_https_proxy_);
}

void HttpProxyClientSocket::DoCallback(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(!user_callback_.is_null());

  // Since Run() may result in Read being called,
  // clear user_callback_ up front.
  CompletionCallback c = user_callback_;
  user_callback_.Reset();
  c.Run(result);
}

void HttpProxyClientSocket::OnIOComplete(int result) {
  DCHECK_NE(STATE_NONE, next_state_);
  DCHECK_NE(STATE_DONE, next_state_);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    DoCallback(rv);
}

int HttpProxyClientSocket::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_NONE);
  DCHECK_NE(next_state_, STATE_DONE);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_GENERATE_AUTH_TOKEN:
        DCHECK_EQ(OK, rv);
        rv = DoGenerateAuthToken();
        break;
      case STATE_GENERATE_AUTH_TOKEN_COMPLETE:
        rv = DoGenerateAuthTokenComplete(rv);
        break;
      case STATE_SEND_REQUEST:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_SEND_REQUEST);
        rv = DoSendRequest();
        break;
      case STATE_SEND_REQUEST_COMPLETE:
        rv = DoSendRequestComplete(rv);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_SEND_REQUEST, rv);
        break;
      case STATE_READ_HEADERS:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_READ_HEADERS);
        rv = DoReadHeaders();
        break;
      case STATE_READ_HEADERS_COMPLETE:
        rv = DoReadHeadersComplete(rv);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_READ_HEADERS, rv);
        break;
      case STATE_DRAIN_BODY:
        DCHECK_EQ(OK, rv);
        rv = DoDrainBody();
        break;
      case STATE_DRAIN_BODY_COMPLETE:
        rv = DoDrainBodyComplete(rv);
        break;
      case STATE_DONE:
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE &&
           next_state_ != STATE_DONE);
  return rv;
}

int HttpProxyClientSocket::DoGenerateAuthToken() {
  next_state_ = STATE_GENERATE_AUTH_TOKEN_COMPLETE;
  return auth_->MaybeGenerateAuthToken(&request_, io_callback_, net_log_);
}

int HttpProxyClientSocket::DoGenerateAuthTokenComplete(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  if (result == OK)
    next_state_ = STATE_SEND_REQUEST;
  return result;
}

int HttpProxyClientSocket::DoSendRequest() {
  next_state_ = STATE_SEND_REQUEST_COMPLETE;

  // This is constructed lazily (instead of within our Start method), so that
  // we have proxy info available.
  if (request_line_.empty()) {
    DCHECK(request_headers_.IsEmpty());
    HttpRequestHeaders authorization_headers;
    if (auth_->HaveAuth())
      auth_->AddAuthorizationHeader(&authorization_headers);
    if (proxy_delegate_) {
      proxy_delegate_->OnBeforeTunnelRequest(proxy_server_,
                                             &authorization_headers);
    }
    std::string user_agent;
    if (!request_.extra_headers.GetHeader(HttpRequestHeaders::kUserAgent,
                                          &user_agent)) {
      user_agent.clear();
    }
    BuildTunnelRequest(endpoint_, authorization_headers, user_agent,
                       &request_line_, &request_headers_);

    net_log_.AddEvent(
        NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
        base::Bind(&HttpRequestHeaders::NetLogCallback,
                   base::Unretained(&request_headers_), &request_line_));
  }

  parser_buf_ = new GrowableIOBuffer();
  http_stream_parser_.reset(new HttpStreamParser(
      transport_.get(), &request_, parser_buf_.get(), net_log_));
  return http_stream_parser_->SendRequest(
      request_line_, request_headers_, &response_, io_callback_);
}

int HttpProxyClientSocket::DoSendRequestComplete(int result) {
  if (result < 0)
    return result;

  next_state_ = STATE_READ_HEADERS;
  return OK;
}

int HttpProxyClientSocket::DoReadHeaders() {
  next_state_ = STATE_READ_HEADERS_COMPLETE;
  return http_stream_parser_->ReadResponseHeaders(io_callback_);
}

int HttpProxyClientSocket::DoReadHeadersComplete(int result) {
  if (result < 0)
    return result;

  // Require the "HTTP/1.x" status line for SSL CONNECT.
  if (response_.headers->GetHttpVersion() < HttpVersion(1, 0))
    return ERR_TUNNEL_CONNECTION_FAILED;

  net_log_.AddEvent(
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      base::Bind(&HttpResponseHeaders::NetLogCallback, response_.headers));

  if (proxy_delegate_) {
    proxy_delegate_->OnTunnelHeadersReceived(
        HostPortPair::FromURL(request_.url),
        proxy_server_,
        *response_.headers);
  }

  switch (response_.headers->response_code()) {
    case 200:  // OK
      if (http_stream_parser_->IsMoreDataBuffered())
        // The proxy sent extraneous data after the headers.
        return ERR_TUNNEL_CONNECTION_FAILED;

      next_state_ = STATE_DONE;
      return OK;

      // We aren't able to CONNECT to the remote host through the proxy.  We
      // need to be very suspicious about the response because an active network
      // attacker can force us into this state by masquerading as the proxy.
      // The only safe thing to do here is to fail the connection because our
      // client is expecting an SSL protected response.
      // See http://crbug.com/7338.

    case 302:  // Found / Moved Temporarily
      // Attempt to follow redirects from HTTPS proxies, but only if we can
      // sanitize the response.  This still allows a rogue HTTPS proxy to
      // redirect an HTTPS site load to a similar-looking site, but no longer
      // allows it to impersonate the site the user requested.
      if (!is_https_proxy_ || !SanitizeProxyRedirect(&response_)) {
        LogBlockedTunnelResponse();
        return ERR_TUNNEL_CONNECTION_FAILED;
      }

      redirect_has_load_timing_info_ = transport_->GetLoadTimingInfo(
          http_stream_parser_->IsConnectionReused(),
          &redirect_load_timing_info_);
      transport_.reset();
      http_stream_parser_.reset();
      return ERR_HTTPS_PROXY_TUNNEL_RESPONSE;

    case 407:  // Proxy Authentication Required
      // We need this status code to allow proxy authentication.  Our
      // authentication code is smart enough to avoid being tricked by an
      // active network attacker.
      // The next state is intentionally not set as it should be STATE_NONE;
      if (!SanitizeProxyAuth(&response_)) {
        LogBlockedTunnelResponse();
        return ERR_TUNNEL_CONNECTION_FAILED;
      }
      return HandleProxyAuthChallenge(auth_.get(), &response_, net_log_);

    default:
      // Ignore response to avoid letting the proxy impersonate the target
      // server.  (See http://crbug.com/137891.)
      // We lose something by doing this.  We have seen proxy 403, 404, and
      // 501 response bodies that contain a useful error message.  For
      // example, Squid uses a 404 response to report the DNS error: "The
      // domain name does not exist."
      LogBlockedTunnelResponse();
      return ERR_TUNNEL_CONNECTION_FAILED;
  }
}

int HttpProxyClientSocket::DoDrainBody() {
  DCHECK(drain_buf_.get());
  next_state_ = STATE_DRAIN_BODY_COMPLETE;
  return http_stream_parser_->ReadResponseBody(
      drain_buf_.get(), kDrainBodyBufferSize, io_callback_);
}

int HttpProxyClientSocket::DoDrainBodyComplete(int result) {
  if (result < 0)
    return ERR_UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH;

  if (!http_stream_parser_->IsResponseBodyComplete()) {
    // Keep draining.
    next_state_ = STATE_DRAIN_BODY;
    return OK;
  }

  return DidDrainBodyForAuthRestart();
}

//----------------------------------------------------------------

}  // namespace net
