// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_proxy_client_socket_wrapper.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "net/base/proxy_delegate.h"
#include "net/http/http_proxy_client_socket.h"
#include "net/http/http_response_info.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"
#include "net/quic/quic_http_utils.h"
#include "net/quic/quic_proxy_client_socket.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/socket_tag.h"
#include "net/socket/ssl_connect_job.h"
#include "net/socket/transport_connect_job.h"
#include "net/spdy/spdy_proxy_client_socket.h"
#include "net/spdy/spdy_session.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/spdy/spdy_stream.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace net {

HttpProxyClientSocketWrapper::HttpProxyClientSocketWrapper(
    const OnProxyAuthChallengeCallback& on_proxy_auth_callback,
    RequestPriority priority,
    base::TimeDelta connect_timeout_duration,
    base::TimeDelta proxy_negotiation_timeout_duration,
    const CommonConnectJobParams& common_connect_job_params,
    const scoped_refptr<TransportSocketParams>& transport_params,
    const scoped_refptr<SSLSocketParams>& ssl_params,
    quic::QuicTransportVersion quic_version,
    const std::string& user_agent,
    const HostPortPair& endpoint,
    HttpAuthCache* http_auth_cache,
    HttpAuthHandlerFactory* http_auth_handler_factory,
    SpdySessionPool* spdy_session_pool,
    QuicStreamFactory* quic_stream_factory,
    bool is_trusted_proxy,
    bool tunnel,
    const NetworkTrafficAnnotationTag& traffic_annotation,
    const NetLogWithSource& net_log)
    : on_proxy_auth_callback_(on_proxy_auth_callback),
      next_state_(STATE_NONE),
      priority_(priority),
      connect_timeout_duration_(connect_timeout_duration),
      proxy_negotiation_timeout_duration_(proxy_negotiation_timeout_duration),
      transport_params_(transport_params),
      ssl_params_(ssl_params),
      quic_version_(quic_version),
      user_agent_(user_agent),
      endpoint_(endpoint),
      spdy_session_pool_(spdy_session_pool),
      has_restarted_(false),
      tunnel_(tunnel),
      common_connect_job_params_(common_connect_job_params),
      using_spdy_(false),
      is_trusted_proxy_(is_trusted_proxy),
      has_established_connection_(false),
      quic_stream_factory_(quic_stream_factory),
      http_auth_controller_(
          tunnel ? new HttpAuthController(
                       HttpAuth::AUTH_PROXY,
                       GURL((ssl_params_.get() ? "https://" : "http://") +
                            GetDestination().ToString()),
                       http_auth_cache,
                       http_auth_handler_factory,
                       common_connect_job_params_.host_resolver)
                 : nullptr),
      net_log_(NetLogWithSource::Make(
          net_log.net_log(),
          NetLogSourceType::PROXY_CLIENT_SOCKET_WRAPPER)),
      traffic_annotation_(traffic_annotation),
      weak_ptr_factory_(this) {
  net_log_.BeginEvent(NetLogEventType::SOCKET_ALIVE,
                      net_log.source().ToEventParametersCallback());
  // If doing a QUIC proxy, |quic_version| must not be
  // quic::QUIC_VERSION_UNSUPPORTED, and |ssl_params| must be valid while
  // |transport_params| is null. Otherwise, |quic_version| must be
  // quic::QUIC_VERSION_UNSUPPORTED, and exactly one of |transport_params| or
  // |ssl_params| must be set.
  DCHECK(quic_version_ == quic::QUIC_VERSION_UNSUPPORTED
             ? (bool)transport_params != (bool)ssl_params
             : !transport_params && ssl_params);
}

HttpProxyClientSocketWrapper::~HttpProxyClientSocketWrapper() {
  // Make sure no sockets are returned to the lower level socket pools.
  Disconnect();

  net_log_.EndEvent(NetLogEventType::SOCKET_ALIVE);
}

