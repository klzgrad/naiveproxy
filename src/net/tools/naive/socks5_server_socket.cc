// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/socks5_server_socket.h"

#include <cstring>
#include <utility>

#include "base/cxx17_backports.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/sys_byteorder.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/sys_addrinfo.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"

namespace net {

enum SocksCommandType {
  kCommandConnect = 0x01,
  kCommandBind = 0x02,
  kCommandUDPAssociate = 0x03,
};

static constexpr unsigned int kGreetReadHeaderSize = 2;
static constexpr unsigned int kAuthReadHeaderSize = 2;
static constexpr unsigned int kReadHeaderSize = 5;
static constexpr char kSOCKS5Version = '\x05';
static constexpr char kSOCKS5Reserved = '\x00';
static constexpr char kAuthMethodNone = '\x00';
static constexpr char kAuthMethodUserPass = '\x02';
static constexpr char kAuthMethodNoAcceptable = '\xff';
static constexpr char kSubnegotiationVersion = '\x01';
static constexpr char kAuthStatusSuccess = '\x00';
static constexpr char kAuthStatusFailure = '\xff';
static constexpr char kReplySuccess = '\x00';
static constexpr char kReplyCommandNotSupported = '\x07';

static_assert(sizeof(struct in_addr) == 4, "incorrect system size of IPv4");
static_assert(sizeof(struct in6_addr) == 16, "incorrect system size of IPv6");

Socks5ServerSocket::Socks5ServerSocket(
    std::unique_ptr<StreamSocket> transport_socket,
    const std::string& user,
    const std::string& pass,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : io_callback_(base::BindRepeating(&Socks5ServerSocket::OnIOComplete,
                                       base::Unretained(this))),
      transport_(std::move(transport_socket)),
      next_state_(STATE_NONE),
      completed_handshake_(false),
      bytes_sent_(0),
      was_ever_used_(false),
      user_(user),
      pass_(pass),
      net_log_(transport_->NetLog()),
      traffic_annotation_(traffic_annotation) {}

Socks5ServerSocket::~Socks5ServerSocket() {
  Disconnect();
}

const HostPortPair& Socks5ServerSocket::request_endpoint() const {
  return request_endpoint_;
}

int Socks5ServerSocket::Connect(CompletionOnceCallback callback) {
  DCHECK(transport_);
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(!user_callback_);

  // If already connected, then just return OK.
  if (completed_handshake_)
    return OK;

  net_log_.BeginEvent(NetLogEventType::SOCKS5_CONNECT);

  next_state_ = STATE_GREET_READ;
  buffer_.clear();

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    user_callback_ = std::move(callback);
  } else {
    net_log_.EndEventWithNetErrorCode(NetLogEventType::SOCKS5_CONNECT, rv);
  }
  return rv;
}

void Socks5ServerSocket::Disconnect() {
  completed_handshake_ = false;
  transport_->Disconnect();

  // Reset other states to make sure they aren't mistakenly used later.
  // These are the states initialized by Connect().
  next_state_ = STATE_NONE;
  user_callback_.Reset();
}

bool Socks5ServerSocket::IsConnected() const {
  return completed_handshake_ && transport_->IsConnected();
}

bool Socks5ServerSocket::IsConnectedAndIdle() const {
  return completed_handshake_ && transport_->IsConnectedAndIdle();
}

const NetLogWithSource& Socks5ServerSocket::NetLog() const {
  return net_log_;
}

bool Socks5ServerSocket::WasEverUsed() const {
  return was_ever_used_;
}

bool Socks5ServerSocket::WasAlpnNegotiated() const {
  if (transport_) {
    return transport_->WasAlpnNegotiated();
  }
  NOTREACHED();
  return false;
}

NextProto Socks5ServerSocket::GetNegotiatedProtocol() const {
  if (transport_) {
    return transport_->GetNegotiatedProtocol();
  }
  NOTREACHED();
  return kProtoUnknown;
}

bool Socks5ServerSocket::GetSSLInfo(SSLInfo* ssl_info) {
  if (transport_) {
    return transport_->GetSSLInfo(ssl_info);
  }
  NOTREACHED();
  return false;
}

int64_t Socks5ServerSocket::GetTotalReceivedBytes() const {
  return transport_->GetTotalReceivedBytes();
}

void Socks5ServerSocket::ApplySocketTag(const SocketTag& tag) {
  return transport_->ApplySocketTag(tag);
}

// Read is called by the transport layer above to read. This can only be done
// if the SOCKS handshake is complete.
int Socks5ServerSocket::Read(IOBuffer* buf,
                             int buf_len,
                             CompletionOnceCallback callback) {
  DCHECK(completed_handshake_);
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(!user_callback_);
  DCHECK(callback);

  int rv = transport_->Read(
      buf, buf_len,
      base::BindOnce(&Socks5ServerSocket::OnReadWriteComplete,
                     base::Unretained(this), std::move(callback)));
  if (rv > 0)
    was_ever_used_ = true;
  return rv;
}

// Write is called by the transport layer. This can only be done if the
// SOCKS handshake is complete.
int Socks5ServerSocket::Write(
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
      base::BindOnce(&Socks5ServerSocket::OnReadWriteComplete,
                     base::Unretained(this), std::move(callback)),
      traffic_annotation);
  if (rv > 0)
    was_ever_used_ = true;
  return rv;
}

