// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/websocket_tunnel_socket.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "net/base/io_buffer.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/base/transport_info.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/padding_utils.h"
#include "net/storage_access_api/status.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_handshake_response_info.h"
#include "net/websockets/websocket_stream.h"
#include "url/origin.h"

namespace net {

namespace {

constexpr uint8_t kTunnelProtocolVersion = 1;
constexpr uint8_t kAddressTypeIPv4 = 1;
constexpr uint8_t kAddressTypeDomain = 3;
constexpr uint8_t kAddressTypeIPv6 = 4;
constexpr size_t kMaxDomainLength = 255;

WebSocketFrameHeader::OpCode FrameOpcode(const WebSocketFrame& frame) {
  return frame.header.opcode;
}

}  // namespace

class WebSocketTunnelSocket::ConnectDelegateImpl
    : public WebSocketStream::ConnectDelegate {
 public:
  explicit ConnectDelegateImpl(
      base::WeakPtr<WebSocketTunnelSocket> socket)
      : socket_(std::move(socket)) {}
  ~ConnectDelegateImpl() override = default;

  void OnCreateRequest(URLRequest* request) override {}

  int OnURLRequestConnected(URLRequest* request,
                            const TransportInfo& info,
                            CompletionOnceCallback callback) override {
    if (socket_) {
      socket_->peer_address_ = info.endpoint;
      socket_->negotiated_protocol_ = info.negotiated_protocol;
    }
    return OK;
  }

  void OnSuccess(
      std::unique_ptr<WebSocketStream> stream,
      std::unique_ptr<WebSocketHandshakeResponseInfo> response) override {
    if (socket_) {
      socket_->OnConnectSuccess(std::move(stream), std::move(response));
    }
  }

  void OnFailure(const std::string& message,
                 int net_error,
                 std::optional<int> response_code) override {
    if (socket_) {
      socket_->OnConnectFailure(net_error);
    }
  }

  void OnStartOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeRequestInfo> request) override {}

  void OnSSLCertificateError(
      std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks>
          ssl_error_callbacks,
      int net_error,
      const SSLInfo& ssl_info,
      bool fatal) override {
    ssl_error_callbacks->CancelSSLRequest(net_error, &ssl_info);
  }

  int OnAuthRequired(
      const AuthChallengeInfo& auth_info,
      scoped_refptr<HttpResponseHeaders> response_headers,
      const IPEndPoint& remote_endpoint,
      base::OnceCallback<void(const AuthCredentials*)> callback,
      std::optional<AuthCredentials>* credentials) override {
    if (socket_ && !socket_->credentials_.Empty()) {
      *credentials = socket_->credentials_;
    }
    return OK;
  }

 private:
  base::WeakPtr<WebSocketTunnelSocket> socket_;
};

