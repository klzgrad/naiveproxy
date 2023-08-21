// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_connection.h"

#include <cstring>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/base/url_util.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/stream_socket.h"
#include "net/spdy/spdy_session.h"
#include "net/tools/naive/http_proxy_server_socket.h"
#include "net/tools/naive/naive_padding_socket.h"
#include "net/tools/naive/redirect_resolver.h"
#include "net/tools/naive/socks5_server_socket.h"
#include "url/scheme_host_port.h"

#if BUILDFLAG(IS_LINUX)
#include <linux/netfilter_ipv4.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "net/base/ip_endpoint.h"
#include "net/base/sockaddr_storage.h"
#include "net/socket/tcp_client_socket.h"
#endif

namespace net {

namespace {
constexpr int kBufferSize = 64 * 1024;
}  // namespace

NaiveConnection::NaiveConnection(
    unsigned int id,
    ClientProtocol protocol,
    std::unique_ptr<PaddingDetectorDelegate> padding_detector_delegate,
    const ProxyInfo& proxy_info,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    RedirectResolver* resolver,
    HttpNetworkSession* session,
    const NetworkAnonymizationKey& network_anonymization_key,
    const NetLogWithSource& net_log,
    std::unique_ptr<StreamSocket> accepted_socket,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : id_(id),
      protocol_(protocol),
      padding_detector_delegate_(std::move(padding_detector_delegate)),
      proxy_info_(proxy_info),
      server_ssl_config_(server_ssl_config),
      proxy_ssl_config_(proxy_ssl_config),
      resolver_(resolver),
      session_(session),
      network_anonymization_key_(network_anonymization_key),
      net_log_(net_log),
      next_state_(STATE_NONE),
      client_socket_(std::move(accepted_socket)),
      server_socket_handle_(std::make_unique<ClientSocketHandle>()),
      sockets_{nullptr, nullptr},
      errors_{OK, OK},
      write_pending_{false, false},
      early_pull_pending_(false),
      can_push_to_server_(false),
      early_pull_result_(ERR_IO_PENDING),
      full_duplex_(false),
      time_func_(&base::TimeTicks::Now),
      traffic_annotation_(traffic_annotation) {
  io_callback_ = base::BindRepeating(&NaiveConnection::OnIOComplete,
                                     weak_ptr_factory_.GetWeakPtr());
}

NaiveConnection::~NaiveConnection() {
  Disconnect();
}

int NaiveConnection::Connect(CompletionOnceCallback callback) {
  DCHECK(client_socket_);
  DCHECK_EQ(next_state_, STATE_NONE);
  DCHECK(!connect_callback_);

  if (full_duplex_)
    return OK;

  next_state_ = STATE_CONNECT_CLIENT;

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    connect_callback_ = std::move(callback);
  }
  return rv;
}

void NaiveConnection::Disconnect() {
  full_duplex_ = false;
  // Closes server side first because latency is higher.
  if (server_socket_handle_->socket())
    server_socket_handle_->socket()->Disconnect();
  client_socket_->Disconnect();

  next_state_ = STATE_NONE;
  connect_callback_.Reset();
  run_callback_.Reset();
}

void NaiveConnection::DoCallback(int result) {
  DCHECK_NE(result, ERR_IO_PENDING);
  DCHECK(connect_callback_);

  // Since Run() may result in Read being called,
  // clear connect_callback_ up front.
  std::move(connect_callback_).Run(result);
}

void NaiveConnection::OnIOComplete(int result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    DoCallback(rv);
  }
}

int NaiveConnection::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_CONNECT_CLIENT:
        DCHECK_EQ(rv, OK);
        rv = DoConnectClient();
        break;
      case STATE_CONNECT_CLIENT_COMPLETE:
        rv = DoConnectClientComplete(rv);
        break;
      case STATE_CONNECT_SERVER:
        DCHECK_EQ(rv, OK);
        rv = DoConnectServer();
        break;
      case STATE_CONNECT_SERVER_COMPLETE:
        rv = DoConnectServerComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

int NaiveConnection::DoConnectClient() {
  next_state_ = STATE_CONNECT_CLIENT_COMPLETE;

  return client_socket_->Connect(io_callback_);
}

int NaiveConnection::DoConnectClientComplete(int result) {
  if (result < 0)
    return result;

  std::optional<PaddingType> client_padding_type =
      padding_detector_delegate_->GetClientPaddingType();
  CHECK(client_padding_type.has_value());

  sockets_[kClient] = std::make_unique<NaivePaddingSocket>(
      client_socket_.get(), *client_padding_type, kClient);

  // For proxy client sockets, padding support detection is finished after the
  // first server response which means there will be one missed early pull. For
  // proxy server sockets (HttpProxyServerSocket), padding support detection is
  // done during client connect, so there shouldn't be any missed early pull.
  if (!padding_detector_delegate_->GetServerPaddingType().has_value()) {
    early_pull_pending_ = false;
    early_pull_result_ = 0;
    next_state_ = STATE_CONNECT_SERVER;
    return OK;
  }

  early_pull_pending_ = true;
  Pull(kClient, kServer);
  if (early_pull_result_ != ERR_IO_PENDING) {
    // Pull has completed synchronously.
    if (early_pull_result_ <= 0) {
      return early_pull_result_ ? early_pull_result_ : ERR_CONNECTION_CLOSED;
    }
  }

  next_state_ = STATE_CONNECT_SERVER;
  return OK;
}

