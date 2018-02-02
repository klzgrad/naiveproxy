// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_channel.h"

#include <limits.h>  // for INT_MAX
#include <stddef.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "base/big_endian.h"
#include "base/bind.h"
#include "base/containers/circular_deque.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "net/base/io_buffer.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/log/net_log_with_source.h"
#include "net/websockets/websocket_errors.h"
#include "net/websockets/websocket_event_interface.h"
#include "net/websockets/websocket_frame.h"
#include "net/websockets/websocket_handshake_request_info.h"
#include "net/websockets/websocket_handshake_response_info.h"
#include "net/websockets/websocket_handshake_stream_create_helper.h"
#include "net/websockets/websocket_stream.h"
#include "url/origin.h"

namespace net {

namespace {

using base::StreamingUtf8Validator;

const int kDefaultSendQuotaLowWaterMark = 1 << 16;
const int kDefaultSendQuotaHighWaterMark = 1 << 17;
const size_t kWebSocketCloseCodeLength = 2;
// Timeout for waiting for the server to acknowledge a closing handshake.
const int kClosingHandshakeTimeoutSeconds = 60;
// We wait for the server to close the underlying connection as recommended in
// https://tools.ietf.org/html/rfc6455#section-7.1.1
// We don't use 2MSL since there're server implementations that don't follow
// the recommendation and wait for the client to close the underlying
// connection. It leads to unnecessarily long time before CloseEvent
// invocation. We want to avoid this rather than strictly following the spec
// recommendation.
const int kUnderlyingConnectionCloseTimeoutSeconds = 2;

typedef WebSocketEventInterface::ChannelState ChannelState;
const ChannelState CHANNEL_ALIVE = WebSocketEventInterface::CHANNEL_ALIVE;
const ChannelState CHANNEL_DELETED = WebSocketEventInterface::CHANNEL_DELETED;

// Maximum close reason length = max control frame payload -
//                               status code length
//                             = 125 - 2
const size_t kMaximumCloseReasonLength = 125 - kWebSocketCloseCodeLength;

// Check a close status code for strict compliance with RFC6455. This is only
// used for close codes received from a renderer that we are intending to send
// out over the network. See ParseClose() for the restrictions on incoming close
// codes. The |code| parameter is type int for convenience of implementation;
// the real type is uint16_t. Code 1005 is treated specially; it cannot be set
// explicitly by Javascript but the renderer uses it to indicate we should send
// a Close frame with no payload.
bool IsStrictlyValidCloseStatusCode(int code) {
  static const int kInvalidRanges[] = {
      // [BAD, OK)
      0,    1000,   // 1000 is the first valid code
      1006, 1007,   // 1006 MUST NOT be set.
      1014, 3000,   // 1014 unassigned; 1015 up to 2999 are reserved.
      5000, 65536,  // Codes above 5000 are invalid.
  };
  const int* const kInvalidRangesEnd =
      kInvalidRanges + arraysize(kInvalidRanges);

  DCHECK_GE(code, 0);
  DCHECK_LT(code, 65536);
  const int* upper = std::upper_bound(kInvalidRanges, kInvalidRangesEnd, code);
  DCHECK_NE(kInvalidRangesEnd, upper);
  DCHECK_GT(upper, kInvalidRanges);
  DCHECK_GT(*upper, code);
  DCHECK_LE(*(upper - 1), code);
  return ((upper - kInvalidRanges) % 2) == 0;
}

// Sets |name| to the name of the frame type for the given |opcode|. Note that
// for all of Text, Binary and Continuation opcode, this method returns
// "Data frame".
void GetFrameTypeForOpcode(WebSocketFrameHeader::OpCode opcode,
                           std::string* name) {
  switch (opcode) {
    case WebSocketFrameHeader::kOpCodeText:    // fall-thru
    case WebSocketFrameHeader::kOpCodeBinary:  // fall-thru
    case WebSocketFrameHeader::kOpCodeContinuation:
      *name = "Data frame";
      break;

    case WebSocketFrameHeader::kOpCodePing:
      *name = "Ping";
      break;

    case WebSocketFrameHeader::kOpCodePong:
      *name = "Pong";
      break;

    case WebSocketFrameHeader::kOpCodeClose:
      *name = "Close";
      break;

    default:
      *name = "Unknown frame type";
      break;
  }

  return;
}

class DependentIOBuffer : public WrappedIOBuffer {
 public:
  DependentIOBuffer(scoped_refptr<IOBuffer> buffer, size_t offset)
      : WrappedIOBuffer(buffer->data() + offset), buffer_(std::move(buffer)) {}

 private:
  ~DependentIOBuffer() override = default;
  scoped_refptr<net::IOBuffer> buffer_;
};

}  // namespace

// A class to encapsulate a set of frames and information about the size of
// those frames.
class WebSocketChannel::SendBuffer {
 public:
  SendBuffer() : total_bytes_(0) {}

  // Add a WebSocketFrame to the buffer and increase total_bytes_.
  void AddFrame(std::unique_ptr<WebSocketFrame> chunk);

  // Return a pointer to the frames_ for write purposes.
  std::vector<std::unique_ptr<WebSocketFrame>>* frames() { return &frames_; }

 private:
  // The frames_ that will be sent in the next call to WriteFrames().
  std::vector<std::unique_ptr<WebSocketFrame>> frames_;

