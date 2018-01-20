// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/http_proxy_server_socket.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/string_split.h"
#include "base/sys_byteorder.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/log/net_log.h"
#include "net/third_party/quiche/src/quiche/spdy/core/hpack/hpack_constants.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "url/gurl.h"

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

HttpProxyServerSocket::HttpProxyServerSocket(
    std::unique_ptr<StreamSocket> transport_socket,
    ClientPaddingDetectorDelegate* padding_detector_delegate,
    const NetworkTrafficAnnotationTag& traffic_annotation,
    const std::vector<PaddingType>& supported_padding_types)
    : io_callback_(base::BindRepeating(&HttpProxyServerSocket::OnIOComplete,
                                       base::Unretained(this))),
      transport_(std::move(transport_socket)),
      padding_detector_delegate_(padding_detector_delegate),
      next_state_(STATE_NONE),
      completed_handshake_(false),
      was_ever_used_(false),
      header_write_size_(-1),
      net_log_(transport_->NetLog()),
      traffic_annotation_(traffic_annotation),
      supported_padding_types_(supported_padding_types) {}

HttpProxyServerSocket::~HttpProxyServerSocket() {
  Disconnect();
}

const HostPortPair& HttpProxyServerSocket::request_endpoint() const {
  return request_endpoint_;
}

int HttpProxyServerSocket::Connect(CompletionOnceCallback callback) {
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

void HttpProxyServerSocket::Disconnect() {
  completed_handshake_ = false;
  transport_->Disconnect();

  // Reset other states to make sure they aren't mistakenly used later.
  // These are the states initialized by Connect().
  next_state_ = STATE_NONE;
  user_callback_.Reset();
}

bool HttpProxyServerSocket::IsConnected() const {
  return completed_handshake_ && transport_->IsConnected();
}

bool HttpProxyServerSocket::IsConnectedAndIdle() const {
  return completed_handshake_ && transport_->IsConnectedAndIdle();
}

const NetLogWithSource& HttpProxyServerSocket::NetLog() const {
  return net_log_;
}

bool HttpProxyServerSocket::WasEverUsed() const {
  return was_ever_used_;
}

bool HttpProxyServerSocket::WasAlpnNegotiated() const {
  if (transport_) {
    return transport_->WasAlpnNegotiated();
  }
  NOTREACHED();
  return false;
}

NextProto HttpProxyServerSocket::GetNegotiatedProtocol() const {
  if (transport_) {
    return transport_->GetNegotiatedProtocol();
  }
  NOTREACHED();
  return kProtoUnknown;
}

bool HttpProxyServerSocket::GetSSLInfo(SSLInfo* ssl_info) {
  if (transport_) {
    return transport_->GetSSLInfo(ssl_info);
  }
  NOTREACHED();
  return false;
}

int64_t HttpProxyServerSocket::GetTotalReceivedBytes() const {
  return transport_->GetTotalReceivedBytes();
}

void HttpProxyServerSocket::ApplySocketTag(const SocketTag& tag) {
  return transport_->ApplySocketTag(tag);
}

// Read is called by the transport layer above to read. This can only be done
// if the HTTP header is complete.
int HttpProxyServerSocket::Read(IOBuffer* buf,
                                int buf_len,
                                CompletionOnceCallback callback) {
  DCHECK(completed_handshake_);
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(!user_callback_);
  DCHECK(callback);

  if (!buffer_.empty()) {
    was_ever_used_ = true;
    int data_len = buffer_.size();
    if (data_len <= buf_len) {
      std::memcpy(buf->data(), buffer_.data(), data_len);
      buffer_.clear();
      return data_len;
    } else {
      std::memcpy(buf->data(), buffer_.data(), buf_len);
      buffer_ = buffer_.substr(buf_len);
      return buf_len;
    }
  }

  int rv = transport_->Read(
      buf, buf_len,
      base::BindOnce(&HttpProxyServerSocket::OnReadWriteComplete,
                     base::Unretained(this), std::move(callback)));
  if (rv > 0)
    was_ever_used_ = true;
  return rv;
}

// Write is called by the transport layer. This can only be done if the
// HTTP CONNECT request is complete.
int HttpProxyServerSocket::Write(
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
      base::BindOnce(&HttpProxyServerSocket::OnReadWriteComplete,
                     base::Unretained(this), std::move(callback)),
      traffic_annotation);
  if (rv > 0)
    was_ever_used_ = true;
  return rv;
}