LoadState HttpProxyClientSocketWrapper::GetConnectLoadState() const {
  switch (next_state_) {
    case STATE_TCP_CONNECT:
    case STATE_TCP_CONNECT_COMPLETE:
    case STATE_SSL_CONNECT:
    case STATE_SSL_CONNECT_COMPLETE:
      return nested_connect_job_->GetLoadState();
    case STATE_HTTP_PROXY_CONNECT:
    case STATE_HTTP_PROXY_CONNECT_COMPLETE:
    case STATE_SPDY_PROXY_CREATE_STREAM:
    case STATE_SPDY_PROXY_CREATE_STREAM_COMPLETE:
    case STATE_QUIC_PROXY_CREATE_SESSION:
    case STATE_QUIC_PROXY_CREATE_STREAM:
    case STATE_QUIC_PROXY_CREATE_STREAM_COMPLETE:
    case STATE_RESTART_WITH_AUTH:
    case STATE_RESTART_WITH_AUTH_COMPLETE:
      return LOAD_STATE_ESTABLISHING_PROXY_TUNNEL;
    case STATE_BEGIN_CONNECT:
    case STATE_NONE:
      // May be possible for this method to be called after an error, shouldn't
      // be called after a successful connect.
      break;
  }
  return LOAD_STATE_IDLE;
}

std::unique_ptr<HttpResponseInfo>
HttpProxyClientSocketWrapper::GetAdditionalErrorState() {
  return std::move(error_response_info_);
}

void HttpProxyClientSocketWrapper::SetPriority(RequestPriority priority) {
  priority_ = priority;

  if (nested_connect_job_)
    nested_connect_job_->ChangePriority(priority);

  if (spdy_stream_request_)
    spdy_stream_request_->SetPriority(priority);

  if (quic_stream_request_)
    quic_stream_request_->SetPriority(priority);

  if (transport_socket_)
    transport_socket_->SetStreamPriority(priority);
}

const HttpResponseInfo* HttpProxyClientSocketWrapper::GetConnectResponseInfo()
    const {
  if (transport_socket_)
    return transport_socket_->GetConnectResponseInfo();
  return nullptr;
}

int HttpProxyClientSocketWrapper::RestartWithAuth(
    CompletionOnceCallback callback) {
  // TODO(mmenke): Remove this method, once this class is merged with
  // HttpProxyConnectJob.
  NOTREACHED();
  return ERR_UNEXPECTED;
}

const scoped_refptr<HttpAuthController>&
HttpProxyClientSocketWrapper::GetAuthController() const {
  return http_auth_controller_;
}

bool HttpProxyClientSocketWrapper::IsUsingSpdy() const {
  if (transport_socket_)
    return transport_socket_->IsUsingSpdy();
  return false;
}

NextProto HttpProxyClientSocketWrapper::GetProxyNegotiatedProtocol() const {
  if (transport_socket_)
    return transport_socket_->GetProxyNegotiatedProtocol();
  return kProtoUnknown;
}

int HttpProxyClientSocketWrapper::Connect(CompletionOnceCallback callback) {
  DCHECK(!callback.is_null());
  DCHECK(connect_callback_.is_null());

  // If connecting or previously connected and not disconnected, return OK, to
  // match TCPClientSocket's behavior.
  if (next_state_ != STATE_NONE || transport_socket_)
    return OK;

  next_state_ = STATE_BEGIN_CONNECT;
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    connect_callback_ = std::move(callback);
  } else {
    connect_timer_.Stop();
  }

  return rv;
}

void HttpProxyClientSocketWrapper::Disconnect() {
  connect_callback_.Reset();
  connect_timer_.Stop();
  next_state_ = STATE_NONE;
  spdy_stream_request_.reset();
  quic_stream_request_.reset();
  nested_connect_job_.reset();
  transport_socket_.reset();
}

bool HttpProxyClientSocketWrapper::IsConnected() const {
  if (transport_socket_)
    return transport_socket_->IsConnected();
  // Don't return true if still connecting.  Shouldn't really matter, either
  // way.
  return false;
}

bool HttpProxyClientSocketWrapper::IsConnectedAndIdle() const {
  if (transport_socket_)
    return transport_socket_->IsConnectedAndIdle();
  return false;
}

const NetLogWithSource& HttpProxyClientSocketWrapper::NetLog() const {
  return net_log_;
}

bool HttpProxyClientSocketWrapper::WasEverUsed() const {
  // TODO(mmenke):  This is a little weird.  Figure out if something else should
  // be done.
  if (transport_socket_)
    return transport_socket_->WasEverUsed();
  return false;
}