  // The total size of the payload data in |frames_|. This will be used to
  // measure the throughput of the link.
  // TODO(ricea): Measure the throughput of the link.
  uint64_t total_bytes_;
};

void WebSocketChannel::SendBuffer::AddFrame(
    std::unique_ptr<WebSocketFrame> frame) {
  total_bytes_ += frame->header.payload_length;
  frames_.push_back(std::move(frame));
}

// Implementation of WebSocketStream::ConnectDelegate that simply forwards the
// calls on to the WebSocketChannel that created it.
class WebSocketChannel::ConnectDelegate
    : public WebSocketStream::ConnectDelegate {
 public:
  explicit ConnectDelegate(WebSocketChannel* creator) : creator_(creator) {}

  void OnCreateRequest(net::URLRequest* request) override {
    creator_->OnCreateURLRequest(request);
  }

  void OnSuccess(std::unique_ptr<WebSocketStream> stream) override {
    creator_->OnConnectSuccess(std::move(stream));
    // |this| may have been deleted.
  }

  void OnFailure(const std::string& message) override {
    creator_->OnConnectFailure(message);
    // |this| has been deleted.
  }

  void OnStartOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeRequestInfo> request) override {
    creator_->OnStartOpeningHandshake(std::move(request));
  }

  void OnFinishOpeningHandshake(
      std::unique_ptr<WebSocketHandshakeResponseInfo> response) override {
    creator_->OnFinishOpeningHandshake(std::move(response));
  }

  void OnSSLCertificateError(
      std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks>
          ssl_error_callbacks,
      const SSLInfo& ssl_info,
      bool fatal) override {
    creator_->OnSSLCertificateError(std::move(ssl_error_callbacks), ssl_info,
                                    fatal);
  }

 private:
  // A pointer to the WebSocketChannel that created this object. There is no
  // danger of this pointer being stale, because deleting the WebSocketChannel
  // cancels the connect process, deleting this object and preventing its
  // callbacks from being called.
  WebSocketChannel* const creator_;

  DISALLOW_COPY_AND_ASSIGN(ConnectDelegate);
};

class WebSocketChannel::HandshakeNotificationSender
    : public base::SupportsWeakPtr<HandshakeNotificationSender> {
 public:
  explicit HandshakeNotificationSender(WebSocketChannel* channel);
  ~HandshakeNotificationSender();

  static void Send(base::WeakPtr<HandshakeNotificationSender> sender);

  ChannelState SendImmediately(WebSocketEventInterface* event_interface);

  const WebSocketHandshakeRequestInfo* handshake_request_info() const {
    return handshake_request_info_.get();
  }

  void set_handshake_request_info(
      std::unique_ptr<WebSocketHandshakeRequestInfo> request_info) {
    handshake_request_info_ = std::move(request_info);
  }

  const WebSocketHandshakeResponseInfo* handshake_response_info() const {
    return handshake_response_info_.get();
  }

  void set_handshake_response_info(
      std::unique_ptr<WebSocketHandshakeResponseInfo> response_info) {
    handshake_response_info_ = std::move(response_info);
  }

 private:
  WebSocketChannel* owner_;
  std::unique_ptr<WebSocketHandshakeRequestInfo> handshake_request_info_;
  std::unique_ptr<WebSocketHandshakeResponseInfo> handshake_response_info_;
};

class WebSocketChannel::PendingReceivedFrame {
 public:
  PendingReceivedFrame(bool final,
                       WebSocketFrameHeader::OpCode opcode,
                       scoped_refptr<IOBuffer> data,
                       uint64_t offset,
                       uint64_t size)
      : final_(final),
        opcode_(opcode),
        data_(std::move(data)),
        offset_(offset),
        size_(size) {}
  PendingReceivedFrame(const PendingReceivedFrame& other) = default;
  ~PendingReceivedFrame() = default;

  bool final() const { return final_; }
  WebSocketFrameHeader::OpCode opcode() const { return opcode_; }

  // ResetOpcode() to Continuation.
  void ResetOpcode() {
    DCHECK(WebSocketFrameHeader::IsKnownDataOpCode(opcode_));
    opcode_ = WebSocketFrameHeader::kOpCodeContinuation;
  }
  const scoped_refptr<IOBuffer>& data() const { return data_; }
  uint64_t offset() const { return offset_; }
  uint64_t size() const { return size_; }

  // Increase |offset_| by |bytes|.
  void DidConsume(uint64_t bytes) {
    DCHECK_LE(offset_, size_);
    DCHECK_LE(bytes, size_ - offset_);
    offset_ += bytes;
  }

  // This object needs to be copyable and assignable, since it will be placed
  // in a base::queue. The compiler-generated copy constructor and assignment
  // operator will do the right thing.

 private:
  bool final_;
  WebSocketFrameHeader::OpCode opcode_;
  scoped_refptr<IOBuffer> data_;
  // Where to start reading from data_. Everything prior to offset_ has
  // already been sent to the browser.
  uint64_t offset_;
  // The size of data_.
  uint64_t size_;
};

WebSocketChannel::HandshakeNotificationSender::HandshakeNotificationSender(
    WebSocketChannel* channel)
    : owner_(channel) {}

WebSocketChannel::HandshakeNotificationSender::~HandshakeNotificationSender() =
    default;

void WebSocketChannel::HandshakeNotificationSender::Send(
    base::WeakPtr<HandshakeNotificationSender> sender) {
  // Do nothing if |sender| is already destructed.
  if (sender) {
    WebSocketChannel* channel = sender->owner_;
    sender->SendImmediately(channel->event_interface_.get());
  }
}

ChannelState WebSocketChannel::HandshakeNotificationSender::SendImmediately(
    WebSocketEventInterface* event_interface) {

  if (handshake_request_info_.get()) {
    if (CHANNEL_DELETED ==
        event_interface->OnStartOpeningHandshake(
            std::move(handshake_request_info_)))
      return CHANNEL_DELETED;
  }

  if (handshake_response_info_.get()) {
    if (CHANNEL_DELETED ==
        event_interface->OnFinishOpeningHandshake(
            std::move(handshake_response_info_)))
      return CHANNEL_DELETED;

    // TODO(yhirano): We can release |this| to save memory because
    // there will be no more opening handshake notification.
  }

  return CHANNEL_ALIVE;
}