int Socks5ServerSocket::SetReceiveBufferSize(int32_t size) {
  return transport_->SetReceiveBufferSize(size);
}

int Socks5ServerSocket::SetSendBufferSize(int32_t size) {
  return transport_->SetSendBufferSize(size);
}

void Socks5ServerSocket::DoCallback(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(user_callback_);

  // Since Run() may result in Read being called,
  // clear user_callback_ up front.
  std::move(user_callback_).Run(result);
}

void Socks5ServerSocket::OnIOComplete(int result) {
  DCHECK_NE(STATE_NONE, next_state_);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    net_log_.EndEvent(NetLogEventType::SOCKS5_CONNECT);
    DoCallback(rv);
  }
}

void Socks5ServerSocket::OnReadWriteComplete(CompletionOnceCallback callback,
                                             int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(callback);

  if (result > 0)
    was_ever_used_ = true;
  std::move(callback).Run(result);
}

int Socks5ServerSocket::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_GREET_READ:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(NetLogEventType::SOCKS5_GREET_READ);
        rv = DoGreetRead();
        break;
      case STATE_GREET_READ_COMPLETE:
        rv = DoGreetReadComplete(rv);
        net_log_.EndEventWithNetErrorCode(NetLogEventType::SOCKS5_GREET_READ,
                                          rv);
        break;
      case STATE_GREET_WRITE:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(NetLogEventType::SOCKS5_GREET_WRITE);
        rv = DoGreetWrite();
        break;
      case STATE_GREET_WRITE_COMPLETE:
        rv = DoGreetWriteComplete(rv);
        net_log_.EndEventWithNetErrorCode(NetLogEventType::SOCKS5_GREET_WRITE,
                                          rv);
        break;
      case STATE_AUTH_READ:
        DCHECK_EQ(OK, rv);
        rv = DoAuthRead();
        break;
      case STATE_AUTH_READ_COMPLETE:
        rv = DoAuthReadComplete(rv);
        break;
      case STATE_AUTH_WRITE:
        DCHECK_EQ(OK, rv);
        rv = DoAuthWrite();
        break;
      case STATE_AUTH_WRITE_COMPLETE:
        rv = DoAuthWriteComplete(rv);
        break;
      case STATE_HANDSHAKE_READ:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(NetLogEventType::SOCKS5_HANDSHAKE_READ);
        rv = DoHandshakeRead();
        break;
      case STATE_HANDSHAKE_READ_COMPLETE:
        rv = DoHandshakeReadComplete(rv);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::SOCKS5_HANDSHAKE_READ, rv);
        break;
      case STATE_HANDSHAKE_WRITE:
        DCHECK_EQ(OK, rv);
        net_log_.BeginEvent(NetLogEventType::SOCKS5_HANDSHAKE_WRITE);
        rv = DoHandshakeWrite();
        break;
      case STATE_HANDSHAKE_WRITE_COMPLETE:
        rv = DoHandshakeWriteComplete(rv);
        net_log_.EndEventWithNetErrorCode(
            NetLogEventType::SOCKS5_HANDSHAKE_WRITE, rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

int Socks5ServerSocket::DoGreetRead() {
  next_state_ = STATE_GREET_READ_COMPLETE;

  if (buffer_.empty()) {
    read_header_size_ = kGreetReadHeaderSize;
  }

  int handshake_buf_len = read_header_size_ - buffer_.size();
  DCHECK_LT(0, handshake_buf_len);
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(handshake_buf_len);
  return transport_->Read(handshake_buf_.get(), handshake_buf_len,
                          io_callback_);
}

int Socks5ServerSocket::DoGreetReadComplete(int result) {
  if (result < 0)
    return result;

  if (result == 0) {
    net_log_.AddEvent(
        NetLogEventType::SOCKS_UNEXPECTEDLY_CLOSED_DURING_GREETING);
    return ERR_SOCKS_CONNECTION_FAILED;
  }

  buffer_.append(handshake_buf_->data(), result);

  // When the first few bytes are read, check how many more are required
  // and accordingly increase them
  if (buffer_.size() == kGreetReadHeaderSize) {
    if (buffer_[0] != kSOCKS5Version) {
      net_log_.AddEventWithIntParams(NetLogEventType::SOCKS_UNEXPECTED_VERSION,
                                     "version", buffer_[0]);
      return ERR_SOCKS_CONNECTION_FAILED;
    }
    int nmethods = buffer_[1];
    if (nmethods == 0) {
      net_log_.AddEvent(NetLogEventType::SOCKS_NO_REQUESTED_AUTH);
      return ERR_SOCKS_CONNECTION_FAILED;
    }

    read_header_size_ += nmethods;
    next_state_ = STATE_GREET_READ;
    return OK;
  }

  if (buffer_.size() == read_header_size_) {
    int nmethods = buffer_[1];
    char expected_method = kAuthMethodNone;
    if (!user_.empty() || !pass_.empty()) {
      expected_method = kAuthMethodUserPass;
    }
    void* match =
        std::memchr(&buffer_[kGreetReadHeaderSize], expected_method, nmethods);
    if (match) {
      auth_method_ = expected_method;
    } else {
      auth_method_ = kAuthMethodNoAcceptable;
    }
    buffer_.clear();
    next_state_ = STATE_GREET_WRITE;
    return OK;
  }

  next_state_ = STATE_GREET_READ;
  return OK;
}

int Socks5ServerSocket::DoGreetWrite() {
  if (buffer_.empty()) {
    const char write_data[] = {kSOCKS5Version, auth_method_};
    buffer_ = std::string(write_data, std::size(write_data));
    bytes_sent_ = 0;
  }

  next_state_ = STATE_GREET_WRITE_COMPLETE;
  int handshake_buf_len = buffer_.size() - bytes_sent_;
  DCHECK_LT(0, handshake_buf_len);
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(handshake_buf_len);
  std::memcpy(handshake_buf_->data(), &buffer_.data()[bytes_sent_],
              handshake_buf_len);
  return transport_->Write(handshake_buf_.get(), handshake_buf_len,
                           io_callback_, traffic_annotation_);
}

int Socks5ServerSocket::DoGreetWriteComplete(int result) {
  if (result < 0)
    return result;

  bytes_sent_ += result;
  if (bytes_sent_ == buffer_.size()) {
    buffer_.clear();
    if (auth_method_ == kAuthMethodNone) {
      next_state_ = STATE_HANDSHAKE_READ;
    } else if (auth_method_ == kAuthMethodUserPass) {
      next_state_ = STATE_AUTH_READ;
    } else {
      net_log_.AddEvent(NetLogEventType::SOCKS_NO_ACCEPTABLE_AUTH);
      return ERR_SOCKS_CONNECTION_FAILED;
    }
  } else {
    next_state_ = STATE_GREET_WRITE;
  }
  return OK;
}

int Socks5ServerSocket::DoAuthRead() {
  next_state_ = STATE_AUTH_READ_COMPLETE;

  if (buffer_.empty()) {
    read_header_size_ = kAuthReadHeaderSize;
  }

  int handshake_buf_len = read_header_size_ - buffer_.size();
  DCHECK_LT(0, handshake_buf_len);
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(handshake_buf_len);
  return transport_->Read(handshake_buf_.get(), handshake_buf_len,
                          io_callback_);
}

int Socks5ServerSocket::DoAuthReadComplete(int result) {
  if (result < 0)
    return result;

  if (result == 0) {
    return ERR_SOCKS_CONNECTION_FAILED;
  }

  buffer_.append(handshake_buf_->data(), result);

  // When the first few bytes are read, check how many more are required
  // and accordingly increase them
  if (buffer_.size() == kAuthReadHeaderSize) {
    if (buffer_[0] != kSubnegotiationVersion) {
      net_log_.AddEventWithIntParams(NetLogEventType::SOCKS_UNEXPECTED_VERSION,
                                     "version", buffer_[0]);
      return ERR_SOCKS_CONNECTION_FAILED;
    }
    int username_len = buffer_[1];
    read_header_size_ += username_len + 1;
    next_state_ = STATE_AUTH_READ;
    return OK;
  }

  if (buffer_.size() == read_header_size_) {
    int username_len = buffer_[1];
    int password_len = buffer_[kAuthReadHeaderSize + username_len];
    size_t password_offset = kAuthReadHeaderSize + username_len + 1;
    if (buffer_.size() == password_offset && password_len != 0) {
      read_header_size_ += password_len;
      next_state_ = STATE_AUTH_READ;
      return OK;
    }

    if (buffer_.compare(kAuthReadHeaderSize, username_len, user_) == 0 &&
        buffer_.compare(password_offset, password_len, pass_) == 0) {
      auth_status_ = kAuthStatusSuccess;
    } else {
      auth_status_ = kAuthStatusFailure;
    }
    buffer_.clear();
    next_state_ = STATE_AUTH_WRITE;
    return OK;
  }

  next_state_ = STATE_AUTH_READ;
  return OK;
}

int Socks5ServerSocket::DoAuthWrite() {
  if (buffer_.empty()) {
    const char write_data[] = {kSubnegotiationVersion, auth_status_};
    buffer_ = std::string(write_data, std::size(write_data));
    bytes_sent_ = 0;
  }

  next_state_ = STATE_AUTH_WRITE_COMPLETE;
  int handshake_buf_len = buffer_.size() - bytes_sent_;
  DCHECK_LT(0, handshake_buf_len);
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(handshake_buf_len);
  std::memcpy(handshake_buf_->data(), &buffer_.data()[bytes_sent_],
              handshake_buf_len);
  return transport_->Write(handshake_buf_.get(), handshake_buf_len,
                           io_callback_, traffic_annotation_);
}

int Socks5ServerSocket::DoAuthWriteComplete(int result) {
  if (result < 0)
    return result;

  bytes_sent_ += result;
  if (bytes_sent_ == buffer_.size()) {
    buffer_.clear();
    if (auth_status_ == kAuthStatusSuccess) {
      next_state_ = STATE_HANDSHAKE_READ;
    } else {
      return ERR_SOCKS_CONNECTION_FAILED;
    }
  } else {
    next_state_ = STATE_AUTH_WRITE;
  }
  return OK;
}

int Socks5ServerSocket::DoHandshakeRead() {
  next_state_ = STATE_HANDSHAKE_READ_COMPLETE;

  if (buffer_.empty()) {
    read_header_size_ = kReadHeaderSize;
  }

  int handshake_buf_len = read_header_size_ - buffer_.size();
  DCHECK_LT(0, handshake_buf_len);
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(handshake_buf_len);
  return transport_->Read(handshake_buf_.get(), handshake_buf_len,
                          io_callback_);
}

int Socks5ServerSocket::DoHandshakeReadComplete(int result) {
  if (result < 0)
    return result;

  // The underlying socket closed unexpectedly.
  if (result == 0) {
    net_log_.AddEvent(
        NetLogEventType::SOCKS_UNEXPECTEDLY_CLOSED_DURING_HANDSHAKE);
    return ERR_SOCKS_CONNECTION_FAILED;
  }

  buffer_.append(handshake_buf_->data(), result);

  // When the first few bytes are read, check how many more are required
  // and accordingly increase them
  if (buffer_.size() == kReadHeaderSize) {
    if (buffer_[0] != kSOCKS5Version || buffer_[2] != kSOCKS5Reserved) {
      net_log_.AddEventWithIntParams(NetLogEventType::SOCKS_UNEXPECTED_VERSION,
                                     "version", buffer_[0]);
      return ERR_SOCKS_CONNECTION_FAILED;
    }
    SocksCommandType command = static_cast<SocksCommandType>(buffer_[1]);
    if (command == kCommandConnect) {
      // The proxy replies with success immediately without first connecting
      // to the requested endpoint.
      reply_ = kReplySuccess;
    } else if (command == kCommandBind || command == kCommandUDPAssociate) {
      reply_ = kReplyCommandNotSupported;
    } else {
      net_log_.AddEventWithIntParams(NetLogEventType::SOCKS_UNEXPECTED_COMMAND,
                                     "commmand", buffer_[1]);
      return ERR_SOCKS_CONNECTION_FAILED;
    }

    // We check the type of IP/Domain the server returns and accordingly
    // increase the size of the request. For domains, we need to read the
    // size of the domain, so the initial request size is upto the domain
    // size. Since for IPv4/IPv6 the size is fixed and hence no 'size' is
    // read, we substract 1 byte from the additional request size.
    address_type_ = static_cast<SocksEndPointAddressType>(buffer_[3]);
    if (address_type_ == kEndPointDomain) {
      address_size_ = static_cast<uint8_t>(buffer_[4]);
      if (address_size_ == 0) {
        net_log_.AddEvent(NetLogEventType::SOCKS_ZERO_LENGTH_DOMAIN);
        return ERR_SOCKS_CONNECTION_FAILED;
      }
    } else if (address_type_ == kEndPointResolvedIPv4) {
      address_size_ = sizeof(struct in_addr);
      --read_header_size_;
    } else if (address_type_ == kEndPointResolvedIPv6) {
      address_size_ = sizeof(struct in6_addr);
      --read_header_size_;
    } else {
      // Aborts connection on unspecified address type.
      net_log_.AddEventWithIntParams(
          NetLogEventType::SOCKS_UNKNOWN_ADDRESS_TYPE, "address_type",
          buffer_[3]);
      return ERR_SOCKS_CONNECTION_FAILED;
    }

    read_header_size_ += address_size_ + sizeof(uint16_t);
    next_state_ = STATE_HANDSHAKE_READ;
    return OK;
  }

  // When the final bytes are read, setup handshake.
  if (buffer_.size() == read_header_size_) {
    size_t port_start = read_header_size_ - sizeof(uint16_t);
    uint16_t port_net;
    std::memcpy(&port_net, &buffer_[port_start], sizeof(uint16_t));
    uint16_t port_host = base::NetToHost16(port_net);

    size_t address_start = port_start - address_size_;
    if (address_type_ == kEndPointDomain) {
      std::string domain(&buffer_[address_start], address_size_);
      request_endpoint_ = HostPortPair(domain, port_host);
    } else {
      IPAddress ip_addr(
          reinterpret_cast<const uint8_t*>(&buffer_[address_start]),
          address_size_);
      IPEndPoint endpoint(ip_addr, port_host);
      request_endpoint_ = HostPortPair::FromIPEndPoint(endpoint);
    }
    buffer_.clear();
    next_state_ = STATE_HANDSHAKE_WRITE;
    return OK;
  }

  next_state_ = STATE_HANDSHAKE_READ;
  return OK;
}

// Writes the SOCKS handshake data to the underlying socket connection.
int Socks5ServerSocket::DoHandshakeWrite() {
  next_state_ = STATE_HANDSHAKE_WRITE_COMPLETE;

  if (buffer_.empty()) {
    const char write_data[] = {
        // clang-format off
        kSOCKS5Version,
        reply_,
        kSOCKS5Reserved,
        kEndPointResolvedIPv4,
        0x00, 0x00, 0x00, 0x00,  // BND.ADDR
        0x00, 0x00,  // BND.PORT
        // clang-format on
    };
    buffer_ = std::string(write_data, std::size(write_data));
    bytes_sent_ = 0;
  }

  int handshake_buf_len = buffer_.size() - bytes_sent_;
  DCHECK_LT(0, handshake_buf_len);
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(handshake_buf_len);
  std::memcpy(handshake_buf_->data(), &buffer_[bytes_sent_], handshake_buf_len);
  return transport_->Write(handshake_buf_.get(), handshake_buf_len,
                           io_callback_, traffic_annotation_);
}

int Socks5ServerSocket::DoHandshakeWriteComplete(int result) {
  if (result < 0)
    return result;

  // We ignore the case when result is 0, since the underlying Write
  // may return spurious writes while waiting on the socket.

  bytes_sent_ += result;
  if (bytes_sent_ == buffer_.size()) {
    buffer_.clear();
    if (reply_ == kReplySuccess) {
      completed_handshake_ = true;
      next_state_ = STATE_NONE;
    } else {
      net_log_.AddEventWithIntParams(NetLogEventType::SOCKS_SERVER_ERROR,
                                     "error_code", reply_);
      return ERR_SOCKS_CONNECTION_FAILED;
    }
  } else {
    next_state_ = STATE_HANDSHAKE_WRITE;
  }

  return OK;
}

int Socks5ServerSocket::GetPeerAddress(IPEndPoint* address) const {
  return transport_->GetPeerAddress(address);
}

int Socks5ServerSocket::GetLocalAddress(IPEndPoint* address) const {
  return transport_->GetLocalAddress(address);
}

}  // namespace net