bool HttpProxyClientSocketWrapper::WasAlpnNegotiated() const {
  if (transport_socket_)
    return transport_socket_->WasAlpnNegotiated();
  return false;
}

NextProto HttpProxyClientSocketWrapper::GetNegotiatedProtocol() const {
  if (transport_socket_)
    return transport_socket_->GetNegotiatedProtocol();
  return kProtoUnknown;
}

bool HttpProxyClientSocketWrapper::GetSSLInfo(SSLInfo* ssl_info) {
  if (transport_socket_)
    return transport_socket_->GetSSLInfo(ssl_info);
  return false;
}

void HttpProxyClientSocketWrapper::GetConnectionAttempts(
    ConnectionAttempts* out) const {
  // TODO(mmenke): Not clear how reconnecting for auth fits into things.
  if (transport_socket_) {
    transport_socket_->GetConnectionAttempts(out);
  } else {
    out->clear();
  }
}

void HttpProxyClientSocketWrapper::ClearConnectionAttempts() {
  if (transport_socket_)
    transport_socket_->ClearConnectionAttempts();
}

void HttpProxyClientSocketWrapper::AddConnectionAttempts(
    const ConnectionAttempts& attempts) {
  if (transport_socket_)
    transport_socket_->AddConnectionAttempts(attempts);
}

int64_t HttpProxyClientSocketWrapper::GetTotalReceivedBytes() const {
  return transport_socket_->GetTotalReceivedBytes();
}

void HttpProxyClientSocketWrapper::ApplySocketTag(const SocketTag& tag) {
  // Applying a socket tag to an HttpProxyClientSocketWrapper is done by simply
  // applying the socket tag to the underlying socket.

  // In the case of a connection to the proxy using HTTP/2 or HTTP/3 where the
  // underlying socket may multiplex multiple streams, applying this request's
  // socket tag to the multiplexed session would incorrectly apply the socket
  // tag to all mutliplexed streams. In reality this would hit the CHECK(false)
  // in QuicProxyClientSocket::ApplySocketTag() or
  // SpdyProxyClientSocket::ApplySocketTag(). Fortunately socket tagging is only
  // supported on Android without the data reduction proxy, so only simple HTTP
  // proxies are supported, so proxies won't be using HTTP/2 or HTTP/3. Detect
  // this case (|ssl_params_| must be set for HTTP/2 and HTTP/3 proxies) and
  // enforce that a specific (non-default) tag isn't being applied.
  if (ssl_params_ ||
      // Android also doesn't support proxy auth, so RestartWithAuth() should't
      // be called so |transport_socket_| shouldn't be cleared. If
      // |transport_socket_| is cleared, enforce that a specific (non-default)
      // tag isn't being applied.
      !transport_socket_) {
    CHECK(tag == SocketTag());
  } else {
    transport_socket_->ApplySocketTag(tag);
  }
}

int HttpProxyClientSocketWrapper::Read(IOBuffer* buf,
                                       int buf_len,
                                       CompletionOnceCallback callback) {
  if (transport_socket_)
    return transport_socket_->Read(buf, buf_len, std::move(callback));
  return ERR_SOCKET_NOT_CONNECTED;
}

int HttpProxyClientSocketWrapper::ReadIfReady(IOBuffer* buf,
                                              int buf_len,
                                              CompletionOnceCallback callback) {
  if (transport_socket_)
    return transport_socket_->ReadIfReady(buf, buf_len, std::move(callback));
  return ERR_SOCKET_NOT_CONNECTED;
}

int HttpProxyClientSocketWrapper::CancelReadIfReady() {
  if (transport_socket_)
    return transport_socket_->CancelReadIfReady();
  return OK;
}

int HttpProxyClientSocketWrapper::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  if (transport_socket_) {
    return transport_socket_->Write(buf, buf_len, std::move(callback),
                                    traffic_annotation);
  }
  return ERR_SOCKET_NOT_CONNECTED;
}

int HttpProxyClientSocketWrapper::SetReceiveBufferSize(int32_t size) {
  // TODO(mmenke):  Should this persist across reconnects?  Seems a little
  //     weird, and not done for normal reconnects.
  if (transport_socket_)
    return transport_socket_->SetReceiveBufferSize(size);
  return ERR_SOCKET_NOT_CONNECTED;
}