WebSocketChannel::WebSocketChannel(
    std::unique_ptr<WebSocketEventInterface> event_interface,
    URLRequestContext* url_request_context)
    : event_interface_(std::move(event_interface)),
      url_request_context_(url_request_context),
      send_quota_low_water_mark_(kDefaultSendQuotaLowWaterMark),
      send_quota_high_water_mark_(kDefaultSendQuotaHighWaterMark),
      current_send_quota_(0),
      current_receive_quota_(0),
      closing_handshake_timeout_(
          base::TimeDelta::FromSeconds(kClosingHandshakeTimeoutSeconds)),
      underlying_connection_close_timeout_(base::TimeDelta::FromSeconds(
          kUnderlyingConnectionCloseTimeoutSeconds)),
      has_received_close_frame_(false),
      received_close_code_(0),
      state_(FRESHLY_CONSTRUCTED),
      notification_sender_(new HandshakeNotificationSender(this)),
      sending_text_message_(false),
      receiving_text_message_(false),
      expecting_to_handle_continuation_(false),
      initial_frame_forwarded_(false) {}

WebSocketChannel::~WebSocketChannel() {
  // The stream may hold a pointer to read_frames_, and so it needs to be
  // destroyed first.
  stream_.reset();
  // The timer may have a callback pointing back to us, so stop it just in case
  // someone decides to run the event loop from their destructor.
  close_timer_.Stop();
}

void WebSocketChannel::SendAddChannelRequest(
    const GURL& socket_url,
    const std::vector<std::string>& requested_subprotocols,
    const url::Origin& origin,
    const GURL& site_for_cookies,
    const std::string& additional_headers) {
  SendAddChannelRequestWithSuppliedCallback(
      socket_url, requested_subprotocols, origin, site_for_cookies,
      additional_headers, base::Bind(&WebSocketStream::CreateAndConnectStream));
}

void WebSocketChannel::SetState(State new_state) {
  DCHECK_NE(state_, new_state);

  if (new_state == CONNECTED)
    established_on_ = base::TimeTicks::Now();
  if (state_ == CONNECTED && !established_on_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES(
        "Net.WebSocket.Duration", base::TimeTicks::Now() - established_on_);
  }

  state_ = new_state;
}

bool WebSocketChannel::InClosingState() const {
  // The state RECV_CLOSED is not supported here, because it is only used in one
  // code path and should not leak into the code in general.
  DCHECK_NE(RECV_CLOSED, state_)
      << "InClosingState called with state_ == RECV_CLOSED";
  return state_ == SEND_CLOSED || state_ == CLOSE_WAIT || state_ == CLOSED;
}

WebSocketChannel::ChannelState WebSocketChannel::SendFrame(
    bool fin,
    WebSocketFrameHeader::OpCode op_code,
    scoped_refptr<IOBuffer> buffer,
    size_t buffer_size) {
  if (buffer_size > INT_MAX) {
    NOTREACHED() << "Frame size sanity check failed";
    return CHANNEL_ALIVE;
  }
  if (stream_ == NULL) {
    LOG(DFATAL) << "Got SendFrame without a connection established; "
                << "misbehaving renderer? fin=" << fin << " op_code=" << op_code
                << " buffer_size=" << buffer_size;
    return CHANNEL_ALIVE;
  }
  if (InClosingState()) {
    DVLOG(1) << "SendFrame called in state " << state_
             << ". This may be a bug, or a harmless race.";
    return CHANNEL_ALIVE;
  }
  if (state_ != CONNECTED) {
    NOTREACHED() << "SendFrame() called in state " << state_;
    return CHANNEL_ALIVE;
  }
  if (buffer_size > base::checked_cast<size_t>(current_send_quota_)) {
    // TODO(ricea): Kill renderer.
    return FailChannel("Send quota exceeded", kWebSocketErrorGoingAway, "");
    // |this| has been deleted.
  }
  if (!WebSocketFrameHeader::IsKnownDataOpCode(op_code)) {
    LOG(DFATAL) << "Got SendFrame with bogus op_code " << op_code
                << "; misbehaving renderer? fin=" << fin
                << " buffer_size=" << buffer_size;
    return CHANNEL_ALIVE;
  }
  if (op_code == WebSocketFrameHeader::kOpCodeText ||
      (op_code == WebSocketFrameHeader::kOpCodeContinuation &&
       sending_text_message_)) {
    StreamingUtf8Validator::State state =
        outgoing_utf8_validator_.AddBytes(buffer->data(), buffer_size);
    if (state == StreamingUtf8Validator::INVALID ||
        (state == StreamingUtf8Validator::VALID_MIDPOINT && fin)) {
      // TODO(ricea): Kill renderer.
      return FailChannel("Browser sent a text frame containing invalid UTF-8",
                         kWebSocketErrorGoingAway, "");
      // |this| has been deleted.
    }
    sending_text_message_ = !fin;
    DCHECK(!fin || state == StreamingUtf8Validator::VALID_ENDPOINT);
  }
  current_send_quota_ -= buffer_size;
  // TODO(ricea): If current_send_quota_ has dropped below
  // send_quota_low_water_mark_, it might be good to increase the "low
  // water mark" and "high water mark", but only if the link to the WebSocket
  // server is not saturated.
  return SendFrameInternal(fin, op_code, std::move(buffer), buffer_size);
  // |this| may have been deleted.
}