WebSocketTunnelSocket::WebSocketTunnelSocket(
    const GURL& socket_url,
    const AuthCredentials& credentials,
    const HostPortPair& target,
    URLRequestContext* url_request_context,
    const NetLogWithSource& net_log,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : socket_url_(socket_url),
      credentials_(credentials),
      target_(target),
      url_request_context_(url_request_context),
      net_log_(net_log),
      traffic_annotation_(traffic_annotation) {
  DCHECK(url_request_context_);
  DCHECK(socket_url_.SchemeIs("ws") || socket_url_.SchemeIs("wss"));
  DCHECK(!target_.IsEmpty());
}

WebSocketTunnelSocket::~WebSocketTunnelSocket() {
  Disconnect();
}

int WebSocketTunnelSocket::Connect(CompletionOnceCallback callback) {
  if (state_ == State::kConnected) {
    return OK;
  }
  if (state_ != State::kDisconnected) {
    return ERR_UNEXPECTED;
  }
  DCHECK(!connect_callback_);
  DCHECK(!stream_);
  DCHECK(!request_);
  connect_callback_ = std::move(callback);

  url::Origin origin = url::Origin::Create(socket_url_);
  HttpRequestHeaders additional_headers;
  InitializeNonindexCodes();
  std::string padding(base::RandIntInclusive(16, 32), '~');
  FillNonindexHeaderValue(base::RandUint64(),
                          base::as_writable_byte_span(padding));
  additional_headers.SetHeader(kPaddingHeader, padding);
  additional_headers.SetHeader(kPaddingTypeRequestHeader, "1,0");
  if (!credentials_.Empty()) {
    std::string username = base::UTF16ToUTF8(credentials_.username());
    std::string password = base::UTF16ToUTF8(credentials_.password());
    std::string basic_value = base::Base64Encode(username + ":" + password);
    additional_headers.SetHeader("Authorization", "Basic " + basic_value);
  }

  state_ = State::kConnecting;
  request_ = WebSocketStream::CreateAndConnectStream(
      socket_url_, {}, origin, StorageAccessApiStatus::kNone,
      IsolationInfo::CreateTransient(std::nullopt), additional_headers,
      url_request_context_, net_log_, WebSocketPriorityHint::kDefault,
      traffic_annotation_,
      std::make_unique<ConnectDelegateImpl>(weak_ptr_factory_.GetWeakPtr()));
  if (state_ == State::kDisconnected) {
    return ERR_CONNECTION_ABORTED;
  }
  return ERR_IO_PENDING;
}

void WebSocketTunnelSocket::Disconnect() {
  request_.reset();
  if (stream_) {
    stream_->Close();
    stream_.reset();
  }
  state_ = State::kDisconnected;
  ever_used_ = false;
  read_pending_ = false;
  write_pending_ = false;
  read_offset_ = 0;
  read_frames_.clear();
  write_frames_.clear();
  control_frames_.clear();
  write_payload_.reset();
  control_payload_.reset();
  read_user_buffer_ = nullptr;
  read_user_buffer_len_ = 0;
  connect_callback_.Reset();
  read_callback_.Reset();
  write_callback_.Reset();
  write_result_size_ = 0;
  control_write_pending_ = false;
  user_write_queued_ = false;
}

bool WebSocketTunnelSocket::IsConnected() const {
  return state_ == State::kConnected && stream_ != nullptr;
}

bool WebSocketTunnelSocket::IsConnectedAndIdle() const {
  return IsConnected() && !read_pending_ && !write_pending_;
}

int WebSocketTunnelSocket::GetPeerAddress(IPEndPoint* address) const {
  if (!peer_address_.address().IsValid()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  *address = peer_address_;
  return OK;
}

int WebSocketTunnelSocket::GetLocalAddress(IPEndPoint* address) const {
  return ERR_SOCKET_NOT_CONNECTED;
}

const NetLogWithSource& WebSocketTunnelSocket::NetLog() const {
  return net_log_;
}

bool WebSocketTunnelSocket::WasEverUsed() const {
  return ever_used_;
}

NextProto WebSocketTunnelSocket::GetNegotiatedProtocol() const {
  return negotiated_protocol_;
}

bool WebSocketTunnelSocket::GetSSLInfo(SSLInfo* ssl_info) {
  return false;
}

int64_t WebSocketTunnelSocket::GetTotalReceivedBytes() const {
  return 0;
}

void WebSocketTunnelSocket::ApplySocketTag(const SocketTag& tag) {}

int WebSocketTunnelSocket::Read(IOBuffer* buf,
                                int buf_len,
                                CompletionOnceCallback callback) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);
  if (!IsConnected()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  DCHECK(!read_pending_);
  DCHECK(read_callback_.is_null());

  read_user_buffer_ = buf;
  read_user_buffer_len_ = buf_len;
  int rv = CopyAvailableData();
  if (rv != 0) {
    read_user_buffer_ = nullptr;
    read_user_buffer_len_ = 0;
    if (rv < 0) {
      stream_->Close();
      state_ = State::kDisconnected;
    }
    return rv;
  }

  read_pending_ = true;
  read_callback_ = std::move(callback);
  return ReadWithBuffer();
}

int WebSocketTunnelSocket::Write(IOBuffer* buf,
                                 int buf_len,
                                 CompletionOnceCallback callback,
                                 const NetworkTrafficAnnotationTag&) {
  DCHECK(buf);
  DCHECK_GE(buf_len, 0);
  if (!IsConnected()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  if (buf_len == 0) {
    return 0;
  }
  DCHECK(!write_pending_);
  DCHECK(write_frames_.empty());

  ever_used_ = true;
  write_payload_ = base::MakeRefCounted<IOBufferWithSize>(buf_len);
  std::memcpy(write_payload_->data(), buf->data(),
              base::checked_cast<size_t>(buf_len));
  auto frame = std::make_unique<WebSocketFrame>(
      WebSocketFrameHeader::kOpCodeBinary);
  frame->header.final = true;
  frame->header.masked = true;
  frame->header.payload_length = base::checked_cast<uint64_t>(buf_len);
  frame->payload = write_payload_->first(base::checked_cast<size_t>(buf_len));
  write_frames_.push_back(std::move(frame));
  write_result_size_ = buf_len;

  write_pending_ = true;
  write_callback_ = std::move(callback);
  if (control_write_pending_) {
    user_write_queued_ = true;
    return ERR_IO_PENDING;
  }
  return BeginUserWrite();
}

int WebSocketTunnelSocket::BeginUserWrite() {
  int rv = stream_->WriteFrames(&write_frames_, base::BindOnce(
      &WebSocketTunnelSocket::OnWriteFramesComplete,
      weak_ptr_factory_.GetWeakPtr()));
  if (rv != ERR_IO_PENDING) {
    OnWriteFramesComplete(rv);
  }
  return ERR_IO_PENDING;
}

int WebSocketTunnelSocket::SetReceiveBufferSize(int32_t size) {
  return OK;
}

int WebSocketTunnelSocket::SetSendBufferSize(int32_t size) {
  return OK;
}

void WebSocketTunnelSocket::OnConnectSuccess(
    std::unique_ptr<WebSocketStream> stream,
    std::unique_ptr<WebSocketHandshakeResponseInfo> response) {
  if (state_ != State::kConnecting) {
    return;
  }
  DCHECK(!stream_);
  if (!response || !response->headers) {
    Fail(ERR_INVALID_RESPONSE);
    return;
  }

  std::optional<PaddingType> padding_type =
      ParsePaddingHeaders(*response->headers);
  if (!padding_type.has_value()) {
    LOG(ERROR) << "Received invalid WebSocket padding type";
    Fail(ERR_INVALID_RESPONSE);
    return;
  }

  negotiated_padding_type_ = *padding_type;
  LOG(INFO) << "Negotiated WebSocket padding type: "
            << ToReadableString(negotiated_padding_type_);

  stream_ = std::move(stream);
  request_.reset();
  int rv = BeginSendTarget();
  if (rv != ERR_IO_PENDING) {
    OnSendTargetComplete(rv);
  }
}

void WebSocketTunnelSocket::OnConnectFailure(int error) {
  request_.reset();
  stream_.reset();
  Fail(error == OK || error == ERR_IO_PENDING ? ERR_CONNECTION_FAILED : error);
}

int WebSocketTunnelSocket::BeginSendTarget() {
  DCHECK(stream_);
  std::string host = target_.host();
  std::optional<IPAddress> address = IPAddress::FromIPLiteral(host);
  size_t address_size = 0;
  uint8_t address_type = kAddressTypeDomain;
  if (address.has_value()) {
    address_size = address->size();
    if (address->IsIPv4()) {
      address_type = kAddressTypeIPv4;
    } else if (address->IsIPv6()) {
      address_type = kAddressTypeIPv6;
    } else {
      address.reset();
    }
  }

  if (!address.has_value()) {
    address_type = kAddressTypeDomain;
    if (host.empty() || host.size() > kMaxDomainLength) {
      return ERR_ADDRESS_INVALID;
    }
  }

  size_t payload_size = 4 + address_size;
  auto payload = base::MakeRefCounted<IOBufferWithSize>(
      base::checked_cast<int>(payload_size));
  uint8_t* bytes = reinterpret_cast<uint8_t*>(payload->data());
  size_t offset = 0;
  bytes[offset++] = kTunnelProtocolVersion;
  bytes[offset++] = address_type;
  if (address.has_value()) {
    std::ranges::copy(address->bytes(), bytes + offset);
    offset += address_size;
  } else {
    bytes[offset++] = base::checked_cast<uint8_t>(host.size());
    std::memcpy(bytes + offset, host.data(), host.size());
    offset += host.size();
  }
  uint16_t port = base::checked_cast<uint16_t>(target_.port());
  bytes[offset++] = port >> 8;
  bytes[offset] = port & 0xff;

  auto frame = std::make_unique<WebSocketFrame>(
      WebSocketFrameHeader::kOpCodeBinary);
  frame->header.final = true;
  frame->header.masked = true;
  frame->header.payload_length = payload_size;
  frame->payload = payload->span();
  write_payload_ = std::move(payload);
  write_frames_.push_back(std::move(frame));

  state_ = State::kSendingTarget;
  return stream_->WriteFrames(
      &write_frames_,
      base::BindOnce(&WebSocketTunnelSocket::OnSendTargetComplete,
                     weak_ptr_factory_.GetWeakPtr()));
}

void WebSocketTunnelSocket::OnSendTargetComplete(int result) {
  write_frames_.clear();
  write_payload_.reset();
  if (result != OK) {
    stream_.reset();
    Fail(result);
    return;
  }
  state_ = State::kReadingStatus;
  int rv = BeginReadStatus();
  if (rv != ERR_IO_PENDING) {
    OnReadFramesComplete(rv);
  }
}

int WebSocketTunnelSocket::BeginReadStatus() {
  DCHECK(read_frames_.empty());
  return stream_->ReadFrames(&read_frames_, base::BindOnce(
      &WebSocketTunnelSocket::OnReadFramesComplete,
      weak_ptr_factory_.GetWeakPtr()));
}

void WebSocketTunnelSocket::OnReadFramesComplete(int result) {
  State previous_state = state_;
  if (result == ERR_CONNECTION_CLOSED) {
    stream_.reset();
    state_ = State::kDisconnected;
    if (previous_state == State::kReadingStatus) {
      Fail(ERR_TUNNEL_CONNECTION_FAILED);
    } else {
      read_pending_ = false;
      read_user_buffer_ = nullptr;
      read_user_buffer_len_ = 0;
      CompletionOnceCallback callback = std::move(read_callback_);
      read_callback_.Reset();
      std::move(callback).Run(result);
    }
    return;
  }
  if (result < 0) {
    stream_.reset();
    state_ = State::kDisconnected;
    if (previous_state == State::kReadingStatus) {
      Fail(result);
    } else {
      read_pending_ = false;
      read_user_buffer_ = nullptr;
      read_user_buffer_len_ = 0;
      CompletionOnceCallback callback = std::move(read_callback_);
      read_callback_.Reset();
      std::move(callback).Run(result);
    }
    return;
  }

  if (state_ == State::kReadingStatus) {
    uint8_t status = 0xff;
    bool got_status = false;
    for (const auto& frame : read_frames_) {
      if (FrameOpcode(*frame) != WebSocketFrameHeader::kOpCodeBinary ||
          frame->payload.empty()) {
        continue;
      }
      status = frame->payload[0];
      got_status = true;
      break;
    }
    read_frames_.clear();
    if (!got_status || status != 0) {
      stream_.reset();
      state_ = State::kDisconnected;
      Fail(ERR_TUNNEL_CONNECTION_FAILED);
      return;
    }
    state_ = State::kConnected;
    ever_used_ = true;
    CompleteConnect(OK);
    return;
  }

  if (!read_pending_) {
    read_frames_.clear();
    return;
  }

  if (state_ == State::kConnected) {
    for (const auto& frame : read_frames_) {
      if (FrameOpcode(*frame) == WebSocketFrameHeader::kOpCodePing) {
        MaybeSendPong(*frame);
      }
    }
  }
  if (!IsConnected()) {
    read_frames_.clear();
    read_pending_ = false;
    read_user_buffer_ = nullptr;
    read_user_buffer_len_ = 0;
    CompletionOnceCallback callback = std::move(read_callback_);
    read_callback_.Reset();
    std::move(callback).Run(ERR_CONNECTION_CLOSED);
    return;
  }

  int copied = CopyAvailableData();
  if (copied < 0) {
    read_frames_.clear();
    read_pending_ = false;
    read_user_buffer_ = nullptr;
    read_user_buffer_len_ = 0;
    stream_.reset();
    state_ = State::kDisconnected;
    CompletionOnceCallback callback = std::move(read_callback_);
    read_callback_.Reset();
    std::move(callback).Run(copied);
    return;
  }
  if (copied == 0) {
    read_frames_.clear();
    ReadWithBuffer();
    return;
  }

  while (!read_frames_.empty()) {
    const auto& frame = read_frames_.front();
    bool is_data_frame =
        FrameOpcode(*frame) == WebSocketFrameHeader::kOpCodeBinary ||
        FrameOpcode(*frame) == WebSocketFrameHeader::kOpCodeContinuation;
    if (!is_data_frame ||
        read_offset_ != frame->payload.size()) {
      if (is_data_frame) {
        read_offset_ = 0;
      }
      read_frames_.erase(read_frames_.begin());
      continue;
    }
    break;
  }

  CompletionOnceCallback callback = std::move(read_callback_);
  read_pending_ = false;
  read_user_buffer_ = nullptr;
  read_user_buffer_len_ = 0;
  read_callback_.Reset();
  std::move(callback).Run(copied);
}

int WebSocketTunnelSocket::ReadWithBuffer() {
  read_frames_.clear();
  int rv = stream_->ReadFrames(&read_frames_, base::BindOnce(
      &WebSocketTunnelSocket::OnReadFramesComplete,
      weak_ptr_factory_.GetWeakPtr()));
  if (rv != ERR_IO_PENDING) {
    OnReadFramesComplete(rv);
  }
  return ERR_IO_PENDING;
}

int WebSocketTunnelSocket::CopyAvailableData() {
  int copied = 0;
  for (const auto& frame : read_frames_) {
    if (FrameOpcode(*frame) == WebSocketFrameHeader::kOpCodeClose) {
      return ERR_CONNECTION_CLOSED;
    }
    if (FrameOpcode(*frame) != WebSocketFrameHeader::kOpCodeBinary &&
        FrameOpcode(*frame) != WebSocketFrameHeader::kOpCodeContinuation) {
      continue;
    }
    size_t payload_size = frame->payload.size();
    size_t available = payload_size > read_offset_
                           ? payload_size - read_offset_
                           : 0;
    if (available == 0) {
      read_offset_ = 0;
      continue;
    }
    size_t space = base::checked_cast<size_t>(read_user_buffer_len_ - copied);
    size_t to_copy = std::min(available, space);
    std::memcpy(read_user_buffer_->data() + copied,
                frame->payload.data() + read_offset_, to_copy);
    copied += base::checked_cast<int>(to_copy);
    read_offset_ += to_copy;
    if (read_offset_ == payload_size) {
      read_offset_ = 0;
    }
    if (copied == read_user_buffer_len_) {
      break;
    }
  }
  return copied;
}

void WebSocketTunnelSocket::OnWriteFramesComplete(int result) {
  DCHECK(write_pending_);
  int written = result == OK ? write_result_size_ : result;
  write_frames_.clear();
  write_payload_.reset();
  write_result_size_ = 0;
  write_pending_ = false;
  if (result != OK) {
    stream_.reset();
    state_ = State::kDisconnected;
  }
  ever_used_ = true;
  CompletionOnceCallback callback = std::move(write_callback_);
  write_callback_.Reset();
  std::move(callback).Run(written);
}

void WebSocketTunnelSocket::MaybeSendPong(const WebSocketFrame& ping_frame) {
  if (state_ != State::kConnected || !stream_ || control_write_pending_ ||
      write_pending_) {
    return;
  }

  size_t payload_size = ping_frame.payload.size();
  auto payload = base::MakeRefCounted<IOBufferWithSize>(
      base::checked_cast<int>(payload_size));
  if (payload_size > 0) {
    std::memcpy(payload->data(), ping_frame.payload.data(), payload_size);
  }
  auto frame =
      std::make_unique<WebSocketFrame>(WebSocketFrameHeader::kOpCodePong);
  frame->header.final = true;
  frame->header.masked = true;
  frame->header.payload_length = payload_size;
  frame->payload = payload->span();
  control_payload_ = std::move(payload);
  control_frames_.push_back(std::move(frame));
  control_write_pending_ = true;
  int rv = stream_->WriteFrames(&control_frames_, base::BindOnce(
      &WebSocketTunnelSocket::OnControlWriteComplete,
      weak_ptr_factory_.GetWeakPtr()));
  if (rv != ERR_IO_PENDING) {
    OnControlWriteComplete(rv);
  }
}

void WebSocketTunnelSocket::OnControlWriteComplete(int result) {
  DCHECK(control_write_pending_);
  control_write_pending_ = false;
  control_frames_.clear();
  control_payload_.reset();
  if (result != OK) {
    stream_.reset();
    state_ = State::kDisconnected;
    if (write_pending_) {
      OnWriteFramesComplete(result);
    }
    return;
  }
  if (user_write_queued_) {
    user_write_queued_ = false;
    BeginUserWrite();
  }
}

void WebSocketTunnelSocket::Fail(int error) {
  state_ = State::kDisconnected;
  CompleteConnect(error);
}

void WebSocketTunnelSocket::CompleteConnect(int error) {
  CompletionOnceCallback callback = std::move(connect_callback_);
  connect_callback_.Reset();
  std::move(callback).Run(error);
}

}  // namespace net
