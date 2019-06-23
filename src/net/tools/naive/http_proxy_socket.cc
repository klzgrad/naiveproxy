// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/http_proxy_socket.h"

#include <cstring>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/sys_byteorder.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"

namespace net {

namespace {
constexpr int kBufferSize = 64 * 1024;
constexpr size_t kMaxHeaderSize = 64 * 1024;
constexpr char kResponseHeader[] = "HTTP/1.1 200 OK\r\nPadding: ";
constexpr int kResponseHeaderSize = sizeof(kResponseHeader) - 1;
// A plain 200 is 10 bytes. Expected 48 bytes. "Padding" uses up 7 bytes.
constexpr int kMinPaddingSize = 30;
constexpr int kMaxPaddingSize = kMinPaddingSize + 32;
}  // namespace

HttpProxySocket::HttpProxySocket(
    std::unique_ptr<StreamSocket> transport_socket,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : io_callback_(base::BindRepeating(&HttpProxySocket::OnIOComplete,
                                       base::Unretained(this))),
      transport_(std::move(transport_socket)),
      next_state_(STATE_NONE),
      completed_handshake_(false),
      was_ever_used_(false),
      header_write_size_(-1),
      net_log_(transport_->NetLog()),
      traffic_annotation_(traffic_annotation) {}

HttpProxySocket::~HttpProxySocket() {
  Disconnect();
}

const HostPortPair& HttpProxySocket::request_endpoint() const {
  return request_endpoint_;
}

int HttpProxySocket::Connect(CompletionOnceCallback callback) {
  DCHECK(transport_);
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(!user_callback_);

  // If already connected, then just return OK.
  if (completed_handshake_)
    return OK;

  next_state_ = STATE_HEADER_READ;
  buffer_.clear();

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    user_callback_ = std::move(callback);
  }
  return rv;
}

void HttpProxySocket::Disconnect() {
  completed_handshake_ = false;
  transport_->Disconnect();

  // Reset other states to make sure they aren't mistakenly used later.
  // These are the states initialized by Connect().
  next_state_ = STATE_NONE;
  user_callback_.Reset();
}

bool HttpProxySocket::IsConnected() const {
  return completed_handshake_ && transport_->IsConnected();
}

bool HttpProxySocket::IsConnectedAndIdle() const {
  return completed_handshake_ && transport_->IsConnectedAndIdle();
}

const NetLogWithSource& HttpProxySocket::NetLog() const {
  return net_log_;
}

bool HttpProxySocket::WasEverUsed() const {
  return was_ever_used_;
}

bool HttpProxySocket::WasAlpnNegotiated() const {
  if (transport_) {
    return transport_->WasAlpnNegotiated();
  }
  NOTREACHED();
  return false;
}

NextProto HttpProxySocket::GetNegotiatedProtocol() const {
  if (transport_) {
    return transport_->GetNegotiatedProtocol();
  }
  NOTREACHED();
  return kProtoUnknown;
}

bool HttpProxySocket::GetSSLInfo(SSLInfo* ssl_info) {
  if (transport_) {
    return transport_->GetSSLInfo(ssl_info);
  }
  NOTREACHED();
  return false;
}

void HttpProxySocket::GetConnectionAttempts(ConnectionAttempts* out) const {
  out->clear();
}

int64_t HttpProxySocket::GetTotalReceivedBytes() const {
  return transport_->GetTotalReceivedBytes();
}

void HttpProxySocket::ApplySocketTag(const SocketTag& tag) {
  return transport_->ApplySocketTag(tag);
}

// Read is called by the transport layer above to read. This can only be done
// if the HTTP header is complete.
int HttpProxySocket::Read(IOBuffer* buf,
                          int buf_len,
                          CompletionOnceCallback callback) {
  DCHECK(completed_handshake_);
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(!user_callback_);
  DCHECK(callback);

  int rv = transport_->Read(
      buf, buf_len,
      base::BindOnce(&HttpProxySocket::OnReadWriteComplete,
                     base::Unretained(this), std::move(callback)));
  if (rv > 0)
    was_ever_used_ = true;
  return rv;
}

// Write is called by the transport layer. This can only be done if the
// SOCKS handshake is complete.
int HttpProxySocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  DCHECK(completed_handshake_);
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(!user_callback_);
  DCHECK(callback);

  int rv = transport_->Write(
      buf, buf_len,
      base::BindOnce(&HttpProxySocket::OnReadWriteComplete,
                     base::Unretained(this), std::move(callback)),
      traffic_annotation);
  if (rv > 0)
    was_ever_used_ = true;
  return rv;
}