ChannelState WebSocketChannel::SendFlowControl(int64_t quota) {
  DCHECK(state_ == CONNECTING || state_ == CONNECTED || state_ == SEND_CLOSED ||
         state_ == CLOSE_WAIT);
  // TODO(ricea): Kill the renderer if it tries to send us a negative quota
  // value or > INT_MAX.
  DCHECK_GE(quota, 0);
  DCHECK_LE(quota, INT_MAX);
  if (!pending_received_frames_.empty()) {
    DCHECK_EQ(0u, current_receive_quota_);
  }
  while (!pending_received_frames_.empty() && quota > 0) {
    PendingReceivedFrame& front = pending_received_frames_.front();
    const uint64_t data_size = front.size() - front.offset();
    const uint64_t bytes_to_send =
        std::min(base::checked_cast<uint64_t>(quota), data_size);
    const bool final = front.final() && data_size == bytes_to_send;
    scoped_refptr<IOBuffer> buffer_to_pass;
    if (front.data()) {
      buffer_to_pass = new DependentIOBuffer(front.data(), front.offset());
    } else {
      DCHECK(!bytes_to_send) << "Non empty data should not be null.";
    }
    DVLOG(3) << "Sending frame previously split due to quota to the "
             << "renderer: quota=" << quota << " data_size=" << data_size
             << " bytes_to_send=" << bytes_to_send;
    if (event_interface_->OnDataFrame(final, front.opcode(),
                                      std::move(buffer_to_pass),
                                      bytes_to_send) == CHANNEL_DELETED)
      return CHANNEL_DELETED;
    if (bytes_to_send < data_size) {
      front.DidConsume(bytes_to_send);
      front.ResetOpcode();
      return CHANNEL_ALIVE;
    }
    quota -= bytes_to_send;

    pending_received_frames_.pop();
  }
  if (!InClosingState() && pending_received_frames_.empty() &&
      has_received_close_frame_) {
    // We've been waiting for the client to consume the frames before
    // responding to the closing handshake initiated by the server.
    return RespondToClosingHandshake();
  }

  // If current_receive_quota_ == 0 then there is no pending ReadFrames()
  // operation.
  const bool start_read =
      current_receive_quota_ == 0 && quota > 0 &&
      (state_ == CONNECTED || state_ == SEND_CLOSED || state_ == CLOSE_WAIT);
  current_receive_quota_ += quota;
  if (start_read)
    return ReadFrames();
  return CHANNEL_ALIVE;
}

ChannelState WebSocketChannel::StartClosingHandshake(
    uint16_t code,
    const std::string& reason) {
  if (InClosingState()) {
    // When the associated renderer process is killed while the channel is in
    // CLOSING state we reach here.
    DVLOG(1) << "StartClosingHandshake called in state " << state_
             << ". This may be a bug, or a harmless race.";
    return CHANNEL_ALIVE;
  }
  if (has_received_close_frame_) {
    // We reach here if the client wants to start a closing handshake while
    // the browser is waiting for the client to consume incoming data frames
    // before responding to a closing handshake initiated by the server.
    // As the client doesn't want the data frames any more, we can respond to
    // the closing handshake initiated by the server.
    return RespondToClosingHandshake();
  }
  if (state_ == CONNECTING) {
    // Abort the in-progress handshake and drop the connection immediately.
    stream_request_.reset();
    SetState(CLOSED);
    return DoDropChannel(false, kWebSocketErrorAbnormalClosure, "");
  }
  if (state_ != CONNECTED) {
    NOTREACHED() << "StartClosingHandshake() called in state " << state_;
    return CHANNEL_ALIVE;
  }

  DCHECK(!close_timer_.IsRunning());
  // This use of base::Unretained() is safe because we stop the timer in the
  // destructor.
  close_timer_.Start(
      FROM_HERE,
      closing_handshake_timeout_,
      base::Bind(&WebSocketChannel::CloseTimeout, base::Unretained(this)));

  // Javascript actually only permits 1000 and 3000-4999, but the implementation
  // itself may produce different codes. The length of |reason| is also checked
  // by Javascript.
  if (!IsStrictlyValidCloseStatusCode(code) ||
      reason.size() > kMaximumCloseReasonLength) {
    // "InternalServerError" is actually used for errors from any endpoint, per
    // errata 3227 to RFC6455. If the renderer is sending us an invalid code or
    // reason it must be malfunctioning in some way, and based on that we
    // interpret this as an internal error.
    if (SendClose(kWebSocketErrorInternalServerError, "") == CHANNEL_DELETED)
      return CHANNEL_DELETED;
    DCHECK_EQ(CONNECTED, state_);
    SetState(SEND_CLOSED);
    return CHANNEL_ALIVE;
  }
  if (SendClose(
          code,
          StreamingUtf8Validator::Validate(reason) ? reason : std::string()) ==
      CHANNEL_DELETED)
    return CHANNEL_DELETED;
  DCHECK_EQ(CONNECTED, state_);
  SetState(SEND_CLOSED);
  return CHANNEL_ALIVE;
}

void WebSocketChannel::SendAddChannelRequestForTesting(
    const GURL& socket_url,
    const std::vector<std::string>& requested_subprotocols,
    const url::Origin& origin,
    const GURL& site_for_cookies,
    const std::string& additional_headers,
    const WebSocketStreamRequestCreationCallback& callback) {
  SendAddChannelRequestWithSuppliedCallback(socket_url, requested_subprotocols,
                                            origin, site_for_cookies,
                                            additional_headers, callback);
}

void WebSocketChannel::SetClosingHandshakeTimeoutForTesting(
    base::TimeDelta delay) {
  closing_handshake_timeout_ = delay;
}

void WebSocketChannel::SetUnderlyingConnectionCloseTimeoutForTesting(
    base::TimeDelta delay) {
  underlying_connection_close_timeout_ = delay;
}

void WebSocketChannel::SendAddChannelRequestWithSuppliedCallback(
    const GURL& socket_url,
    const std::vector<std::string>& requested_subprotocols,
    const url::Origin& origin,
    const GURL& site_for_cookies,
    const std::string& additional_headers,
    const WebSocketStreamRequestCreationCallback& callback) {
  DCHECK_EQ(FRESHLY_CONSTRUCTED, state_);
  if (!socket_url.SchemeIsWSOrWSS()) {
    // TODO(ricea): Kill the renderer (this error should have been caught by
    // Javascript).
    ignore_result(event_interface_->OnFailChannel("Invalid scheme"));
    // |this| is deleted here.
    return;
  }
  socket_url_ = socket_url;
  std::unique_ptr<WebSocketStream::ConnectDelegate> connect_delegate(
      new ConnectDelegate(this));
  std::unique_ptr<WebSocketHandshakeStreamCreateHelper> create_helper(
      new WebSocketHandshakeStreamCreateHelper(connect_delegate.get(),
                                               requested_subprotocols));
  stream_request_ =
      callback.Run(socket_url_, std::move(create_helper), origin,
                   site_for_cookies, additional_headers, url_request_context_,
                   NetLogWithSource(), std::move(connect_delegate));
  SetState(CONNECTING);
}