int HttpProxyServerSocket::SetReceiveBufferSize(int32_t size) {
  return transport_->SetReceiveBufferSize(size);
}

int HttpProxyServerSocket::SetSendBufferSize(int32_t size) {
  return transport_->SetSendBufferSize(size);
}

void HttpProxyServerSocket::DoCallback(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(user_callback_);

  // Since Run() may result in Read being called,
  // clear user_callback_ up front.
  std::move(user_callback_).Run(result);
}

void HttpProxyServerSocket::OnIOComplete(int result) {
  DCHECK_NE(STATE_NONE, next_state_);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    DoCallback(rv);
  }
}

void HttpProxyServerSocket::OnReadWriteComplete(CompletionOnceCallback callback,
                                                int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(callback);

  if (result > 0)
    was_ever_used_ = true;
  std::move(callback).Run(result);
}

int HttpProxyServerSocket::DoLoop(int last_io_result) {
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

int HttpProxyServerSocket::DoHeaderRead() {
  next_state_ = STATE_HEADER_READ_COMPLETE;

  handshake_buf_ = base::MakeRefCounted<IOBuffer>(kBufferSize);
  return transport_->Read(handshake_buf_.get(), kBufferSize, io_callback_);
}

std::optional<PaddingType> HttpProxyServerSocket::ParsePaddingHeaders(
    const HttpRequestHeaders& headers) {
  bool has_padding = headers.HasHeader(kPaddingHeader);
  std::string padding_type_request;
  bool has_padding_type_request =
      headers.GetHeader(kPaddingTypeRequestHeader, &padding_type_request);

  if (!has_padding_type_request) {
    // Backward compatibility with before kVariant1 when the padding-version
    // header does not exist.
    if (has_padding) {
      return PaddingType::kVariant1;
    } else {
      return PaddingType::kNone;
    }
  }

  std::vector<base::StringPiece> padding_type_strs = base::SplitStringPiece(
      padding_type_request, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (base::StringPiece padding_type_str : padding_type_strs) {
    std::optional<PaddingType> padding_type =
        ParsePaddingType(padding_type_str);
    if (!padding_type.has_value()) {
      LOG(ERROR) << "Invalid padding type: " << padding_type_str;
      return std::nullopt;
    }
    if (std::find(supported_padding_types_.begin(),
                  supported_padding_types_.end(),
                  *padding_type) != supported_padding_types_.end()) {
      return padding_type;
    }
  }
  LOG(ERROR) << "No padding type is supported: " << padding_type_request;
  return std::nullopt;
}

int HttpProxyServerSocket::DoHeaderReadComplete(int result) {
  if (result < 0)
    return result;

  if (result == 0) {
    return ERR_CONNECTION_CLOSED;
  }

  buffer_.append(handshake_buf_->data(), result);
  if (buffer_.size() > kMaxHeaderSize) {
    return ERR_MSG_TOO_BIG;
  }

  size_t header_end = buffer_.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    next_state_ = STATE_HEADER_READ;
    return OK;
  }

  size_t first_line_end = buffer_.find("\r\n");
  size_t first_space = buffer_.find(' ');
  bool is_http_1_0 = false;
  if (first_space == std::string::npos || first_space + 1 >= first_line_end) {
    LOG(WARNING) << "Invalid request: " << buffer_.substr(0, first_line_end);
    return ERR_INVALID_ARGUMENT;
  }
  size_t second_space = buffer_.find(' ', first_space + 1);
  if (second_space == std::string::npos || second_space >= first_line_end) {
    LOG(WARNING) << "Invalid request: " << buffer_.substr(0, first_line_end);
    return ERR_INVALID_ARGUMENT;
  }

  std::string method = buffer_.substr(0, first_space);
  std::string uri =
      buffer_.substr(first_space + 1, second_space - (first_space + 1));
  std::string version =
      buffer_.substr(second_space + 1, first_line_end - (second_space + 1));
  if (method == HttpRequestHeaders::kConnectMethod) {
    request_endpoint_ = HostPortPair::FromString(uri);
  } else {
    // postprobe endpoint handling
    is_http_1_0 = true;
  }

  size_t second_line = first_line_end + 2;
  HttpRequestHeaders headers;
  std::string headers_str;
  if (second_line < header_end) {
    headers_str = buffer_.substr(second_line, header_end - second_line);
    headers.AddHeadersFromString(headers_str);
  }

  if (is_http_1_0) {
    GURL url(uri);
    if (!url.is_valid()) {
      LOG(WARNING) << "Invalid URI: " << uri;
      return ERR_INVALID_ARGUMENT;
    }

    std::string host;
    int port;

    std::string host_str;
    if (headers.GetHeader(HttpRequestHeaders::kHost, &host_str)) {
      if (!ParseHostAndPort(host_str, &host, &port)) {
        LOG(WARNING) << "Invalid Host: " << host_str;
        return ERR_INVALID_ARGUMENT;
      }
      if (port == -1) {
        port = 80;
      }
    } else {
      if (!url.has_host()) {
        LOG(WARNING) << "Missing host: " << uri;
        return ERR_INVALID_ARGUMENT;
      }
      host = url.host();
      port = url.EffectiveIntPort();

      host_str = url.host();
      if (url.has_port()) {
        host_str.append(":").append(url.port());
      }
      headers.SetHeader(HttpRequestHeaders::kHost, host_str);
    }
    // Host is already known. Converts any absolute URI to relative.
    uri = url.path();
    if (url.has_query()) {
      uri.append("?").append(url.query());
    }

    request_endpoint_.set_host(host);
    request_endpoint_.set_port(port);
  }

  std::optional<PaddingType> padding_type = ParsePaddingHeaders(headers);
  if (!padding_type.has_value()) {
    return ERR_INVALID_ARGUMENT;
  }
  padding_detector_delegate_->SetClientPaddingType(*padding_type);

  if (is_http_1_0) {
    // Regenerates http header to make sure don't leak them to end servers
    HttpRequestHeaders sanitized_headers = headers;
    sanitized_headers.RemoveHeader(HttpRequestHeaders::kProxyConnection);
    sanitized_headers.RemoveHeader(HttpRequestHeaders::kProxyAuthorization);
    std::ostringstream ss;
    ss << method << " " << uri << " " << version << "\r\n"
       << sanitized_headers.ToString();
    if (buffer_.size() > header_end + 4) {
      ss << buffer_.substr(header_end + 4);
    }
    buffer_ = ss.str();
    // Skips padding write for raw http proxy
    completed_handshake_ = true;
    next_state_ = STATE_NONE;
    return OK;
  }

  buffer_ = buffer_.substr(header_end + 4);

  next_state_ = STATE_HEADER_WRITE;
  return OK;
}

int HttpProxyServerSocket::DoHeaderWrite() {
  next_state_ = STATE_HEADER_WRITE_COMPLETE;

  // Adds padding.
  int padding_size = base::RandInt(kMinPaddingSize, kMaxPaddingSize);
  header_write_size_ = kResponseHeaderSize + padding_size + 4;
  handshake_buf_ = base::MakeRefCounted<IOBuffer>(header_write_size_);
  char* p = handshake_buf_->data();
  std::memcpy(p, kResponseHeader, kResponseHeaderSize);
  FillNonindexHeaderValue(base::RandUint64(), p + kResponseHeaderSize,
                          padding_size);
  std::memcpy(p + kResponseHeaderSize + padding_size, "\r\n\r\n", 4);

  return transport_->Write(handshake_buf_.get(), header_write_size_,
                           io_callback_, traffic_annotation_);
}

int HttpProxyServerSocket::DoHeaderWriteComplete(int result) {
  if (result < 0)
    return result;

  if (result != header_write_size_) {
    return ERR_FAILED;
  }

  completed_handshake_ = true;
  next_state_ = STATE_NONE;
  return OK;
}

int HttpProxyServerSocket::GetPeerAddress(IPEndPoint* address) const {
  return transport_->GetPeerAddress(address);
}

int HttpProxyServerSocket::GetLocalAddress(IPEndPoint* address) const {
  return transport_->GetLocalAddress(address);
}

}  // namespace net