int HttpProxySocket::SetReceiveBufferSize(int32_t size) {
  return transport_->SetReceiveBufferSize(size);
}

int HttpProxySocket::SetSendBufferSize(int32_t size) {
  return transport_->SetSendBufferSize(size);
}

void HttpProxySocket::DoCallback(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(user_callback_);

  // Since Run() may result in Read being called,
  // clear user_callback_ up front.
  std::move(user_callback_).Run(result);
}

void HttpProxySocket::OnIOComplete(int result) {
  DCHECK_NE(STATE_NONE, next_state_);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    DoCallback(rv);
  }
}

void HttpProxySocket::OnReadWriteComplete(CompletionOnceCallback callback,
                                          int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(callback);

  if (result > 0)
    was_ever_used_ = true;
  std::move(callback).Run(result);
}

int HttpProxySocket::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_HEADER_READ:
        DCHECK_EQ(OK, rv);
        rv = DoHeaderRead();
        break;
      case STATE_HEADER_READ_COMPLETE:
        rv = DoHeaderReadComplete(rv);
        break;
      case STATE_HEADER_WRITE:
        DCHECK_EQ(OK, rv);
        rv = DoHeaderWrite();
        break;
      case STATE_HEADER_WRITE_COMPLETE:
        rv = DoHeaderWriteComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

int HttpProxySocket::DoHeaderRead() {
  next_state_ = STATE_HEADER_READ_COMPLETE;

  handshake_buf_ = base::MakeRefCounted<IOBuffer>(kBufferSize);
  return transport_->Read(handshake_buf_.get(), kBufferSize, io_callback_);
}

int HttpProxySocket::DoHeaderReadComplete(int result) {
  if (result < 0)
    return result;

  if (result == 0) {
    return ERR_CONNECTION_CLOSED;
  }

  buffer_.append(handshake_buf_->data(), result);
  if (buffer_.size() > kMaxHeaderSize) {
    return ERR_MSG_TOO_BIG;
  }

  auto header_end = buffer_.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    next_state_ = STATE_HEADER_READ;
    return OK;
  }
  if (header_end + 4 != buffer_.size()) {
    return ERR_INVALID_ARGUMENT;
  }

  // HttpProxyClientSocket uses CONNECT for all endpoints.
  auto first_line_end = buffer_.find("\r\n");
  auto first_space = buffer_.find(' ');
  if (first_space == std::string::npos || first_space + 1 >= first_line_end) {
    return ERR_INVALID_ARGUMENT;
  }
  if (buffer_.compare(0, first_space, "CONNECT") != 0) {
    return ERR_INVALID_ARGUMENT;
  }
  auto second_space = buffer_.find(' ', first_space + 1);
  if (second_space == std::string::npos || second_space >= first_line_end) {
    return ERR_INVALID_ARGUMENT;
  }
  request_endpoint_ = HostPortPair::FromString(
      buffer_.substr(first_space + 1, second_space - (first_space + 1)));

  next_state_ = STATE_HEADER_WRITE;
  return OK;
}

int HttpProxySocket::DoHeaderWrite() {
  next_state_ = STATE_HEADER_WRITE_COMPLETE;

  // Adds padding.
  int padding_size = base::RandInt(kMinPaddingSize, kMaxPaddingSize);
  header_write_size_ = kResponseHeaderSize + padding_size + 4;
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(header_write_size_);
  char* p = handshake_buf_->data();
  std::memcpy(p, kResponseHeader, kResponseHeaderSize);
  std::memset(p + kResponseHeaderSize, '.', padding_size);
  std::memcpy(p + kResponseHeaderSize + padding_size, "\r\n\r\n", 4);

  return transport_->Write(handshake_buf_.get(), header_write_size_,
                           io_callback_, traffic_annotation_);
}

int HttpProxySocket::DoHeaderWriteComplete(int result) {
  if (result < 0)
    return result;

  if (result != header_write_size_) {
    return ERR_FAILED;
  }

  completed_handshake_ = true;
  next_state_ = STATE_NONE;
  return OK;
}

int HttpProxySocket::GetPeerAddress(IPEndPoint* address) const {
  return transport_->GetPeerAddress(address);
}

int HttpProxySocket::GetLocalAddress(IPEndPoint* address) const {
  return transport_->GetLocalAddress(address);
}

}  // namespace net