void WebSocketChannel::OnCreateURLRequest(URLRequest* request) {
  event_interface_->OnCreateURLRequest(request);
}

void WebSocketChannel::OnConnectSuccess(
    std::unique_ptr<WebSocketStream> stream) {
  DCHECK(stream);
  DCHECK_EQ(CONNECTING, state_);

  stream_ = std::move(stream);

  SetState(CONNECTED);

  if (event_interface_->OnAddChannelResponse(stream_->GetSubProtocol(),
                                             stream_->GetExtensions()) ==
      CHANNEL_DELETED)
    return;

  // TODO(ricea): Get flow control information from the WebSocketStream once we
  // have a multiplexing WebSocketStream.
  current_send_quota_ = send_quota_high_water_mark_;
  if (event_interface_->OnFlowControl(send_quota_high_water_mark_) ==
      CHANNEL_DELETED)
    return;

  // |stream_request_| is not used once the connection has succeeded.
  stream_request_.reset();

  ignore_result(ReadFrames());
  // |this| may have been deleted.
}

void WebSocketChannel::OnConnectFailure(const std::string& message) {
  DCHECK_EQ(CONNECTING, state_);

  // Copy the message before we delete its owner.
  std::string message_copy = message;

  SetState(CLOSED);
  stream_request_.reset();

  if (CHANNEL_DELETED ==
      notification_sender_->SendImmediately(event_interface_.get())) {
    // |this| has been deleted.
    return;
  }
  ChannelState result = event_interface_->OnFailChannel(message_copy);
  DCHECK_EQ(CHANNEL_DELETED, result);
  // |this| has been deleted.
}

void WebSocketChannel::OnSSLCertificateError(
    std::unique_ptr<WebSocketEventInterface::SSLErrorCallbacks>
        ssl_error_callbacks,
    const SSLInfo& ssl_info,
    bool fatal) {
  ignore_result(event_interface_->OnSSLCertificateError(
      std::move(ssl_error_callbacks), socket_url_, ssl_info, fatal));
}

void WebSocketChannel::OnStartOpeningHandshake(
    std::unique_ptr<WebSocketHandshakeRequestInfo> request) {
  DCHECK(!notification_sender_->handshake_request_info());

  // Because it is hard to handle an IPC error synchronously is difficult,
  // we asynchronously notify the information.
  notification_sender_->set_handshake_request_info(std::move(request));
  ScheduleOpeningHandshakeNotification();
}

void WebSocketChannel::OnFinishOpeningHandshake(
    std::unique_ptr<WebSocketHandshakeResponseInfo> response) {
  DCHECK(!notification_sender_->handshake_response_info());

  // Because it is hard to handle an IPC error synchronously is difficult,
  // we asynchronously notify the information.
  notification_sender_->set_handshake_response_info(std::move(response));
  ScheduleOpeningHandshakeNotification();
}

void WebSocketChannel::ScheduleOpeningHandshakeNotification() {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(HandshakeNotificationSender::Send,
                            notification_sender_->AsWeakPtr()));
}

ChannelState WebSocketChannel::WriteFrames() {
  int result = OK;
  do {
    // This use of base::Unretained is safe because this object owns the
    // WebSocketStream and destroying it cancels all callbacks.
    result = stream_->WriteFrames(
        data_being_sent_->frames(),
        base::Bind(base::IgnoreResult(&WebSocketChannel::OnWriteDone),
                   base::Unretained(this),
                   false));
    if (result != ERR_IO_PENDING) {
      if (OnWriteDone(true, result) == CHANNEL_DELETED)
        return CHANNEL_DELETED;
      // OnWriteDone() returns CHANNEL_DELETED on error. Here |state_| is
      // guaranteed to be the same as before OnWriteDone() call.
    }
  } while (result == OK && data_being_sent_);
  return CHANNEL_ALIVE;
}

ChannelState WebSocketChannel::OnWriteDone(bool synchronous, int result) {
  DCHECK_NE(FRESHLY_CONSTRUCTED, state_);
  DCHECK_NE(CONNECTING, state_);
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(data_being_sent_);
  switch (result) {
    case OK:
      if (data_to_send_next_) {
        data_being_sent_ = std::move(data_to_send_next_);
        if (!synchronous)
          return WriteFrames();
      } else {
        data_being_sent_.reset();
        if (current_send_quota_ < send_quota_low_water_mark_) {
          // TODO(ricea): Increase low_water_mark and high_water_mark if
          // throughput is high, reduce them if throughput is low.  Low water
          // mark needs to be >= the bandwidth delay product *of the IPC
          // channel*. Because factors like context-switch time, thread wake-up
          // time, and bus speed come into play it is complex and probably needs
          // to be determined empirically.
          DCHECK_LE(send_quota_low_water_mark_, send_quota_high_water_mark_);
          // TODO(ricea): Truncate quota by the quota specified by the remote
          // server, if the protocol in use supports quota.
          int fresh_quota = send_quota_high_water_mark_ - current_send_quota_;
          current_send_quota_ += fresh_quota;
          return event_interface_->OnFlowControl(fresh_quota);
        }
      }
      return CHANNEL_ALIVE;

    // If a recoverable error condition existed, it would go here.

    default:
      DCHECK_LT(result, 0)
          << "WriteFrames() should only return OK or ERR_ codes";

      stream_->Close();
      SetState(CLOSED);
      return DoDropChannel(false, kWebSocketErrorAbnormalClosure, "");
  }
}

