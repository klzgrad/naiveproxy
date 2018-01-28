// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_proxy_client_socket.h"

#include <algorithm>  // min
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "net/base/auth.h"
#include "net/base/io_buffer.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/proxy_connect_redirect_http_stream.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/spdy/chromium/spdy_http_utils.h"
#include "url/gurl.h"

namespace net {

SpdyProxyClientSocket::SpdyProxyClientSocket(
    const base::WeakPtr<SpdyStream>& spdy_stream,
    const SpdyString& user_agent,
    const HostPortPair& endpoint,
    const NetLogWithSource& source_net_log,
    HttpAuthController* auth_controller)
    : next_state_(STATE_DISCONNECTED),
      spdy_stream_(spdy_stream),
      endpoint_(endpoint),
      auth_(auth_controller),
      user_agent_(user_agent),
      user_buffer_len_(0),
      write_buffer_len_(0),
      was_ever_used_(false),
      redirect_has_load_timing_info_(false),
      net_log_(NetLogWithSource::Make(spdy_stream->net_log().net_log(),
                                      NetLogSourceType::PROXY_CLIENT_SOCKET)),
      source_dependency_(source_net_log.source()),
      weak_factory_(this),
      write_callback_weak_factory_(this) {
  request_.method = "CONNECT";
  request_.url = GURL("https://" + endpoint.ToString());
  net_log_.BeginEvent(NetLogEventType::SOCKET_ALIVE,
                      source_net_log.source().ToEventParametersCallback());
  net_log_.AddEvent(
      NetLogEventType::HTTP2_PROXY_CLIENT_SESSION,
      spdy_stream->net_log().source().ToEventParametersCallback());

  spdy_stream_->SetDelegate(this);
  was_ever_used_ = spdy_stream_->WasEverUsed();
}

SpdyProxyClientSocket::~SpdyProxyClientSocket() {
  Disconnect();
  net_log_.EndEvent(NetLogEventType::SOCKET_ALIVE);
}

const HttpResponseInfo* SpdyProxyClientSocket::GetConnectResponseInfo() const {
  return response_.headers.get() ? &response_ : NULL;
}

const scoped_refptr<HttpAuthController>&
SpdyProxyClientSocket::GetAuthController() const {
  return auth_;
}

int SpdyProxyClientSocket::RestartWithAuth(const CompletionCallback& callback) {
  // A SPDY Stream can only handle a single request, so the underlying
  // stream may not be reused and a new SpdyProxyClientSocket must be
  // created (possibly on top of the same SPDY Session).
  next_state_ = STATE_DISCONNECTED;
  return OK;
}

bool SpdyProxyClientSocket::IsUsingSpdy() const {
  return true;
}

NextProto SpdyProxyClientSocket::GetProxyNegotiatedProtocol() const {
  return spdy_stream_->GetNegotiatedProtocol();
}

std::unique_ptr<HttpStream>
SpdyProxyClientSocket::CreateConnectResponseStream() {
  return std::make_unique<ProxyConnectRedirectHttpStream>(
      redirect_has_load_timing_info_ ? &redirect_load_timing_info_ : nullptr);
}

// Sends a HEADERS frame to the proxy with a CONNECT request
// for the specified endpoint.  Waits for the server to send back
// a HEADERS frame.  OK will be returned if the status is 200.
// ERR_TUNNEL_CONNECTION_FAILED will be returned for any other status.
// In any of these cases, Read() may be called to retrieve the HTTP
// response body.  Any other return values should be considered fatal.
// TODO(rch): handle 407 proxy auth requested correctly, perhaps
// by creating a new stream for the subsequent request.
// TODO(rch): create a more appropriate error code to disambiguate
// the HTTPS Proxy tunnel failure from an HTTP Proxy tunnel failure.
int SpdyProxyClientSocket::Connect(const CompletionCallback& callback) {
  DCHECK(read_callback_.is_null());
  if (next_state_ == STATE_OPEN)
    return OK;

  DCHECK_EQ(STATE_DISCONNECTED, next_state_);
  next_state_ = STATE_GENERATE_AUTH_TOKEN;

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    read_callback_ = callback;
  return rv;
}

void SpdyProxyClientSocket::Disconnect() {
  read_buffer_queue_.Clear();
  user_buffer_ = NULL;
  user_buffer_len_ = 0;
  read_callback_.Reset();

  write_buffer_len_ = 0;
  write_callback_.Reset();
  write_callback_weak_factory_.InvalidateWeakPtrs();

  next_state_ = STATE_DISCONNECTED;

  if (spdy_stream_.get()) {
    // This will cause OnClose to be invoked, which takes care of
    // cleaning up all the internal state.
    spdy_stream_->Cancel();
    DCHECK(!spdy_stream_.get());
  }
}

bool SpdyProxyClientSocket::IsConnected() const {
  return next_state_ == STATE_OPEN;
}

bool SpdyProxyClientSocket::IsConnectedAndIdle() const {
  return IsConnected() && read_buffer_queue_.IsEmpty() &&
      spdy_stream_->IsOpen();
}

const NetLogWithSource& SpdyProxyClientSocket::NetLog() const {
  return net_log_;
}

void SpdyProxyClientSocket::SetSubresourceSpeculation() {
  // TODO(rch): what should this implementation be?
}

void SpdyProxyClientSocket::SetOmniboxSpeculation() {
  // TODO(rch): what should this implementation be?
}

bool SpdyProxyClientSocket::WasEverUsed() const {
  return was_ever_used_ || (spdy_stream_.get() && spdy_stream_->WasEverUsed());
}

bool SpdyProxyClientSocket::WasAlpnNegotiated() const {
  return false;
}

NextProto SpdyProxyClientSocket::GetNegotiatedProtocol() const {
  return kProtoUnknown;
}

bool SpdyProxyClientSocket::GetSSLInfo(SSLInfo* ssl_info) {
  return spdy_stream_->GetSSLInfo(ssl_info);
}

void SpdyProxyClientSocket::GetConnectionAttempts(
    ConnectionAttempts* out) const {
  out->clear();
}

int64_t SpdyProxyClientSocket::GetTotalReceivedBytes() const {
  NOTIMPLEMENTED();
  return 0;
}

int SpdyProxyClientSocket::Read(IOBuffer* buf, int buf_len,
                                const CompletionCallback& callback) {
  DCHECK(read_callback_.is_null());
  DCHECK(!user_buffer_.get());

  if (next_state_ == STATE_DISCONNECTED)
    return ERR_SOCKET_NOT_CONNECTED;

  if (next_state_ == STATE_CLOSED && read_buffer_queue_.IsEmpty()) {
    return 0;
  }

  DCHECK(next_state_ == STATE_OPEN || next_state_ == STATE_CLOSED);
  DCHECK(buf);
  size_t result = PopulateUserReadBuffer(buf->data(), buf_len);
  if (result == 0) {
    user_buffer_ = buf;
    user_buffer_len_ = static_cast<size_t>(buf_len);
    DCHECK(!callback.is_null());
    read_callback_ = callback;
    return ERR_IO_PENDING;
  }
  user_buffer_ = NULL;
  return result;
}

size_t SpdyProxyClientSocket::PopulateUserReadBuffer(char* data, size_t len) {
  return read_buffer_queue_.Dequeue(data, len);
}

int SpdyProxyClientSocket::Write(IOBuffer* buf, int buf_len,
                                 const CompletionCallback& callback) {
  DCHECK(write_callback_.is_null());
  if (next_state_ != STATE_OPEN)
    return ERR_SOCKET_NOT_CONNECTED;

  DCHECK(spdy_stream_.get());
  spdy_stream_->SendData(buf, buf_len, MORE_DATA_TO_SEND);
  net_log_.AddByteTransferEvent(NetLogEventType::SOCKET_BYTES_SENT, buf_len,
                                buf->data());
  write_callback_ = callback;
  write_buffer_len_ = buf_len;
  return ERR_IO_PENDING;
}

int SpdyProxyClientSocket::SetReceiveBufferSize(int32_t size) {
  // Since this StreamSocket sits on top of a shared SpdySession, it
  // is not safe for callers to change this underlying socket.
  return ERR_NOT_IMPLEMENTED;
}

int SpdyProxyClientSocket::SetSendBufferSize(int32_t size) {
  // Since this StreamSocket sits on top of a shared SpdySession, it
  // is not safe for callers to change this underlying socket.
  return ERR_NOT_IMPLEMENTED;
}

int SpdyProxyClientSocket::GetPeerAddress(IPEndPoint* address) const {
  if (!IsConnected())
    return ERR_SOCKET_NOT_CONNECTED;
  return spdy_stream_->GetPeerAddress(address);
}

int SpdyProxyClientSocket::GetLocalAddress(IPEndPoint* address) const {
  if (!IsConnected())
    return ERR_SOCKET_NOT_CONNECTED;
  return spdy_stream_->GetLocalAddress(address);
}

void SpdyProxyClientSocket::LogBlockedTunnelResponse() const {
  ProxyClientSocket::LogBlockedTunnelResponse(
      response_.headers->response_code(),
      /* is_https_proxy = */ true);
}

void SpdyProxyClientSocket::RunCallback(const CompletionCallback& callback,
                                        int result) const {
  callback.Run(result);
}

void SpdyProxyClientSocket::OnIOComplete(int result) {
  DCHECK_NE(STATE_DISCONNECTED, next_state_);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    CompletionCallback c = read_callback_;
    read_callback_.Reset();
    c.Run(rv);
  }
}

