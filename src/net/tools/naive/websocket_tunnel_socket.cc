#include "net/tools/naive/websocket_tunnel_socket.h"

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify the buffer arithmetic.
#pragma allow_unsafe_buffers
#endif

#include <algorithm>
#include <cstring>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/embedder_support/user_agent_utils.h"
#include "net/base/io_buffer.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/base/transport_info.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/storage_access_api/status.h"
#include "net/tools/naive/padding_utils.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_handshake_constants.h"
#include "net/websockets/websocket_handshake_response_info.h"
#include "net/websockets/websocket_stream.h"
#include "url/origin.h"

namespace net {
namespace {

constexpr uint8_t kVersion = 1;
constexpr uint8_t kIPv4 = 1;
constexpr uint8_t kDomain = 3;
constexpr uint8_t kIPv6 = 4;

bool IsData(const WebSocketFrame& frame) {
  return frame.header.opcode == WebSocketFrameHeader::kOpCodeBinary ||
         frame.header.opcode == WebSocketFrameHeader::kOpCodeContinuation;
}

}  // namespace

class WebSocketTunnelSocket::ConnectDelegate
    : public WebSocketStream::ConnectDelegate {
 public:
  explicit ConnectDelegate(base::WeakPtr<WebSocketTunnelSocket> socket)
      : socket_(std::move(socket)) {}

  void OnCreateRequest(URLRequest*) override {}
  int OnURLRequestConnected(URLRequest*, const TransportInfo& info,
                            CompletionOnceCallback) override {
    if (socket_) {
      socket_->peer_address_ = info.endpoint;
      socket_->protocol_ = info.negotiated_protocol;
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
  void OnFailure(const std::string&, int error, std::optional<int>) override {
    if (socket_) {
      socket_->OnConnectFailure(error);
    }
  }
  void OnStartOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeRequestInfo>) override {}
  void OnSSLCertificateError(
      std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks> callbacks,
      int error, const SSLInfo& info, bool) override {
    callbacks->CancelSSLRequest(error, &info);
  }
  int OnAuthRequired(const AuthChallengeInfo&,
                     scoped_refptr<HttpResponseHeaders>, const IPEndPoint&,
                     base::OnceCallback<void(const AuthCredentials*)>,
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
    const GURL& url, const AuthCredentials& credentials,
    const HostPortPair& target, URLRequestContext* context,
    const NetLogWithSource& net_log,
    const NetworkTrafficAnnotationTag& annotation)
    : url_(url), credentials_(credentials), target_(target), context_(context),
      net_log_(net_log), annotation_(annotation) {
  DCHECK(context_);
  DCHECK(url_.SchemeIs("ws") || url_.SchemeIs("wss"));
  DCHECK(!target_.IsEmpty());
}

WebSocketTunnelSocket::~WebSocketTunnelSocket() {
  Disconnect();
}

int WebSocketTunnelSocket::Connect(CompletionOnceCallback callback) {
  if (state_ == State::kConnected) return OK;
  if (state_ != State::kDisconnected) return ERR_UNEXPECTED;
  DCHECK(!connect_callback_ && !stream_ && !request_);
  connect_callback_ = std::move(callback);

  HttpRequestHeaders headers;
  headers.SetHeader(HttpRequestHeaders::kUserAgent,
                    embedder_support::GetUserAgent());
  headers.SetHeader(HttpRequestHeaders::kAcceptLanguage, "en-US,en;q=0.9");
  headers.SetHeader(HttpRequestHeaders::kCacheControl, "no-cache");
  headers.SetHeader(HttpRequestHeaders::kPragma, "no-cache");
  InitializeNonindexCodes();
  std::string padding(base::RandIntInclusive(16, 32), '~');
  FillNonindexHeaderValue(base::RandUint64(),
                          base::as_writable_byte_span(padding));
  headers.SetHeader(kPaddingHeader, padding);
  headers.SetHeader(kPaddingTypeRequestHeader, "1,0");
  if (!credentials_.Empty()) {
    headers.SetHeader("Authorization", "Basic " + base::Base64Encode(
        base::UTF16ToUTF8(credentials_.username()) + ":" +
        base::UTF16ToUTF8(credentials_.password())));
  }

  state_ = State::kConnecting;
  request_ = WebSocketStream::CreateAndConnectStream(
      url_, {}, url::Origin::Create(ChangeWebSocketSchemeToHttpScheme(url_)),
      StorageAccessApiStatus::kNone,
      IsolationInfo::CreateTransient(std::nullopt),
      headers, context_, net_log_, WebSocketPriorityHint::kDefault, annotation_,
      std::make_unique<ConnectDelegate>(weak_factory_.GetWeakPtr()));
  return ERR_IO_PENDING;
}

void WebSocketTunnelSocket::Disconnect() {
  request_.reset();
  if (stream_) {
    stream_->Close();
    stream_.reset();
  }
  state_ = State::kDisconnected;
  ever_used_ = read_pending_ = write_pending_ = false;
  pong_pending_ = pong_write_pending_ = user_write_queued_ = false;
  read_offset_ = write_size_ = read_buffer_len_ = 0;
  read_frames_.clear();
  write_frames_.clear();
  pong_frames_.clear();
  write_payload_.reset();
  pong_payload_.reset();
  read_buffer_ = nullptr;
  connect_callback_.Reset();
  read_callback_.Reset();
  write_callback_.Reset();
}

bool WebSocketTunnelSocket::IsConnected() const {
  return state_ == State::kConnected && stream_;
}

bool WebSocketTunnelSocket::IsConnectedAndIdle() const {
  return IsConnected() && !read_pending_ && !write_pending_ &&
         !pong_write_pending_;
}

int WebSocketTunnelSocket::GetPeerAddress(IPEndPoint* address) const {
  if (!peer_address_.address().IsValid()) return ERR_SOCKET_NOT_CONNECTED;
  *address = peer_address_;
  return OK;
}

int WebSocketTunnelSocket::GetLocalAddress(IPEndPoint*) const {
  return ERR_SOCKET_NOT_CONNECTED;
}

const NetLogWithSource& WebSocketTunnelSocket::NetLog() const {
  return net_log_;
}

bool WebSocketTunnelSocket::WasEverUsed() const { return ever_used_; }

NextProto WebSocketTunnelSocket::GetNegotiatedProtocol() const {
  return protocol_;
}

bool WebSocketTunnelSocket::GetSSLInfo(SSLInfo*) { return false; }

int64_t WebSocketTunnelSocket::GetTotalReceivedBytes() const { return 0; }

void WebSocketTunnelSocket::ApplySocketTag(const SocketTag&) {}

int WebSocketTunnelSocket::Read(IOBuffer* buffer, int length,
                                CompletionOnceCallback callback) {
  DCHECK(buffer && length > 0);
  if (!IsConnected()) return ERR_SOCKET_NOT_CONNECTED;
  DCHECK(!read_pending_ && read_callback_.is_null());
  read_buffer_ = buffer;
  read_buffer_len_ = length;
  read_pending_ = true;
  read_callback_ = std::move(callback);
  if (!read_frames_.empty()) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnReadFrames,
                        weak_factory_.GetWeakPtr(), OK));
    return ERR_IO_PENDING;
  }
  const int result = ReadFrames();
  if (result != ERR_IO_PENDING) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnReadFrames,
                        weak_factory_.GetWeakPtr(), result));
  }
  return ERR_IO_PENDING;
}