int NaiveConnection::DoConnectServer() {
  next_state_ = STATE_CONNECT_SERVER_COMPLETE;

  HostPortPair origin;
  if (protocol_ == ClientProtocol::kSocks5) {
    const auto* socket =
        static_cast<const Socks5ServerSocket*>(client_socket_.get());
    origin = socket->request_endpoint();
  } else if (protocol_ == ClientProtocol::kHttp) {
    const auto* socket =
        static_cast<const HttpProxyServerSocket*>(client_socket_.get());
    origin = socket->request_endpoint();
  } else if (protocol_ == ClientProtocol::kRedir) {
#if BUILDFLAG(IS_LINUX)
    const auto* socket =
        static_cast<const TCPClientSocket*>(client_socket_.get());
    IPEndPoint peer_endpoint;
    int rv;
    rv = socket->GetPeerAddress(&peer_endpoint);
    if (rv != OK) {
      LOG(ERROR) << "Connection " << id_
                 << " cannot get peer address: " << ErrorToShortString(rv);
      return rv;
    }
    int sd = socket->SocketDescriptorForTesting();
    SockaddrStorage dst;
    if (peer_endpoint.GetFamily() == ADDRESS_FAMILY_IPV4 ||
        peer_endpoint.address().IsIPv4MappedIPv6()) {
      rv = getsockopt(sd, SOL_IP, SO_ORIGINAL_DST, dst.addr, &dst.addr_len);
    } else {
      rv = getsockopt(sd, SOL_IPV6, SO_ORIGINAL_DST, dst.addr, &dst.addr_len);
    }
    if (rv == 0) {
      IPEndPoint ipe;
      if (ipe.FromSockAddr(dst.addr, dst.addr_len)) {
        const auto& addr = ipe.address();
        auto name = resolver_->FindNameByAddress(addr);
        if (!name.empty()) {
          origin = HostPortPair(name, ipe.port());
        } else if (!resolver_->IsInResolvedRange(addr)) {
          origin = HostPortPair::FromIPEndPoint(ipe);
        } else {
          LOG(ERROR) << "Connection " << id_ << " to unresolved name for "
                     << addr.ToString();
          return ERR_ADDRESS_INVALID;
        }
      }
    } else {
      LOG(ERROR) << "Failed to get original destination address";
      return ERR_ADDRESS_INVALID;
    }
#else
    static_cast<void>(resolver_);
#endif
  }

  url::CanonHostInfo host_info;
  url::SchemeHostPort endpoint(
      "http", CanonicalizeHost(origin.HostForURL(), &host_info), origin.port(),
      url::SchemeHostPort::ALREADY_CANONICALIZED);
  if (!endpoint.IsValid()) {
    LOG(ERROR) << "Connection " << id_ << " to invalid origin "
               << origin.ToString();
    return ERR_ADDRESS_INVALID;
  }

  LOG(INFO) << "Connection " << id_ << " to " << origin.ToString();

  // Ignores socket limit set by socket pool for this type of socket.
  return InitSocketHandleForRawConnect2(
      std::move(endpoint), LOAD_IGNORE_LIMITS, MAXIMUM_PRIORITY, session_,
      proxy_info_, server_ssl_config_, proxy_ssl_config_, PRIVACY_MODE_DISABLED,
      network_anonymization_key_, net_log_, server_socket_handle_.get(),
      io_callback_);
}

int NaiveConnection::DoConnectServerComplete(int result) {
  if (result < 0)
    return result;

  std::optional<PaddingType> server_padding_type =
      padding_detector_delegate_->GetServerPaddingType();
  CHECK(server_padding_type.has_value());

  sockets_[kServer] = std::make_unique<NaivePaddingSocket>(
      server_socket_handle_->socket(), *server_padding_type, kServer);

  full_duplex_ = true;
  next_state_ = STATE_NONE;
  return OK;
}

int NaiveConnection::Run(CompletionOnceCallback callback) {
  DCHECK(sockets_[kServer]);
  DCHECK_EQ(next_state_, STATE_NONE);
  DCHECK(!connect_callback_);

  // The client-side socket may be closed before the server-side
  // socket is connected.
  if (errors_[kClient] != OK || sockets_[kClient] == nullptr)
    return errors_[kClient];
  if (errors_[kServer] != OK)
    return errors_[kServer];

  run_callback_ = std::move(callback);

  bytes_passed_without_yielding_[kClient] = 0;
  bytes_passed_without_yielding_[kServer] = 0;

  yield_after_time_[kClient] =
      time_func_() + base::Milliseconds(kYieldAfterDurationMilliseconds);
  yield_after_time_[kServer] = yield_after_time_[kClient];

  can_push_to_server_ = true;
  // early_pull_result_ == 0 means the early pull was not started because
  // padding support was not yet known.
  if (!early_pull_pending_ && early_pull_result_ == 0) {
    Pull(kClient, kServer);
  } else if (!early_pull_pending_) {
    DCHECK_GT(early_pull_result_, 0);
    Push(kClient, kServer, early_pull_result_);
  }
  Pull(kServer, kClient);

  return ERR_IO_PENDING;
}

