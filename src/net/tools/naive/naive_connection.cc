// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_connection.h"

#include <cstring>
#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/stream_socket.h"
#include "net/spdy/spdy_session.h"
#include "net/tools/naive/http_proxy_socket.h"
#include "net/tools/naive/socks5_server_socket.h"

#if defined(OS_LINUX)
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
constexpr int kFirstPaddings = 4;
constexpr int kPaddingHeaderSize = 3;
constexpr int kMaxPaddingSize = 255;
}  // namespace

NaiveConnection::NaiveConnection(
    unsigned int id,
    Protocol protocol,
    Direction pad_direction,
    const ProxyInfo& proxy_info,
    const SSLConfig& server_ssl_config,
    const SSLConfig& proxy_ssl_config,
    HttpNetworkSession* session,
    const NetLogWithSource& net_log,
    std::unique_ptr<StreamSocket> accepted_socket,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : id_(id),
      protocol_(protocol),
      pad_direction_(pad_direction),
      proxy_info_(proxy_info),
      server_ssl_config_(server_ssl_config),
      proxy_ssl_config_(proxy_ssl_config),
      session_(session),
      net_log_(net_log),
      next_state_(STATE_NONE),
      client_socket_(std::move(accepted_socket)),
      server_socket_handle_(std::make_unique<ClientSocketHandle>()),
      sockets_{client_socket_.get(), nullptr},
      errors_{OK, OK},
      write_pending_{false, false},
      early_pull_pending_(false),
      can_push_to_server_(false),
      early_pull_result_(ERR_IO_PENDING),
      num_paddings_{0, 0},
      read_padding_state_(STATE_READ_PAYLOAD_LENGTH_1),
      full_duplex_(false),
      time_func_(&base::TimeTicks::Now),
      traffic_annotation_(traffic_annotation),
      weak_ptr_factory_(this) {
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
  if (protocol_ == kSocks5) {
    const auto* socket =
        static_cast<const Socks5ServerSocket*>(client_socket_.get());
    origin = socket->request_endpoint();
  } else if (protocol_ == kHttp) {
    const auto* socket =
        static_cast<const HttpProxySocket*>(client_socket_.get());
    origin = socket->request_endpoint();
  } else if (protocol_ == kRedir) {
#if defined(OS_LINUX)
    const auto* socket =
        static_cast<const TCPClientSocket*>(client_socket_.get());
    int sd = socket->SocketDescriptorForTesting();
    SockaddrStorage dst;
    int rv;
    rv = getsockopt(sd, SOL_IP, SO_ORIGINAL_DST, dst.addr, &dst.addr_len);
    if (rv == 0) {
      IPEndPoint ipe;
      if (ipe.FromSockAddr(dst.addr, dst.addr_len)) {
        origin = HostPortPair::FromIPEndPoint(ipe);
      }
    }
#endif
  }

  if (origin.IsEmpty()) {
    LOG(ERROR) << "Connection " << id_ << " to invalid origin";
    return ERR_ADDRESS_INVALID;
  }

  LOG(INFO) << "Connection " << id_ << " to " << origin.ToString();

  // Ignores socket limit set by socket pool for this type of socket.
  return InitSocketHandleForRawConnect2(
      origin, session_, LOAD_IGNORE_LIMITS, MAXIMUM_PRIORITY, proxy_info_,
      server_ssl_config_, proxy_ssl_config_, PRIVACY_MODE_DISABLED, net_log_,
      server_socket_handle_.get(), io_callback_);
}

int NaiveConnection::DoConnectServerComplete(int result) {
  if (result < 0)
    return result;

  DCHECK(server_socket_handle_->socket());
  sockets_[kServer] = server_socket_handle_->socket();

  full_duplex_ = true;
  next_state_ = STATE_NONE;
  return OK;
}

int NaiveConnection::Run(CompletionOnceCallback callback) {
  DCHECK(sockets_[kClient]);
  DCHECK(sockets_[kServer]);
  DCHECK_EQ(next_state_, STATE_NONE);
  DCHECK(!connect_callback_);

  if (errors_[kClient] != OK)
    return errors_[kClient];
  if (errors_[kServer] != OK)
    return errors_[kServer];

  run_callback_ = std::move(callback);

  bytes_passed_without_yielding_[kClient] = 0;
  bytes_passed_without_yielding_[kServer] = 0;

  yield_after_time_[kClient] =
      time_func_() +
      base::TimeDelta::FromMilliseconds(kYieldAfterDurationMilliseconds);
  yield_after_time_[kServer] = yield_after_time_[kClient];

  can_push_to_server_ = true;
  if (!early_pull_pending_) {
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
  if (from == pad_direction_ && num_paddings_[from] < kFirstPaddings) {
    auto buffer = base::MakeRefCounted<GrowableIOBuffer>();
    buffer->SetCapacity(kBufferSize);
    buffer->set_offset(kPaddingHeaderSize);
    read_buffers_[from] = buffer;
    read_size = kBufferSize - kPaddingHeaderSize - kMaxPaddingSize;
  } else {
    read_buffers_[from] = base::MakeRefCounted<IOBuffer>(kBufferSize);
  }

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
  int write_size = size;
  int write_offset = 0;
  if (from == pad_direction_ && num_paddings_[from] < kFirstPaddings) {
    // Adds padding.
    ++num_paddings_[from];
    int padding_size = base::RandInt(0, kMaxPaddingSize);
    auto* buffer = static_cast<GrowableIOBuffer*>(read_buffers_[from].get());
    buffer->set_offset(0);
    uint8_t* p = reinterpret_cast<uint8_t*>(buffer->data());
    p[0] = size / 256;
    p[1] = size % 256;
    p[2] = padding_size;
    std::memset(p + kPaddingHeaderSize + size, 0, padding_size);
    write_size = kPaddingHeaderSize + size + padding_size;
  } else if (to == pad_direction_ && num_paddings_[from] < kFirstPaddings) {
    // Removes padding.
    const char* p = read_buffers_[from]->data();
    bool trivial_padding = false;
    if (read_padding_state_ == STATE_READ_PAYLOAD_LENGTH_1 &&
        size >= kPaddingHeaderSize) {
      int payload_size =
          static_cast<uint8_t>(p[0]) * 256 + static_cast<uint8_t>(p[1]);
      int padding_size = static_cast<uint8_t>(p[2]);
      if (size == kPaddingHeaderSize + payload_size + padding_size) {
        write_size = payload_size;
        write_offset = kPaddingHeaderSize;
        ++num_paddings_[from];
        trivial_padding = true;
      }
    }
    if (!trivial_padding) {
      auto unpadded_buffer = base::MakeRefCounted<IOBuffer>(kBufferSize);
      char* unpadded_ptr = unpadded_buffer->data();
      for (int i = 0; i < size;) {
        if (num_paddings_[from] >= kFirstPaddings &&
            read_padding_state_ == STATE_READ_PAYLOAD_LENGTH_1) {
          std::memcpy(unpadded_ptr, p + i, size - i);
          unpadded_ptr += size - i;
          break;
        }
        int copy_size;
        switch (read_padding_state_) {
          case STATE_READ_PAYLOAD_LENGTH_1:
            payload_length_ = static_cast<uint8_t>(p[i]);
            ++i;
            read_padding_state_ = STATE_READ_PAYLOAD_LENGTH_2;
            break;
          case STATE_READ_PAYLOAD_LENGTH_2:
            payload_length_ =
                payload_length_ * 256 + static_cast<uint8_t>(p[i]);
            ++i;
            read_padding_state_ = STATE_READ_PADDING_LENGTH;
            break;
          case STATE_READ_PADDING_LENGTH:
            padding_length_ = static_cast<uint8_t>(p[i]);
            ++i;
            read_padding_state_ = STATE_READ_PAYLOAD;
            break;
          case STATE_READ_PAYLOAD:
            if (payload_length_ <= size - i) {
              copy_size = payload_length_;
              read_padding_state_ = STATE_READ_PADDING;
            } else {
              copy_size = size - i;
            }
            std::memcpy(unpadded_ptr, p + i, copy_size);
            unpadded_ptr += copy_size;
            i += copy_size;
            payload_length_ -= copy_size;
            break;
          case STATE_READ_PADDING:
            if (padding_length_ <= size - i) {
              copy_size = padding_length_;
              read_padding_state_ = STATE_READ_PAYLOAD_LENGTH_1;
              ++num_paddings_[from];
            } else {
              copy_size = size - i;
            }
            i += copy_size;
            padding_length_ -= copy_size;
            break;
        }
      }
      write_size = unpadded_ptr - unpadded_buffer->data();
      read_buffers_[from] = unpadded_buffer;
    }
    if (write_size == 0) {
      OnPushComplete(from, to, OK);
      return;
    }
  }

  write_buffers_[to] = base::MakeRefCounted<DrainableIOBuffer>(
      std::move(read_buffers_[from]), write_offset + write_size);
  if (write_offset) {
    write_buffers_[to]->DidConsume(write_offset);
  }
  write_pending_[to] = true;
  DCHECK(sockets_[to]);
  int rv = sockets_[to]->Write(
      write_buffers_[to].get(), write_size,
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
  return sockets_[side];
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
    early_pull_result_ = result;
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
        time_func_() +
        base::TimeDelta::FromMilliseconds(kYieldAfterDurationMilliseconds);
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindRepeating(&NaiveConnection::Pull,
                            weak_ptr_factory_.GetWeakPtr(), from, to));
  } else {
    Pull(from, to);
  }
}

}  // namespace net