int WebSocketTunnelSocket::Write(IOBuffer* buffer, int length,
                                 CompletionOnceCallback callback,
                                 const NetworkTrafficAnnotationTag&) {
  DCHECK(buffer && length >= 0);
  if (!IsConnected()) return ERR_SOCKET_NOT_CONNECTED;
  if (length == 0) return 0;
  DCHECK(!write_pending_ && write_frames_.empty());
  ever_used_ = true;
  write_payload_ = base::MakeRefCounted<IOBufferWithSize>(length);
  std::memcpy(write_payload_->data(), buffer->data(),
              base::checked_cast<size_t>(length));
  auto frame = std::make_unique<WebSocketFrame>(
      WebSocketFrameHeader::kOpCodeBinary);
  frame->header.final = true;
  frame->header.masked = true;
  frame->header.payload_length = base::checked_cast<uint64_t>(length);
  frame->payload = write_payload_->first(base::checked_cast<size_t>(length));
  write_frames_.push_back(std::move(frame));
  write_size_ = length;
  write_pending_ = true;
  write_callback_ = std::move(callback);
  if (pong_write_pending_) {
    user_write_queued_ = true;
    return ERR_IO_PENDING;
  }
  return BeginWrite();
}

int WebSocketTunnelSocket::SetReceiveBufferSize(int32_t) { return OK; }