int HttpProxyClientSocketWrapper::SetSendBufferSize(int32_t size) {
  if (transport_socket_)
    return transport_socket_->SetSendBufferSize(size);
  return ERR_SOCKET_NOT_CONNECTED;
}

int HttpProxyClientSocketWrapper::GetPeerAddress(IPEndPoint* address) const {
  if (transport_socket_)
    return transport_socket_->GetPeerAddress(address);
  return ERR_SOCKET_NOT_CONNECTED;
}

int HttpProxyClientSocketWrapper::GetLocalAddress(IPEndPoint* address) const {
  if (transport_socket_)
    return transport_socket_->GetLocalAddress(address);
  return ERR_SOCKET_NOT_CONNECTED;
}

void HttpProxyClientSocketWrapper::OnConnectJobComplete(int result,
                                                        ConnectJob* job) {
  DCHECK_EQ(nested_connect_job_.get(), job);
  DCHECK(next_state_ == STATE_TCP_CONNECT_COMPLETE ||
         next_state_ == STATE_SSL_CONNECT_COMPLETE);
  OnIOComplete(result);
}

bool HttpProxyClientSocketWrapper::HasEstablishedConnection() {
  if (has_established_connection_)
    return true;

  // It's possible the nested connect job has established a connection, but
  // hasn't completed yet (For example, an SSLConnectJob may be negotiating
  // SSL).
  if (nested_connect_job_) {
    has_established_connection_ =
        nested_connect_job_->HasEstablishedConnection();
  }
  return has_established_connection_;
}

void HttpProxyClientSocketWrapper::OnNeedsProxyAuth(
    const HttpResponseInfo& response,
    HttpAuthController* auth_controller,
    base::OnceClosure restart_with_auth_callback,
    ConnectJob* job) {
  // This class can't sit on top of another proxy socket class.
  NOTREACHED();
}

ProxyServer::Scheme HttpProxyClientSocketWrapper::GetProxyServerScheme() const {
  if (quic_version_ != quic::QUIC_VERSION_UNSUPPORTED)
    return ProxyServer::SCHEME_QUIC;

  if (transport_params_)
    return ProxyServer::SCHEME_HTTP;

  return ProxyServer::SCHEME_HTTPS;
}

void HttpProxyClientSocketWrapper::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    connect_timer_.Stop();
    // May delete |this|.
    std::move(connect_callback_).Run(rv);
  }
}

void HttpProxyClientSocketWrapper::RestartWithAuthCredentials() {
  DCHECK(!connect_callback_.is_null());
  DCHECK(transport_socket_);
  DCHECK_EQ(STATE_NONE, next_state_);

  // Always do this asynchronously, to avoid re-entrancy.
  next_state_ = STATE_RESTART_WITH_AUTH;
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&HttpProxyClientSocketWrapper::OnIOComplete,
                                weak_ptr_factory_.GetWeakPtr(), net::OK));
}

int HttpProxyClientSocketWrapper::DoLoop(int result) {
  DCHECK_NE(next_state_, STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_BEGIN_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoBeginConnect();
        break;
      case STATE_TCP_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoTransportConnect();
        break;
      case STATE_TCP_CONNECT_COMPLETE:
        rv = DoTransportConnectComplete(rv);
        break;
      case STATE_SSL_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoSSLConnect();
        break;
      case STATE_SSL_CONNECT_COMPLETE:
        rv = DoSSLConnectComplete(rv);
        break;
      case STATE_HTTP_PROXY_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoHttpProxyConnect();
        break;
      case STATE_HTTP_PROXY_CONNECT_COMPLETE:
        rv = DoHttpProxyConnectComplete(rv);
        break;
      case STATE_SPDY_PROXY_CREATE_STREAM:
        DCHECK_EQ(OK, rv);
        rv = DoSpdyProxyCreateStream();
        break;
      case STATE_SPDY_PROXY_CREATE_STREAM_COMPLETE:
        rv = DoSpdyProxyCreateStreamComplete(rv);
        break;
      case STATE_QUIC_PROXY_CREATE_SESSION:
        DCHECK_EQ(OK, rv);
        rv = DoQuicProxyCreateSession();
        break;
      case STATE_QUIC_PROXY_CREATE_STREAM:
        rv = DoQuicProxyCreateStream(rv);
        break;
      case STATE_QUIC_PROXY_CREATE_STREAM_COMPLETE:
        rv = DoQuicProxyCreateStreamComplete(rv);
        break;
      case STATE_RESTART_WITH_AUTH:
        DCHECK_EQ(OK, rv);
        rv = DoRestartWithAuth();
        break;
      case STATE_RESTART_WITH_AUTH_COMPLETE:
        rv = DoRestartWithAuthComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);

  return rv;
}