int SpdyProxyClientSocket::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_DISCONNECTED);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_DISCONNECTED;
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
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_SEND_REQUEST, rv);
        rv = DoSendRequestComplete(rv);
        if (rv >= 0 || rv == ERR_IO_PENDING) {
          // Emit extra event so can use the same events as
          // HttpProxyClientSocket.
          net_log_.BeginEvent(
              NetLogEventType::HTTP_TRANSACTION_TUNNEL_READ_HEADERS);
        }
        break;
      case STATE_READ_REPLY_COMPLETE:
        rv = DoReadReplyComplete(rv);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::HTTP_TRANSACTION_TUNNEL_READ_HEADERS, rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_DISCONNECTED &&
           next_state_ != STATE_OPEN);
  return rv;
}

int SpdyProxyClientSocket::DoGenerateAuthToken() {
  next_state_ = STATE_GENERATE_AUTH_TOKEN_COMPLETE;
  return auth_->MaybeGenerateAuthToken(
      &request_,
      base::Bind(&SpdyProxyClientSocket::OnIOComplete,
                 weak_factory_.GetWeakPtr()),
      net_log_);
}

int SpdyProxyClientSocket::DoGenerateAuthTokenComplete(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  if (result == OK)
    next_state_ = STATE_SEND_REQUEST;
  return result;
}