int WebSocketTunnelSocket::SetSendBufferSize(int32_t) { return OK; }

void WebSocketTunnelSocket::OnConnectSuccess(
    std::unique_ptr<WebSocketStream> stream,
    std::unique_ptr<WebSocketHandshakeResponseInfo> response) {
  if (state_ != State::kConnecting) return;
  if (!response || !response->headers ||
      response->headers->HasHeader(websockets::kSecWebSocketExtensions)) {
    Fail(ERR_INVALID_RESPONSE);
    return;
  }
  std::optional<PaddingType> negotiated =
      ParsePaddingHeaders(*response->headers);
  if (!negotiated) {
    Fail(ERR_INVALID_RESPONSE);
    return;
  }
  padding_type_ = *negotiated;
  stream_ = std::move(stream);
  request_.reset();
  SendTarget();
}

void WebSocketTunnelSocket::OnConnectFailure(int error) {
  request_.reset();
  stream_.reset();
  Fail(error == OK || error == ERR_IO_PENDING ? ERR_CONNECTION_FAILED : error);
}

void WebSocketTunnelSocket::SendTarget() {
  std::string host = target_.host();
  std::optional<IPAddress> address = IPAddress::FromIPLiteral(host);
  size_t address_size = 0;
  uint8_t type = kDomain;
  if (address) {
    address_size = address->size();
    type = address->IsIPv4() ? kIPv4 : kIPv6;
  } else if (host.empty() || host.size() > 255) {
    Fail(ERR_ADDRESS_INVALID);
    return;
  }
  const size_t size = address ? 4 + address_size : 5 + host.size();
  auto payload = base::MakeRefCounted<IOBufferWithSize>(
      base::checked_cast<int>(size));
  uint8_t* bytes = reinterpret_cast<uint8_t*>(payload->data());
  size_t offset = 0;
  bytes[offset++] = kVersion;
  bytes[offset++] = type;
  if (address) {
    std::ranges::copy(address->bytes(), bytes + offset);
    offset += address_size;
  } else {
    bytes[offset++] = base::checked_cast<uint8_t>(host.size());
    std::memcpy(bytes + offset, host.data(), host.size());
    offset += host.size();
  }
  const uint16_t port = base::checked_cast<uint16_t>(target_.port());
  bytes[offset++] = port >> 8;
  bytes[offset] = port & 0xff;
  auto frame = std::make_unique<WebSocketFrame>(
      WebSocketFrameHeader::kOpCodeBinary);
  frame->header.final = true;
  frame->header.masked = true;
  frame->header.payload_length = size;
  frame->payload = payload->span();
  write_payload_ = std::move(payload);
  write_frames_.push_back(std::move(frame));
  const int result = stream_->WriteFrames(
      &write_frames_, base::BindOnce(&WebSocketTunnelSocket::OnTargetSent,
                                     weak_factory_.GetWeakPtr()));
  if (result != ERR_IO_PENDING) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnTargetSent,
                        weak_factory_.GetWeakPtr(), result));
  }
}