int HttpProxyClientSocketWrapper::DoBeginConnect() {
  connect_start_time_ = base::TimeTicks::Now();
  SetConnectTimer(connect_timeout_duration_);
  switch (GetProxyServerScheme()) {
    case ProxyServer::SCHEME_QUIC:
      next_state_ = STATE_QUIC_PROXY_CREATE_SESSION;
      // QUIC connections are always considered to have been established.
      // |has_established_connection_| is only used to start retries if a
      // connection hasn't been established yet, and QUIC has its own connection
      // establishment logic.
      has_established_connection_ = true;
      break;
    case ProxyServer::SCHEME_HTTP:
      next_state_ = STATE_TCP_CONNECT;
      break;
    case ProxyServer::SCHEME_HTTPS:
      next_state_ = STATE_SSL_CONNECT;
      break;
    default:
      NOTREACHED();
  }
  return OK;
}

int HttpProxyClientSocketWrapper::DoTransportConnect() {
  next_state_ = STATE_TCP_CONNECT_COMPLETE;
  nested_connect_job_ = TransportConnectJob::CreateTransportConnectJob(
      transport_params_, priority_, common_connect_job_params_, this,
      &net_log_);
  return nested_connect_job_->Connect();
}

int HttpProxyClientSocketWrapper::DoTransportConnectComplete(int result) {
  if (result != OK) {
    UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Insecure.Error",
                               base::TimeTicks::Now() - connect_start_time_);
    // This is a special error code meaning to reuse an existing SPDY session
    // rather than use a fresh socket. Overriding it with a proxy error message
    // would cause the request to fail, instead of switching to using the SPDY
    // session.
    if (result == ERR_SPDY_SESSION_ALREADY_EXISTS)
      return result;
    return ERR_PROXY_CONNECTION_FAILED;
  }

  has_established_connection_ = true;

  // Reset the timer to just the length of time allowed for HttpProxy handshake
  // so that a fast TCP connection plus a slow HttpProxy failure doesn't take
  // longer to timeout than it should.
  SetConnectTimer(proxy_negotiation_timeout_duration_);

  next_state_ = STATE_HTTP_PROXY_CONNECT;
  return result;
}

int HttpProxyClientSocketWrapper::DoSSLConnect() {
  DCHECK(ssl_params_);
  if (tunnel_) {
    SpdySessionKey key(ssl_params_->GetDirectConnectionParams()->destination(),
                       ProxyServer::Direct(), PRIVACY_MODE_DISABLED,
                       SpdySessionKey::IsProxySession::kTrue,
                       common_connect_job_params_.socket_tag);
    if (spdy_session_pool_->FindAvailableSession(
            key, /* enable_ip_based_pooling = */ true,
            /* is_websocket = */ false, net_log_)) {
      using_spdy_ = true;
      next_state_ = STATE_SPDY_PROXY_CREATE_STREAM;
      return OK;
    }
  }
  next_state_ = STATE_SSL_CONNECT_COMPLETE;
  nested_connect_job_ = std::make_unique<SSLConnectJob>(
      priority_, common_connect_job_params_, ssl_params_, this, &net_log_);
  return nested_connect_job_->Connect();
}