int SpdyProxyClientSocket::DoSendRequest() {
  next_state_ = STATE_SEND_REQUEST_COMPLETE;

  // Add Proxy-Authentication header if necessary.
  HttpRequestHeaders authorization_headers;
  if (auth_->HaveAuth()) {
    auth_->AddAuthorizationHeader(&authorization_headers);
  }

  SpdyString request_line;
  BuildTunnelRequest(endpoint_, authorization_headers, user_agent_,
                     &request_line, &request_.extra_headers);

  net_log_.AddEvent(
      NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      base::Bind(&HttpRequestHeaders::NetLogCallback,
                 base::Unretained(&request_.extra_headers), &request_line));

  SpdyHeaderBlock headers;
  CreateSpdyHeadersFromHttpRequest(request_, request_.extra_headers, true,
                                   &headers);

  return spdy_stream_->SendRequestHeaders(std::move(headers),
                                          MORE_DATA_TO_SEND);
}

int SpdyProxyClientSocket::DoSendRequestComplete(int result) {
  if (result < 0)
    return result;

  // Wait for HEADERS frame from the server
  next_state_ = STATE_READ_REPLY_COMPLETE;
  return ERR_IO_PENDING;
}

int SpdyProxyClientSocket::DoReadReplyComplete(int result) {
  // We enter this method directly from DoSendRequestComplete, since
  // we are notified by a callback when the HEADERS frame arrives.

  if (result < 0)
    return result;

  // Require the "HTTP/1.x" status line for SSL CONNECT.
  if (response_.headers->GetHttpVersion() < HttpVersion(1, 0))
    return ERR_TUNNEL_CONNECTION_FAILED;

  net_log_.AddEvent(
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      base::Bind(&HttpResponseHeaders::NetLogCallback, response_.headers));

  switch (response_.headers->response_code()) {
    case 200:  // OK
      next_state_ = STATE_OPEN;
      return OK;

    case 302:  // Found / Moved Temporarily
      // Try to return a sanitized response so we can follow auth redirects.
      // If we can't, fail the tunnel connection.
      if (!SanitizeProxyRedirect(&response_)) {
        LogBlockedTunnelResponse();
        return ERR_TUNNEL_CONNECTION_FAILED;
      }

      redirect_has_load_timing_info_ =
          spdy_stream_->GetLoadTimingInfo(&redirect_load_timing_info_);
      // Note that this triggers a ERROR_CODE_CANCEL.
      spdy_stream_->DetachDelegate();
      next_state_ = STATE_DISCONNECTED;
      return ERR_HTTPS_PROXY_TUNNEL_RESPONSE;

    case 407:  // Proxy Authentication Required
      next_state_ = STATE_OPEN;
      if (!SanitizeProxyAuth(&response_)) {
        LogBlockedTunnelResponse();
        return ERR_TUNNEL_CONNECTION_FAILED;
      }
      return HandleProxyAuthChallenge(auth_.get(), &response_, net_log_);

    default:
      // Ignore response to avoid letting the proxy impersonate the target
      // server.  (See http://crbug.com/137891.)
      LogBlockedTunnelResponse();
      return ERR_TUNNEL_CONNECTION_FAILED;
  }
}