void WebSocketTunnelSocket::OnTargetSent(int result) {
  write_frames_.clear();
  write_payload_.reset();
  if (result != OK) {
    stream_.reset();
    Fail(result);
    return;
  }
  const int read_result = ReadFrames();
  if (read_result != ERR_IO_PENDING) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnReadFrames,
                        weak_factory_.GetWeakPtr(), read_result));
  }
}

int WebSocketTunnelSocket::ReadFrames() {
  read_frames_.clear();
  return stream_->ReadFrames(
      &read_frames_, base::BindOnce(&WebSocketTunnelSocket::OnReadFrames,
                                    weak_factory_.GetWeakPtr()));
}

void WebSocketTunnelSocket::OnReadFrames(int result) {
  if (result < 0) {
    stream_.reset();
    state_ = State::kDisconnected;
    if (read_pending_) CompleteRead(result);
    else if (connect_callback_) Fail(ERR_TUNNEL_CONNECTION_FAILED);
    return;
  }
  if (connect_callback_) {
    auto it = std::find_if(read_frames_.begin(), read_frames_.end(),
                           [](const auto& frame) {
                             return frame->header.opcode ==
                                    WebSocketFrameHeader::kOpCodeBinary &&
                                    !frame->payload.empty();
                           });
    if (it == read_frames_.end() || (*it)->payload[0] != 0) {
      stream_.reset();
      Fail(ERR_TUNNEL_CONNECTION_FAILED);
      return;
    }
    read_frames_.erase(it);
    state_ = State::kConnected;
    ever_used_ = true;
    CompleteConnect(OK);
    return;
  }

  ProcessControlFrames();
  if (!IsConnected()) {
    if (read_pending_) CompleteRead(ERR_CONNECTION_CLOSED);
    return;
  }
  if (!read_pending_) {
    SendPong();
    return;
  }
  const int copied = CopyData();
  if (copied > 0) {
    CompleteRead(copied);
    return;
  }
  if (copied < 0) {
    stream_.reset();
    state_ = State::kDisconnected;
    CompleteRead(copied);
    return;
  }
  const int read_result = ReadFrames();
  if (read_result != ERR_IO_PENDING) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnReadFrames,
                        weak_factory_.GetWeakPtr(), read_result));
  }
}

int WebSocketTunnelSocket::CopyData() {
  int copied = 0;
  for (auto it = read_frames_.begin(); it != read_frames_.end();) {
    if (!IsData(**it)) {
      it = read_frames_.erase(it);
      continue;
    }
    const size_t available = (*it)->payload.size() - read_offset_;
    const size_t space = base::checked_cast<size_t>(read_buffer_len_ - copied);
    const size_t count = std::min(available, space);
    std::memcpy(read_buffer_->data() + copied,
                (*it)->payload.data() + read_offset_, count);
    copied += base::checked_cast<int>(count);
    read_offset_ += count;
    if (read_offset_ == (*it)->payload.size()) {
      it = read_frames_.erase(it);
      read_offset_ = 0;
    } else {
      // A partial frame can only remain when the caller's buffer is full.
      break;
    }
    if (copied == read_buffer_len_) break;
  }
  return copied;
}

void WebSocketTunnelSocket::ProcessControlFrames() {
  for (auto it = read_frames_.begin(); it != read_frames_.end();) {
    const auto opcode = (*it)->header.opcode;
    if (opcode == WebSocketFrameHeader::kOpCodePing) {
      pong_payload_ = base::MakeRefCounted<IOBufferWithSize>(
          base::checked_cast<int>((*it)->payload.size()));
      if (!(*it)->payload.empty()) {
        std::memcpy(pong_payload_->data(), (*it)->payload.data(),
                    (*it)->payload.size());
      }
      pong_pending_ = true;
      it = read_frames_.erase(it);
    } else if (opcode == WebSocketFrameHeader::kOpCodePong) {
      it = read_frames_.erase(it);
    } else if (opcode == WebSocketFrameHeader::kOpCodeClose) {
      stream_.reset();
      state_ = State::kDisconnected;
      return;
    } else {
      ++it;
    }
  }
}

