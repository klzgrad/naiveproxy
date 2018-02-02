// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_factory_impl_request.h"

#include <utility>

#include "base/callback.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "net/http/bidirectional_stream_impl.h"
#include "net/http/http_stream_factory_impl_job.h"
#include "net/log/net_log_event_type.h"
#include "net/spdy/chromium/spdy_http_stream.h"
#include "net/spdy/chromium/spdy_session.h"

namespace net {

HttpStreamFactoryImpl::Request::Request(
    const GURL& url,
    Helper* helper,
    HttpStreamRequest::Delegate* delegate,
    WebSocketHandshakeStreamBase::CreateHelper*
        websocket_handshake_stream_create_helper,
    const NetLogWithSource& net_log,
    StreamType stream_type)
    : url_(url),
      helper_(helper),
      websocket_handshake_stream_create_helper_(
          websocket_handshake_stream_create_helper),
      net_log_(net_log),
      completed_(false),
      was_alpn_negotiated_(false),
      negotiated_protocol_(kProtoUnknown),
      using_spdy_(false),
      stream_type_(stream_type) {
  net_log_.BeginEvent(NetLogEventType::HTTP_STREAM_REQUEST);
}

HttpStreamFactoryImpl::Request::~Request() {
  net_log_.EndEvent(NetLogEventType::HTTP_STREAM_REQUEST);
  helper_->OnRequestComplete();
}

void HttpStreamFactoryImpl::Request::Complete(bool was_alpn_negotiated,
                                              NextProto negotiated_protocol,
                                              bool using_spdy) {
  DCHECK(!completed_);
  completed_ = true;
  was_alpn_negotiated_ = was_alpn_negotiated;
  negotiated_protocol_ = negotiated_protocol;
  using_spdy_ = using_spdy;
}

void HttpStreamFactoryImpl::Request::OnStreamReadyOnPooledConnection(
    const SSLConfig& used_ssl_config,
    const ProxyInfo& used_proxy_info,
    std::unique_ptr<HttpStream> stream) {
  DCHECK(completed_);
  helper_->OnStreamReadyOnPooledConnection(used_ssl_config, used_proxy_info,
                                           std::move(stream));
}

void HttpStreamFactoryImpl::Request::
    OnBidirectionalStreamImplReadyOnPooledConnection(
        const SSLConfig& used_ssl_config,
        const ProxyInfo& used_proxy_info,
        std::unique_ptr<BidirectionalStreamImpl> stream) {
  DCHECK(completed_);
  helper_->OnBidirectionalStreamImplReadyOnPooledConnection(
      used_ssl_config, used_proxy_info, std::move(stream));
}

int HttpStreamFactoryImpl::Request::RestartTunnelWithProxyAuth() {
  return helper_->RestartTunnelWithProxyAuth();
}

void HttpStreamFactoryImpl::Request::SetPriority(RequestPriority priority) {
  helper_->SetPriority(priority);
}

LoadState HttpStreamFactoryImpl::Request::GetLoadState() const {
  return helper_->GetLoadState();
}

bool HttpStreamFactoryImpl::Request::was_alpn_negotiated() const {
  DCHECK(completed_);
  return was_alpn_negotiated_;
}

NextProto HttpStreamFactoryImpl::Request::negotiated_protocol() const {
  DCHECK(completed_);
  return negotiated_protocol_;
}

bool HttpStreamFactoryImpl::Request::using_spdy() const {
  DCHECK(completed_);
  return using_spdy_;
}

const ConnectionAttempts& HttpStreamFactoryImpl::Request::connection_attempts()
    const {
  return connection_attempts_;
}

void HttpStreamFactoryImpl::Request::AddConnectionAttempts(
    const ConnectionAttempts& attempts) {
  for (const auto& attempt : attempts)
    connection_attempts_.push_back(attempt);
}

}  // namespace net