int HttpProxyClientSocketWrapper::DoSSLConnectComplete(int result) {
  if (result == ERR_SSL_CLIENT_AUTH_CERT_NEEDED) {
    // Not really used to hold a socket.
    // TODO(mmenke): Implement a better API to get this information.
    ClientSocketHandle client_socket_handle;
    nested_connect_job_->GetAdditionalErrorState(&client_socket_handle);

    DCHECK(client_socket_handle.ssl_error_response_info().cert_request_info);
    UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Secure.Error",
                               base::TimeTicks::Now() - connect_start_time_);
    error_response_info_ = std::make_unique<HttpResponseInfo>(
        client_socket_handle.ssl_error_response_info());
    error_response_info_->cert_request_info->is_proxy = true;
    return result;
  }

  if (IsCertificateError(result)) {
    UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Secure.Error",
                               base::TimeTicks::Now() - connect_start_time_);
    // TODO(rch): allow the user to deal with proxy cert errors in the
    // same way as server cert errors.
    return ERR_PROXY_CERTIFICATE_INVALID;
  }
  // A SPDY session to the proxy completed prior to resolving the proxy
  // hostname. Surface this error, and allow the delegate to retry.
  // See crbug.com/334413.
  if (result == ERR_SPDY_SESSION_ALREADY_EXISTS) {
    DCHECK(!nested_connect_job_->socket());
    return ERR_SPDY_SESSION_ALREADY_EXISTS;
  }
  if (result < 0) {
    UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Secure.Error",
                               base::TimeTicks::Now() - connect_start_time_);
    return ERR_PROXY_CONNECTION_FAILED;
  }

  has_established_connection_ = true;

  negotiated_protocol_ = nested_connect_job_->socket()->GetNegotiatedProtocol();
  using_spdy_ = negotiated_protocol_ == kProtoHTTP2;

  // Reset the timer to just the length of time allowed for HttpProxy handshake
  // so that a fast SSL connection plus a slow HttpProxy failure doesn't take
  // longer to timeout than it should.
  SetConnectTimer(proxy_negotiation_timeout_duration_);

  // TODO(rch): If we ever decide to implement a "trusted" SPDY proxy
  // (one that we speak SPDY over SSL to, but to which we send HTTPS
  // request directly instead of through CONNECT tunnels, then we
  // need to add a predicate to this if statement so we fall through
  // to the else case. (HttpProxyClientSocket currently acts as
  // a "trusted" SPDY proxy).
  if (using_spdy_ && tunnel_) {
    next_state_ = STATE_SPDY_PROXY_CREATE_STREAM;
  } else {
    next_state_ = STATE_HTTP_PROXY_CONNECT;
  }
  return result;
}

int HttpProxyClientSocketWrapper::DoHttpProxyConnect() {
  next_state_ = STATE_HTTP_PROXY_CONNECT_COMPLETE;

  if (transport_params_) {
    UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Insecure.Success",
                               base::TimeTicks::Now() - connect_start_time_);
  } else {
    UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Secure.Success",
                               base::TimeTicks::Now() - connect_start_time_);
  }

  // Add a HttpProxy connection on top of the tcp socket.
  transport_socket_ =
      common_connect_job_params_.client_socket_factory->CreateProxyClientSocket(
          nested_connect_job_->PassSocket(), user_agent_, endpoint_,
          ProxyServer(GetProxyServerScheme(), GetDestination()),
          http_auth_controller_.get(), tunnel_, using_spdy_,
          negotiated_protocol_, common_connect_job_params_.proxy_delegate,
          ssl_params_.get() != nullptr, traffic_annotation_);
  nested_connect_job_.reset();
  return transport_socket_->Connect(base::Bind(
      &HttpProxyClientSocketWrapper::OnIOComplete, base::Unretained(this)));
}

int HttpProxyClientSocketWrapper::DoHttpProxyConnectComplete(int result) {
  // Always inform caller of auth requests asynchronously.
  if (result == ERR_PROXY_AUTH_REQUESTED) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&HttpProxyClientSocketWrapper::OnAuthChallenge,
                       weak_ptr_factory_.GetWeakPtr()));
    return ERR_IO_PENDING;
  }

  if (result == ERR_HTTP_1_1_REQUIRED)
    return ERR_PROXY_HTTP_1_1_REQUIRED;

  return result;
}