int WebSocketTunnelSocket::BeginWrite() {
  const int result = stream_->WriteFrames(
      &write_frames_, base::BindOnce(&WebSocketTunnelSocket::OnWriteComplete,
                                     weak_factory_.GetWeakPtr()));
  if (result != ERR_IO_PENDING) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnWriteComplete,
                        weak_factory_.GetWeakPtr(), result));
  }
  return ERR_IO_PENDING;
}

void WebSocketTunnelSocket::OnWriteComplete(int result) {
  write_frames_.clear();
  write_payload_.reset();
  if (result != OK) {
    stream_.reset();
    state_ = State::kDisconnected;
    CompleteWrite(result);
    return;
  }
  const int written = write_size_;
  write_size_ = 0;
  write_pending_ = false;
  SendPong();
  CompleteWrite(written);
}

void WebSocketTunnelSocket::SendPong() {
  if (!pong_pending_ || !IsConnected() || write_pending_ ||
      pong_write_pending_) {
    return;
  }
  auto frame = std::make_unique<WebSocketFrame>(
      WebSocketFrameHeader::kOpCodePong);
  frame->header.final = true;
  frame->header.masked = true;
  frame->header.payload_length = pong_payload_ ? pong_payload_->size() : 0;
  if (pong_payload_) frame->payload = pong_payload_->span();
  pong_frames_.push_back(std::move(frame));
  pong_write_pending_ = true;
  pong_pending_ = false;
  const int result = stream_->WriteFrames(
      &pong_frames_, base::BindOnce(&WebSocketTunnelSocket::OnPongComplete,
                                    weak_factory_.GetWeakPtr()));
  if (result != ERR_IO_PENDING) {
    Post(base::BindOnce(&WebSocketTunnelSocket::OnPongComplete,
                        weak_factory_.GetWeakPtr(), result));
  }
}

void WebSocketTunnelSocket::OnPongComplete(int result) {
  pong_frames_.clear();
  pong_payload_.reset();
  pong_write_pending_ = false;
  if (result != OK) {
    stream_.reset();
    state_ = State::kDisconnected;
    if (write_pending_) CompleteWrite(result);
    else if (read_pending_) CompleteRead(result);
    return;
  }
  if (user_write_queued_) {
    user_write_queued_ = false;
    BeginWrite();
  }
}

void WebSocketTunnelSocket::Post(base::OnceClosure task) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, std::move(task));
}

void WebSocketTunnelSocket::CompleteConnect(int result) {
  CompletionOnceCallback callback = std::move(connect_callback_);
  connect_callback_.Reset();
  std::move(callback).Run(result);
}

void WebSocketTunnelSocket::CompleteRead(int result) {
  read_pending_ = false;
  read_buffer_ = nullptr;
  read_buffer_len_ = 0;
  CompletionOnceCallback callback = std::move(read_callback_);
  read_callback_.Reset();
  std::move(callback).Run(result);
}

void WebSocketTunnelSocket::CompleteWrite(int result) {
  write_pending_ = false;
  write_size_ = 0;
  CompletionOnceCallback callback = std::move(write_callback_);
  write_callback_.Reset();
  std::move(callback).Run(result);
}

void WebSocketTunnelSocket::Fail(int error) {
  state_ = State::kDisconnected;
  if (connect_callback_) {
    CompleteConnect(error);
  } else if (read_pending_) {
    CompleteRead(error);
  } else if (write_pending_) {
    CompleteWrite(error);
  }
}

}  // namespace net