ChannelState WebSocketChannel::ReadFrames() {
  int result = OK;
  while (result == OK && current_receive_quota_ > 0) {
    // This use of base::Unretained is safe because this object owns the
    // WebSocketStream, and any pending reads will be cancelled when it is
    // destroyed.
    result = stream_->ReadFrames(
        &read_frames_,
        base::Bind(base::IgnoreResult(&WebSocketChannel::OnReadDone),
                   base::Unretained(this),
                   false));
    if (result != ERR_IO_PENDING) {
      if (OnReadDone(true, result) == CHANNEL_DELETED)
        return CHANNEL_DELETED;
    }
    DCHECK_NE(CLOSED, state_);
  }
  return CHANNEL_ALIVE;
}

ChannelState WebSocketChannel::OnReadDone(bool synchronous, int result) {
  DCHECK_NE(FRESHLY_CONSTRUCTED, state_);
  DCHECK_NE(CONNECTING, state_);
  DCHECK_NE(ERR_IO_PENDING, result);
  switch (result) {
    case OK:
      // ReadFrames() must use ERR_CONNECTION_CLOSED for a closed connection
      // with no data read, not an empty response.
      DCHECK(!read_frames_.empty())
          << "ReadFrames() returned OK, but nothing was read.";
      for (size_t i = 0; i < read_frames_.size(); ++i) {
        if (HandleFrame(std::move(read_frames_[i])) == CHANNEL_DELETED)
          return CHANNEL_DELETED;
      }
      read_frames_.clear();
      // There should always be a call to ReadFrames pending.
      // TODO(ricea): Unless we are out of quota.
      DCHECK_NE(CLOSED, state_);
      if (!synchronous)
        return ReadFrames();
      return CHANNEL_ALIVE;

    case ERR_WS_PROTOCOL_ERROR:
      // This could be kWebSocketErrorProtocolError (specifically, non-minimal
      // encoding of payload length) or kWebSocketErrorMessageTooBig, or an
      // extension-specific error.
      return FailChannel("Invalid frame header",
                         kWebSocketErrorProtocolError,
                         "WebSocket Protocol Error");

    default:
      DCHECK_LT(result, 0)
          << "ReadFrames() should only return OK or ERR_ codes";

      stream_->Close();
      SetState(CLOSED);

      uint16_t code = kWebSocketErrorAbnormalClosure;
      std::string reason = "";
      bool was_clean = false;
      if (has_received_close_frame_) {
        code = received_close_code_;
        reason = received_close_reason_;
        was_clean = (result == ERR_CONNECTION_CLOSED);
      }

      return DoDropChannel(was_clean, code, reason);
  }
}

ChannelState WebSocketChannel::HandleFrame(
    std::unique_ptr<WebSocketFrame> frame) {
  if (frame->header.masked) {
    // RFC6455 Section 5.1 "A client MUST close a connection if it detects a
    // masked frame."
    return FailChannel(
        "A server must not mask any frames that it sends to the "
        "client.",
        kWebSocketErrorProtocolError,
        "Masked frame from server");
  }
  const WebSocketFrameHeader::OpCode opcode = frame->header.opcode;
  DCHECK(!WebSocketFrameHeader::IsKnownControlOpCode(opcode) ||
         frame->header.final);
  if (frame->header.reserved1 || frame->header.reserved2 ||
      frame->header.reserved3) {
    return FailChannel(base::StringPrintf(
                           "One or more reserved bits are on: reserved1 = %d, "
                           "reserved2 = %d, reserved3 = %d",
                           static_cast<int>(frame->header.reserved1),
                           static_cast<int>(frame->header.reserved2),
                           static_cast<int>(frame->header.reserved3)),
                       kWebSocketErrorProtocolError,
                       "Invalid reserved bit");
  }

  // Respond to the frame appropriately to its type.
  return HandleFrameByState(opcode, frame->header.final, std::move(frame->data),
                            frame->header.payload_length);
}

ChannelState WebSocketChannel::HandleFrameByState(
    const WebSocketFrameHeader::OpCode opcode,
    bool final,
    scoped_refptr<IOBuffer> data_buffer,
    uint64_t size) {
  DCHECK_NE(RECV_CLOSED, state_)
      << "HandleFrame() does not support being called re-entrantly from within "
         "SendClose()";
  DCHECK_NE(CLOSED, state_);
  if (state_ == CLOSE_WAIT) {
    std::string frame_name;
    GetFrameTypeForOpcode(opcode, &frame_name);

    // FailChannel() won't send another Close frame.
    return FailChannel(
        frame_name + " received after close", kWebSocketErrorProtocolError, "");
  }
  switch (opcode) {
    case WebSocketFrameHeader::kOpCodeText:  // fall-thru
    case WebSocketFrameHeader::kOpCodeBinary:
    case WebSocketFrameHeader::kOpCodeContinuation:
      return HandleDataFrame(opcode, final, std::move(data_buffer), size);

    case WebSocketFrameHeader::kOpCodePing:
      DVLOG(1) << "Got Ping of size " << size;
      if (state_ == CONNECTED)
        return SendFrameInternal(true, WebSocketFrameHeader::kOpCodePong,
                                 std::move(data_buffer), size);
      DVLOG(3) << "Ignored ping in state " << state_;
      return CHANNEL_ALIVE;

    case WebSocketFrameHeader::kOpCodePong:
      DVLOG(1) << "Got Pong of size " << size;
      // There is no need to do anything with pong messages.
      return CHANNEL_ALIVE;

    case WebSocketFrameHeader::kOpCodeClose: {
      uint16_t code = kWebSocketNormalClosure;
      std::string reason;
      std::string message;
      if (!ParseClose(std::move(data_buffer), size, &code, &reason, &message)) {
        return FailChannel(message, code, reason);
      }
      // TODO(ricea): Find a way to safely log the message from the close
      // message (escape control codes and so on).
      DVLOG(1) << "Got Close with code " << code;
      return HandleCloseFrame(code, reason);
    }

    default:
      return FailChannel(
          base::StringPrintf("Unrecognized frame opcode: %d", opcode),
          kWebSocketErrorProtocolError,
          "Unknown opcode");
  }
}