void NaiveConnection::Pull(Direction from, Direction to) {
  if (errors_[kClient] < 0 || errors_[kServer] < 0)
    return;

  int read_size = kBufferSize;
  read_buffers_[from] = base::MakeRefCounted<IOBuffer>(kBufferSize);

  DCHECK(sockets_[from]);
  int rv = sockets_[from]->Read(
      read_buffers_[from].get(), read_size,
      base::BindRepeating(&NaiveConnection::OnPullComplete,
                          weak_ptr_factory_.GetWeakPtr(), from, to));

  if (from == kClient && early_pull_pending_)
    early_pull_result_ = rv;

  if (rv != ERR_IO_PENDING)
    OnPullComplete(from, to, rv);
}

void NaiveConnection::Push(Direction from, Direction to, int size) {
  write_buffers_[to] = base::MakeRefCounted<DrainableIOBuffer>(
      std::move(read_buffers_[from]), size);
  write_pending_[to] = true;
  DCHECK(sockets_[to]);
  int rv = sockets_[to]->Write(
      write_buffers_[to].get(), write_buffers_[to]->BytesRemaining(),
      base::BindRepeating(&NaiveConnection::OnPushComplete,
                          weak_ptr_factory_.GetWeakPtr(), from, to),
      traffic_annotation_);

  if (rv != ERR_IO_PENDING)
    OnPushComplete(from, to, rv);
}

void NaiveConnection::Disconnect(Direction side) {
  if (sockets_[side]) {
    sockets_[side]->Disconnect();
    sockets_[side] = nullptr;
    write_pending_[side] = false;
  }
}

bool NaiveConnection::IsConnected(Direction side) {
  return sockets_[side] != nullptr;
}

void NaiveConnection::OnBothDisconnected() {
  if (run_callback_) {
    int error = OK;
    if (errors_[kClient] != ERR_CONNECTION_CLOSED && errors_[kClient] < 0)
      error = errors_[kClient];
    if (errors_[kServer] != ERR_CONNECTION_CLOSED && errors_[kClient] < 0)
      error = errors_[kServer];
    std::move(run_callback_).Run(error);
  }
}

void NaiveConnection::OnPullError(Direction from, Direction to, int error) {
  DCHECK_LT(error, 0);

  errors_[from] = error;
  Disconnect(from);

  if (!write_pending_[to])
    Disconnect(to);

  if (!IsConnected(from) && !IsConnected(to))
    OnBothDisconnected();
}

void NaiveConnection::OnPushError(Direction from, Direction to, int error) {
  DCHECK_LE(error, 0);
  DCHECK(!write_pending_[to]);

  if (error < 0) {
    errors_[to] = error;
    Disconnect(kServer);
    Disconnect(kClient);
  } else if (!IsConnected(from)) {
    Disconnect(to);
  }

  if (!IsConnected(from) && !IsConnected(to))
    OnBothDisconnected();
}

void NaiveConnection::OnPullComplete(Direction from, Direction to, int result) {
  if (from == kClient && early_pull_pending_) {
    early_pull_pending_ = false;
    early_pull_result_ = result ? result : ERR_CONNECTION_CLOSED;
  }

  if (result <= 0) {
    OnPullError(from, to, result ? result : ERR_CONNECTION_CLOSED);
    return;
  }

  if (from == kClient && !can_push_to_server_)
    return;

  Push(from, to, result);
}

void NaiveConnection::OnPushComplete(Direction from, Direction to, int result) {
  if (result >= 0 && write_buffers_[to] != nullptr) {
    bytes_passed_without_yielding_[from] += result;
    write_buffers_[to]->DidConsume(result);
    int size = write_buffers_[to]->BytesRemaining();
    if (size > 0) {
      int rv = sockets_[to]->Write(
          write_buffers_[to].get(), size,
          base::BindRepeating(&NaiveConnection::OnPushComplete,
                              weak_ptr_factory_.GetWeakPtr(), from, to),
          traffic_annotation_);
      if (rv != ERR_IO_PENDING)
        OnPushComplete(from, to, rv);
      return;
    }
  }

  write_pending_[to] = false;
  // Checks for termination even if result is OK.
  OnPushError(from, to, result >= 0 ? OK : result);

  if (bytes_passed_without_yielding_[from] > kYieldAfterBytesRead ||
      time_func_() > yield_after_time_[from]) {
    bytes_passed_without_yielding_[from] = 0;
    yield_after_time_[from] =
        time_func_() + base::Milliseconds(kYieldAfterDurationMilliseconds);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindRepeating(&NaiveConnection::Pull,
                            weak_ptr_factory_.GetWeakPtr(), from, to));
  } else {
    Pull(from, to);
  }
}

}  // namespace net