int HttpProxyClientSocketWrapper::DoSpdyProxyCreateStream() {
  DCHECK(using_spdy_);
  DCHECK(tunnel_);
  DCHECK(ssl_params_);
  SpdySessionKey key(ssl_params_->GetDirectConnectionParams()->destination(),
                     ProxyServer::Direct(), PRIVACY_MODE_DISABLED,
                     SpdySessionKey::IsProxySession::kTrue,
                     common_connect_job_params_.socket_tag);
  base::WeakPtr<SpdySession> spdy_session =
      spdy_session_pool_->FindAvailableSession(
          key, /* enable_ip_based_pooling = */ true,
          /* is_websocket = */ false, net_log_);
  // It's possible that a session to the proxy has recently been created
  if (spdy_session) {
    nested_connect_job_.reset();
  } else {
    // Create a session direct to the proxy itself
    spdy_session = spdy_session_pool_->CreateAvailableSessionFromSocket(
        key, is_trusted_proxy_, nested_connect_job_->PassSocket(),
        nested_connect_job_->connect_timing(), net_log_);
    DCHECK(spdy_session);
    nested_connect_job_.reset();
  }

  next_state_ = STATE_SPDY_PROXY_CREATE_STREAM_COMPLETE;
  spdy_stream_request_ = std::make_unique<SpdyStreamRequest>();
  return spdy_stream_request_->StartRequest(
      SPDY_BIDIRECTIONAL_STREAM, spdy_session,
      GURL("https://" + endpoint_.ToString()), priority_,
      common_connect_job_params_.socket_tag, spdy_session->net_log(),
      base::Bind(&HttpProxyClientSocketWrapper::OnIOComplete,
                 base::Unretained(this)),
      traffic_annotation_);
}

int HttpProxyClientSocketWrapper::DoSpdyProxyCreateStreamComplete(int result) {
  if (result < 0) {
    spdy_stream_request_.reset();
    return result;
  }

  next_state_ = STATE_HTTP_PROXY_CONNECT_COMPLETE;
  base::WeakPtr<SpdyStream> stream = spdy_stream_request_->ReleaseStream();
  spdy_stream_request_.reset();
  DCHECK(stream.get());
  // |transport_socket_| will set itself as |stream|'s delegate.
  transport_socket_.reset(new SpdyProxyClientSocket(
      stream, user_agent_, endpoint_, net_log_, http_auth_controller_.get()));
  return transport_socket_->Connect(base::Bind(
      &HttpProxyClientSocketWrapper::OnIOComplete, base::Unretained(this)));
}

int HttpProxyClientSocketWrapper::DoQuicProxyCreateSession() {
  DCHECK(ssl_params_);
  DCHECK(tunnel_);
  next_state_ = STATE_QUIC_PROXY_CREATE_STREAM;
  const HostPortPair& proxy_server =
      ssl_params_->GetDirectConnectionParams()->destination();
  quic_stream_request_ =
      std::make_unique<QuicStreamRequest>(quic_stream_factory_);
  return quic_stream_request_->Request(
      proxy_server, quic_version_, ssl_params_->privacy_mode(), priority_,
      common_connect_job_params_.socket_tag,
      ssl_params_->ssl_config().GetCertVerifyFlags(),
      GURL("https://" + proxy_server.ToString()), net_log_,
      &quic_net_error_details_,
      /*failed_on_default_network_callback=*/CompletionOnceCallback(),
      base::Bind(&HttpProxyClientSocketWrapper::OnIOComplete,
                 base::Unretained(this)));
}

int HttpProxyClientSocketWrapper::DoQuicProxyCreateStream(int result) {
  if (result < 0) {
    quic_stream_request_.reset();
    return result;
  }

  next_state_ = STATE_QUIC_PROXY_CREATE_STREAM_COMPLETE;
  quic_session_ = quic_stream_request_->ReleaseSessionHandle();
  quic_stream_request_.reset();

  return quic_session_->RequestStream(
      false,
      base::Bind(&HttpProxyClientSocketWrapper::OnIOComplete,
                 base::Unretained(this)),
      traffic_annotation_);
}

int HttpProxyClientSocketWrapper::DoQuicProxyCreateStreamComplete(int result) {
  if (result < 0)
    return result;

  next_state_ = STATE_HTTP_PROXY_CONNECT_COMPLETE;
  std::unique_ptr<QuicChromiumClientStream::Handle> quic_stream =
      quic_session_->ReleaseStream();

  spdy::SpdyPriority spdy_priority =
      ConvertRequestPriorityToQuicPriority(priority_);
  quic_stream->SetPriority(spdy_priority);

  transport_socket_.reset(new QuicProxyClientSocket(
      std::move(quic_stream), std::move(quic_session_), user_agent_, endpoint_,
      net_log_, http_auth_controller_.get()));
  return transport_socket_->Connect(base::Bind(
      &HttpProxyClientSocketWrapper::OnIOComplete, base::Unretained(this)));
}