ChannelState WebSocketChannel::HandleDataFrame(
    WebSocketFrameHeader::OpCode opcode,
    bool final,
    scoped_refptr<IOBuffer> data_buffer,
    uint64_t size) {
  if (state_ != CONNECTED) {
    DVLOG(3) << "Ignored data packet received in state " << state_;
    return CHANNEL_ALIVE;
  }
  if (has_received_close_frame_) {
    DVLOG(3) << "Ignored data packet as we've received a close frame.";
    return CHANNEL_ALIVE;
  }
  DCHECK(opcode == WebSocketFrameHeader::kOpCodeContinuation ||
         opcode == WebSocketFrameHeader::kOpCodeText ||
         opcode == WebSocketFrameHeader::kOpCodeBinary);
  const bool got_continuation =
      (opcode == WebSocketFrameHeader::kOpCodeContinuation);
  if (got_continuation != expecting_to_handle_continuation_) {
    const std::string console_log = got_continuation
        ? "Received unexpected continuation frame."
        : "Received start of new message but previous message is unfinished.";
    const std::string reason = got_continuation
        ? "Unexpected continuation"
        : "Previous data frame unfinished";
    return FailChannel(console_log, kWebSocketErrorProtocolError, reason);
  }
  expecting_to_handle_continuation_ = !final;
  WebSocketFrameHeader::OpCode opcode_to_send = opcode;
  if (!initial_frame_forwarded_ &&
      opcode == WebSocketFrameHeader::kOpCodeContinuation) {
    opcode_to_send = receiving_text_message_
                         ? WebSocketFrameHeader::kOpCodeText
                         : WebSocketFrameHeader::kOpCodeBinary;
  }
  if (opcode == WebSocketFrameHeader::kOpCodeText ||
      (opcode == WebSocketFrameHeader::kOpCodeContinuation &&
       receiving_text_message_)) {
    // This call is not redundant when size == 0 because it tells us what
    // the current state is.
    StreamingUtf8Validator::State state = incoming_utf8_validator_.AddBytes(
        size ? data_buffer->data() : NULL, static_cast<size_t>(size));
    if (state == StreamingUtf8Validator::INVALID ||
        (state == StreamingUtf8Validator::VALID_MIDPOINT && final)) {
      return FailChannel("Could not decode a text frame as UTF-8.",
                         kWebSocketErrorProtocolError,
                         "Invalid UTF-8 in text frame");
    }
    receiving_text_message_ = !final;
    DCHECK(!final || state == StreamingUtf8Validator::VALID_ENDPOINT);
  }
  if (size == 0U && !final)
    return CHANNEL_ALIVE;

  initial_frame_forwarded_ = !final;
  if (size > current_receive_quota_ || !pending_received_frames_.empty()) {
    const bool no_quota = (current_receive_quota_ == 0);
    DCHECK(no_quota || pending_received_frames_.empty());
    DVLOG(3) << "Queueing frame to renderer due to quota. quota="
             << current_receive_quota_ << " size=" << size;
    WebSocketFrameHeader::OpCode opcode_to_queue =
        no_quota ? opcode_to_send : WebSocketFrameHeader::kOpCodeContinuation;
    pending_received_frames_.push(PendingReceivedFrame(
        final, opcode_to_queue, data_buffer, current_receive_quota_, size));
    if (no_quota)
      return CHANNEL_ALIVE;
    size = current_receive_quota_;
    final = false;
  }

  current_receive_quota_ -= size;

  // Sends the received frame to the renderer process.
  return event_interface_->OnDataFrame(final, opcode_to_send,
                                       std::move(data_buffer), size);
}

ChannelState WebSocketChannel::HandleCloseFrame(uint16_t code,
                                                const std::string& reason) {
  DVLOG(1) << "Got Close with code " << code;
  switch (state_) {
    case CONNECTED:
      has_received_close_frame_ = true;
      received_close_code_ = code;
      received_close_reason_ = reason;
      if (!pending_received_frames_.empty()) {
        // We have some data to be sent to the renderer before sending this
        // frame.
        return CHANNEL_ALIVE;
      }
      return RespondToClosingHandshake();

    case SEND_CLOSED:
      SetState(CLOSE_WAIT);
      DCHECK(close_timer_.IsRunning());
      close_timer_.Stop();
      // This use of base::Unretained() is safe because we stop the timer
      // in the destructor.
      close_timer_.Start(
          FROM_HERE, underlying_connection_close_timeout_,
          base::Bind(&WebSocketChannel::CloseTimeout, base::Unretained(this)));

      // From RFC6455 section 7.1.5: "Each endpoint
      // will see the status code sent by the other end as _The WebSocket
      // Connection Close Code_."
      has_received_close_frame_ = true;
      received_close_code_ = code;
      received_close_reason_ = reason;
      break;

    default:
      LOG(DFATAL) << "Got Close in unexpected state " << state_;
      break;
  }
  return CHANNEL_ALIVE;
}

ChannelState WebSocketChannel::RespondToClosingHandshake() {
  DCHECK(has_received_close_frame_);
  DCHECK_EQ(CONNECTED, state_);
  SetState(RECV_CLOSED);
  if (SendClose(received_close_code_, received_close_reason_) ==
      CHANNEL_DELETED)
    return CHANNEL_DELETED;
  DCHECK_EQ(RECV_CLOSED, state_);

  SetState(CLOSE_WAIT);
  DCHECK(!close_timer_.IsRunning());
  // This use of base::Unretained() is safe because we stop the timer
  // in the destructor.
  close_timer_.Start(
      FROM_HERE, underlying_connection_close_timeout_,
      base::Bind(&WebSocketChannel::CloseTimeout, base::Unretained(this)));

  return event_interface_->OnClosingHandshake();
}