// SpdyStream::Delegate methods:
// Called when SYN frame has been sent.
// Returns true if no more data to be sent after SYN frame.
void SpdyProxyClientSocket::OnHeadersSent() {
  DCHECK_EQ(next_state_, STATE_SEND_REQUEST_COMPLETE);

  OnIOComplete(OK);
}

void SpdyProxyClientSocket::OnHeadersReceived(
    const SpdyHeaderBlock& response_headers) {
  // If we've already received the reply, existing headers are too late.
  // TODO(mbelshe): figure out a way to make HEADERS frames useful after the
  //                initial response.
  if (next_state_ != STATE_READ_REPLY_COMPLETE)
    return;

  // Save the response
  const bool headers_valid =
      SpdyHeadersToHttpResponse(response_headers, &response_);
  DCHECK(headers_valid);

  OnIOComplete(OK);
}

// Called when data is received or on EOF (if |buffer| is NULL).
void SpdyProxyClientSocket::OnDataReceived(std::unique_ptr<SpdyBuffer> buffer) {
  if (buffer) {
    net_log_.AddByteTransferEvent(NetLogEventType::SOCKET_BYTES_RECEIVED,
                                  buffer->GetRemainingSize(),
                                  buffer->GetRemainingData());
    read_buffer_queue_.Enqueue(std::move(buffer));
  } else {
    net_log_.AddByteTransferEvent(NetLogEventType::SOCKET_BYTES_RECEIVED, 0,
                                  NULL);
  }

  if (!read_callback_.is_null()) {
    int rv = PopulateUserReadBuffer(user_buffer_->data(), user_buffer_len_);
    CompletionCallback c = read_callback_;
    read_callback_.Reset();
    user_buffer_ = NULL;
    user_buffer_len_ = 0;
    c.Run(rv);
  }
}

void SpdyProxyClientSocket::OnDataSent()  {
  DCHECK(!write_callback_.is_null());

  int rv = write_buffer_len_;
  write_buffer_len_ = 0;

  // Proxy write callbacks result in deep callback chains. Post to allow the
  // stream's write callback chain to unwind (see crbug.com/355511).
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&SpdyProxyClientSocket::RunCallback,
                            write_callback_weak_factory_.GetWeakPtr(),
                            base::ResetAndReturn(&write_callback_), rv));
}

void SpdyProxyClientSocket::OnTrailers(const SpdyHeaderBlock& trailers) {
  // |spdy_stream_| is of type SPDY_BIDIRECTIONAL_STREAM, so trailers are
  // combined with response headers and this method will not be calld.
  NOTREACHED();
}

void SpdyProxyClientSocket::OnClose(int status)  {
  was_ever_used_ = spdy_stream_->WasEverUsed();
  spdy_stream_.reset();

  bool connecting = next_state_ != STATE_DISCONNECTED &&
      next_state_ < STATE_OPEN;
  if (next_state_ == STATE_OPEN)
    next_state_ = STATE_CLOSED;
  else
    next_state_ = STATE_DISCONNECTED;

  base::WeakPtr<SpdyProxyClientSocket> weak_ptr = weak_factory_.GetWeakPtr();
  CompletionCallback write_callback = write_callback_;
  write_callback_.Reset();
  write_buffer_len_ = 0;

  // If we're in the middle of connecting, we need to make sure
  // we invoke the connect callback.
  if (connecting) {
    DCHECK(!read_callback_.is_null());
    CompletionCallback read_callback = read_callback_;
    read_callback_.Reset();
    read_callback.Run(status);
  } else if (!read_callback_.is_null()) {
    // If we have a read_callback_, the we need to make sure we call it back.
    OnDataReceived(std::unique_ptr<SpdyBuffer>());
  }
  // This may have been deleted by read_callback_, so check first.
  if (weak_ptr.get() && !write_callback.is_null())
    write_callback.Run(ERR_CONNECTION_CLOSED);
}

NetLogSource SpdyProxyClientSocket::source_dependency() const {
  return source_dependency_;
}

}  // namespace net