int HttpProxyClientSocketWrapper::DoRestartWithAuth() {
  DCHECK(transport_socket_);

  next_state_ = STATE_RESTART_WITH_AUTH_COMPLETE;
  return transport_socket_->RestartWithAuth(base::BindOnce(
      &HttpProxyClientSocketWrapper::OnIOComplete, base::Unretained(this)));
}

int HttpProxyClientSocketWrapper::DoRestartWithAuthComplete(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);

  if (result == OK && !transport_socket_->IsConnected())
    result = ERR_UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH;

  // If the connection could not be reused to attempt to send proxy auth
  // credentials, try reconnecting. Do not reset the HttpAuthController in this
  // case; the server may, for instance, send "Proxy-Connection: close" and
  // expect that each leg of the authentication progress on separate
  // connections.
  bool reconnect = result == ERR_UNABLE_TO_REUSE_CONNECTION_FOR_PROXY_AUTH;

  // If auth credentials were sent but the connection was closed, the server may
  // have timed out while the user was selecting credentials. Retry once.
  if (!has_restarted_ &&
      (result == ERR_CONNECTION_CLOSED || result == ERR_CONNECTION_RESET ||
       result == ERR_CONNECTION_ABORTED ||
       result == ERR_SOCKET_NOT_CONNECTED)) {
    reconnect = true;
    has_restarted_ = true;

    // Release any auth state bound to the connection. The new connection will
    // start the current scheme and identity from scratch.
    if (http_auth_controller_)
      http_auth_controller_->OnConnectionClosed();
  }

  if (reconnect) {
    // Attempt to create a new one.
    transport_socket_.reset();
    using_spdy_ = false;
    negotiated_protocol_ = NextProto();
    next_state_ = STATE_BEGIN_CONNECT;
    return OK;
  }

  // If not reconnecting, treat the result as the result of establishing a
  // tunnel through the proxy. This important in the case another auth challenge
  // is seen.
  next_state_ = STATE_HTTP_PROXY_CONNECT_COMPLETE;
  return result;
}

void HttpProxyClientSocketWrapper::SetConnectTimer(base::TimeDelta delay) {
  connect_timer_.Stop();
  connect_timer_.Start(FROM_HERE, delay, this,
                       &HttpProxyClientSocketWrapper::ConnectTimeout);
}

void HttpProxyClientSocketWrapper::ConnectTimeout() {
  // Timer shouldn't be running if next_state_ is STATE_NONE.
  DCHECK_NE(STATE_NONE, next_state_);
  DCHECK(!connect_callback_.is_null());

  if (next_state_ == STATE_TCP_CONNECT_COMPLETE ||
      next_state_ == STATE_SSL_CONNECT_COMPLETE) {
    if (transport_params_) {
      UMA_HISTOGRAM_MEDIUM_TIMES(
          "Net.HttpProxy.ConnectLatency.Insecure.TimedOut",
          base::TimeTicks::Now() - connect_start_time_);
    } else {
      UMA_HISTOGRAM_MEDIUM_TIMES("Net.HttpProxy.ConnectLatency.Secure.TimedOut",
                                 base::TimeTicks::Now() - connect_start_time_);
    }
  }

  CompletionOnceCallback callback = std::move(connect_callback_);
  Disconnect();
  std::move(callback).Run(ERR_CONNECTION_TIMED_OUT);
}

void HttpProxyClientSocketWrapper::OnAuthChallenge() {
  connect_timer_.Stop();
  on_proxy_auth_callback_.Run(
      *transport_socket_->GetConnectResponseInfo(),
      transport_socket_->GetAuthController().get(),
      base::BindOnce(&HttpProxyClientSocketWrapper::RestartWithAuthCredentials,
                     weak_ptr_factory_.GetWeakPtr()));
}

const HostPortPair& HttpProxyClientSocketWrapper::GetDestination() {
  if (transport_params_) {
    return transport_params_->destination();
  } else {
    return ssl_params_->GetDirectConnectionParams()->destination();
  }
}

}  // namespace net