ChannelState WebSocketChannel::SendFrameInternal(
    bool fin,
    WebSocketFrameHeader::OpCode op_code,
    scoped_refptr<IOBuffer> buffer,
    uint64_t size) {
  DCHECK(state_ == CONNECTED || state_ == RECV_CLOSED);
  DCHECK(stream_);

  std::unique_ptr<WebSocketFrame> frame(new WebSocketFrame(op_code));
  WebSocketFrameHeader& header = frame->header;
  header.final = fin;
  header.masked = true;
  header.payload_length = size;
  frame->data = std::move(buffer);

  if (data_being_sent_) {
    // Either the link to the WebSocket server is saturated, or several messages
    // are being sent in a batch.
    // TODO(ricea): Keep some statistics to work out the situation and adjust
    // quota appropriately.
    if (!data_to_send_next_)
      data_to_send_next_.reset(new SendBuffer);
    data_to_send_next_->AddFrame(std::move(frame));
    return CHANNEL_ALIVE;
  }

  data_being_sent_.reset(new SendBuffer);
  data_being_sent_->AddFrame(std::move(frame));
  return WriteFrames();
}

ChannelState WebSocketChannel::FailChannel(const std::string& message,
                                           uint16_t code,
                                           const std::string& reason) {
  DCHECK_NE(FRESHLY_CONSTRUCTED, state_);
  DCHECK_NE(CONNECTING, state_);
  DCHECK_NE(CLOSED, state_);

  // TODO(ricea): Logging.
  if (state_ == CONNECTED) {
    if (SendClose(code, reason) == CHANNEL_DELETED)
      return CHANNEL_DELETED;
  }

  // Careful study of RFC6455 section 7.1.7 and 7.1.1 indicates the browser
  // should close the connection itself without waiting for the closing
  // handshake.
  stream_->Close();
  SetState(CLOSED);
  ChannelState result = event_interface_->OnFailChannel(message);
  DCHECK_EQ(CHANNEL_DELETED, result);
  return result;
}

ChannelState WebSocketChannel::SendClose(uint16_t code,
                                         const std::string& reason) {
  DCHECK(state_ == CONNECTED || state_ == RECV_CLOSED);
  DCHECK_LE(reason.size(), kMaximumCloseReasonLength);
  scoped_refptr<IOBuffer> body;
  uint64_t size = 0;
  if (code == kWebSocketErrorNoStatusReceived) {
    // Special case: translate kWebSocketErrorNoStatusReceived into a Close
    // frame with no payload.
    DCHECK(reason.empty());
    body = new IOBuffer(0);
  } else {
    const size_t payload_length = kWebSocketCloseCodeLength + reason.length();
    body = new IOBuffer(payload_length);
    size = payload_length;
    base::WriteBigEndian(body->data(), code);
    static_assert(sizeof(code) == kWebSocketCloseCodeLength,
                  "they should both be two");
    std::copy(
        reason.begin(), reason.end(), body->data() + kWebSocketCloseCodeLength);
  }
  if (SendFrameInternal(true, WebSocketFrameHeader::kOpCodeClose,
                        std::move(body), size) == CHANNEL_DELETED)
    return CHANNEL_DELETED;
  return CHANNEL_ALIVE;
}

bool WebSocketChannel::ParseClose(scoped_refptr<IOBuffer> buffer,
                                  uint64_t size,
                                  uint16_t* code,
                                  std::string* reason,
                                  std::string* message) {
  reason->clear();
  if (size < kWebSocketCloseCodeLength) {
    if (size == 0U) {
      *code = kWebSocketErrorNoStatusReceived;
      return true;
    }

    DVLOG(1) << "Close frame with payload size " << size << " received "
             << "(the first byte is " << std::hex
             << static_cast<int>(buffer->data()[0]) << ")";
    *code = kWebSocketErrorProtocolError;
    *message =
        "Received a broken close frame containing an invalid size body.";
    return false;
  }

  const char* data = buffer->data();
  uint16_t unchecked_code = 0;
  base::ReadBigEndian(data, &unchecked_code);
  static_assert(sizeof(unchecked_code) == kWebSocketCloseCodeLength,
                "they should both be two bytes");

  switch (unchecked_code) {
    case kWebSocketErrorNoStatusReceived:
    case kWebSocketErrorAbnormalClosure:
    case kWebSocketErrorTlsHandshake:
      *code = kWebSocketErrorProtocolError;
      *message =
          "Received a broken close frame containing a reserved status code.";
      return false;

    default:
      *code = unchecked_code;
      break;
  }

  std::string text(data + kWebSocketCloseCodeLength, data + size);
  if (StreamingUtf8Validator::Validate(text)) {
    reason->swap(text);
    return true;
  }

  *code = kWebSocketErrorProtocolError;
  *reason = "Invalid UTF-8 in Close frame";
  *message = "Received a broken close frame containing invalid UTF-8.";
  return false;
}

ChannelState WebSocketChannel::DoDropChannel(bool was_clean,
                                             uint16_t code,
                                             const std::string& reason) {
  if (CHANNEL_DELETED ==
      notification_sender_->SendImmediately(event_interface_.get()))
    return CHANNEL_DELETED;
  ChannelState result =
      event_interface_->OnDropChannel(was_clean, code, reason);
  DCHECK_EQ(CHANNEL_DELETED, result);
  return result;
}

void WebSocketChannel::CloseTimeout() {
  stream_->Close();
  SetState(CLOSED);
  DoDropChannel(false, kWebSocketErrorAbnormalClosure, "");
  // |this| has been deleted.
}

}  // namespace net
