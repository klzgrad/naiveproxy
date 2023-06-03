// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/io/event_loop_connecting_client_socket.h"

#include <limits>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace quic {

EventLoopConnectingClientSocket::EventLoopConnectingClientSocket(
    socket_api::SocketProtocol protocol,
    const quic::QuicSocketAddress& peer_address,
    QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
    QuicEventLoop* event_loop, quiche::QuicheBufferAllocator* buffer_allocator,
    AsyncVisitor* async_visitor)
    : protocol_(protocol),
      peer_address_(peer_address),
      receive_buffer_size_(receive_buffer_size),
      send_buffer_size_(send_buffer_size),
      event_loop_(event_loop),
      buffer_allocator_(buffer_allocator),
      async_visitor_(async_visitor) {
  QUICHE_DCHECK(event_loop_);
  QUICHE_DCHECK(buffer_allocator_);
}

EventLoopConnectingClientSocket::~EventLoopConnectingClientSocket() {
  // Connected socket must be closed via Disconnect() before destruction. Cannot
  // safely recover if state indicates caller may be expecting async callbacks.
  QUICHE_DCHECK(connect_status_ != ConnectStatus::kConnecting);
  QUICHE_DCHECK(!receive_max_size_.has_value());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));
  if (descriptor_ != kInvalidSocketFd) {
    QUICHE_BUG(quic_event_loop_connecting_socket_invalid_destruction)
        << "Must call Disconnect() on connected socket before destruction.";
    Close();
  }

  QUICHE_DCHECK(connect_status_ == ConnectStatus::kNotConnected);
  QUICHE_DCHECK(send_remaining_.empty());
}

absl::Status EventLoopConnectingClientSocket::ConnectBlocking() {
  QUICHE_DCHECK_EQ(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kNotConnected);
  QUICHE_DCHECK(!receive_max_size_.has_value());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  absl::Status status = Open();
  if (!status.ok()) {
    return status;
  }

  status = socket_api::SetSocketBlocking(descriptor_, /*blocking=*/true);
  if (!status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Failed to set socket to address: " << peer_address_.ToString()
        << " as blocking for connect with error: " << status;
    Close();
    return status;
  }

  status = DoInitialConnect();

  if (absl::IsUnavailable(status)) {
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Non-blocking connect to should-be blocking socket to address:"
        << peer_address_.ToString() << ".";
    Close();
    connect_status_ = ConnectStatus::kNotConnected;
    return status;
  } else if (!status.ok()) {
    // DoInitialConnect() closes the socket on failures.
    QUICHE_DCHECK_EQ(descriptor_, kInvalidSocketFd);
    QUICHE_DCHECK(connect_status_ == ConnectStatus::kNotConnected);
    return status;
  }

  status = socket_api::SetSocketBlocking(descriptor_, /*blocking=*/false);
  if (!status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Failed to return socket to address: " << peer_address_.ToString()
        << " to non-blocking after connect with error: " << status;
    Close();
    connect_status_ = ConnectStatus::kNotConnected;
  }

  QUICHE_DCHECK(connect_status_ != ConnectStatus::kConnecting);
  return status;
}

void EventLoopConnectingClientSocket::ConnectAsync() {
  QUICHE_DCHECK(async_visitor_);
  QUICHE_DCHECK_EQ(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kNotConnected);
  QUICHE_DCHECK(!receive_max_size_.has_value());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  absl::Status status = Open();
  if (!status.ok()) {
    async_visitor_->ConnectComplete(status);
    return;
  }

  FinishOrRearmAsyncConnect(DoInitialConnect());
}

void EventLoopConnectingClientSocket::Disconnect() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ != ConnectStatus::kNotConnected);

  Close();
  QUICHE_DCHECK_EQ(descriptor_, kInvalidSocketFd);

  // Reset all state before invoking any callbacks.
  bool require_connect_callback = connect_status_ == ConnectStatus::kConnecting;
  connect_status_ = ConnectStatus::kNotConnected;
  bool require_receive_callback = receive_max_size_.has_value();
  receive_max_size_.reset();
  bool require_send_callback =
      !absl::holds_alternative<absl::monostate>(send_data_);
  send_data_ = absl::monostate();
  send_remaining_ = "";

  if (require_connect_callback) {
    QUICHE_DCHECK(async_visitor_);
    async_visitor_->ConnectComplete(absl::CancelledError());
  }
  if (require_receive_callback) {
    QUICHE_DCHECK(async_visitor_);
    async_visitor_->ReceiveComplete(absl::CancelledError());
  }
  if (require_send_callback) {
    QUICHE_DCHECK(async_visitor_);
    async_visitor_->SendComplete(absl::CancelledError());
  }
}

absl::StatusOr<QuicSocketAddress>
EventLoopConnectingClientSocket::GetLocalAddress() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);

  return socket_api::GetSocketAddress(descriptor_);
}

absl::StatusOr<quiche::QuicheMemSlice>
EventLoopConnectingClientSocket::ReceiveBlocking(QuicByteCount max_size) {
  QUICHE_DCHECK_GT(max_size, 0u);
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);
  QUICHE_DCHECK(!receive_max_size_.has_value());

  absl::Status status =
      socket_api::SetSocketBlocking(descriptor_, /*blocking=*/true);
  if (!status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Failed to set socket to address: " << peer_address_.ToString()
        << " as blocking for receive with error: " << status;
    return status;
  }

  receive_max_size_ = max_size;
  absl::StatusOr<quiche::QuicheMemSlice> buffer = ReceiveInternal();

  if (!buffer.ok() && absl::IsUnavailable(buffer.status())) {
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Non-blocking receive from should-be blocking socket to address:"
        << peer_address_.ToString() << ".";
    receive_max_size_.reset();
  } else {
    QUICHE_DCHECK(!receive_max_size_.has_value());
  }

  absl::Status set_non_blocking_status =
      socket_api::SetSocketBlocking(descriptor_, /*blocking=*/false);
  if (!set_non_blocking_status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Failed to return socket to address: " << peer_address_.ToString()
        << " to non-blocking after receive with error: "
        << set_non_blocking_status;
    return set_non_blocking_status;
  }

  return buffer;
}

void EventLoopConnectingClientSocket::ReceiveAsync(QuicByteCount max_size) {
  QUICHE_DCHECK(async_visitor_);
  QUICHE_DCHECK_GT(max_size, 0u);
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);
  QUICHE_DCHECK(!receive_max_size_.has_value());

  receive_max_size_ = max_size;

  FinishOrRearmAsyncReceive(ReceiveInternal());
}

absl::Status EventLoopConnectingClientSocket::SendBlocking(std::string data) {
  QUICHE_DCHECK(!data.empty());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  send_data_ = std::move(data);
  return SendBlockingInternal();
}

absl::Status EventLoopConnectingClientSocket::SendBlocking(
    quiche::QuicheMemSlice data) {
  QUICHE_DCHECK(!data.empty());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  send_data_ = std::move(data);
  return SendBlockingInternal();
}

void EventLoopConnectingClientSocket::SendAsync(std::string data) {
  QUICHE_DCHECK(!data.empty());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  send_data_ = std::move(data);
  send_remaining_ = absl::get<std::string>(send_data_);

  FinishOrRearmAsyncSend(SendInternal());
}

void EventLoopConnectingClientSocket::SendAsync(quiche::QuicheMemSlice data) {
  QUICHE_DCHECK(!data.empty());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  send_data_ = std::move(data);
  send_remaining_ =
      absl::get<quiche::QuicheMemSlice>(send_data_).AsStringView();

  FinishOrRearmAsyncSend(SendInternal());
}

void EventLoopConnectingClientSocket::OnSocketEvent(
    QuicEventLoop* event_loop, SocketFd fd, QuicSocketEventMask events) {
  QUICHE_DCHECK_EQ(event_loop, event_loop_);
  QUICHE_DCHECK_EQ(fd, descriptor_);

  if (connect_status_ == ConnectStatus::kConnecting &&
      (events & (kSocketEventWritable | kSocketEventError))) {
    FinishOrRearmAsyncConnect(GetConnectResult());
    return;
  }

  if (receive_max_size_.has_value() &&
      (events & (kSocketEventReadable | kSocketEventError))) {
    FinishOrRearmAsyncReceive(ReceiveInternal());
  }
  if (!send_remaining_.empty() &&
      (events & (kSocketEventWritable | kSocketEventError))) {
    FinishOrRearmAsyncSend(SendInternal());
  }
}

absl::Status EventLoopConnectingClientSocket::Open() {
  QUICHE_DCHECK_EQ(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kNotConnected);
  QUICHE_DCHECK(!receive_max_size_.has_value());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));
  QUICHE_DCHECK(send_remaining_.empty());

  absl::StatusOr<SocketFd> descriptor =
      socket_api::CreateSocket(peer_address_.host().address_family(), protocol_,
                               /*blocking=*/false);
  if (!descriptor.ok()) {
    QUICHE_DVLOG(1) << "Failed to open socket for connection to address: "
                    << peer_address_.ToString()
                    << " with error: " << descriptor.status();
    return descriptor.status();
  }
  QUICHE_DCHECK_NE(descriptor.value(), kInvalidSocketFd);

  descriptor_ = descriptor.value();

  if (async_visitor_) {
    bool registered;
    if (event_loop_->SupportsEdgeTriggered()) {
      registered = event_loop_->RegisterSocket(
          descriptor_,
          kSocketEventReadable | kSocketEventWritable | kSocketEventError,
          this);
    } else {
      // Just register the socket without any armed events for now.  Will rearm
      // with specific events as needed.  Registering now before events are
      // needed makes it easier to ensure the socket is registered only once
      // and can always be unregistered on socket close.
      registered = event_loop_->RegisterSocket(descriptor_, /*events=*/0, this);
    }
    QUICHE_DCHECK(registered);
  }

  if (receive_buffer_size_ != 0) {
    absl::Status status =
        socket_api::SetReceiveBufferSize(descriptor_, receive_buffer_size_);
    if (!status.ok()) {
      QUICHE_LOG_FIRST_N(WARNING, 100)
          << "Failed to set receive buffer size to: " << receive_buffer_size_
          << " for socket to address: " << peer_address_.ToString()
          << " with error: " << status;
      Close();
      return status;
    }
  }

  if (send_buffer_size_ != 0) {
    absl::Status status =
        socket_api::SetSendBufferSize(descriptor_, send_buffer_size_);
    if (!status.ok()) {
      QUICHE_LOG_FIRST_N(WARNING, 100)
          << "Failed to set send buffer size to: " << send_buffer_size_
          << " for socket to address: " << peer_address_.ToString()
          << " with error: " << status;
      Close();
      return status;
    }
  }

  return absl::OkStatus();
}

void EventLoopConnectingClientSocket::Close() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);

  bool unregistered = event_loop_->UnregisterSocket(descriptor_);
  QUICHE_DCHECK_EQ(unregistered, !!async_visitor_);

  absl::Status status = socket_api::Close(descriptor_);
  if (!status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Could not close socket to address: " << peer_address_.ToString()
        << " with error: " << status;
  }

  descriptor_ = kInvalidSocketFd;
}

absl::Status EventLoopConnectingClientSocket::DoInitialConnect() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kNotConnected);
  QUICHE_DCHECK(!receive_max_size_.has_value());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  absl::Status connect_result = socket_api::Connect(descriptor_, peer_address_);

  if (connect_result.ok()) {
    connect_status_ = ConnectStatus::kConnected;
  } else if (absl::IsUnavailable(connect_result)) {
    connect_status_ = ConnectStatus::kConnecting;
  } else {
    QUICHE_DVLOG(1) << "Synchronously failed to connect socket to address: "
                    << peer_address_.ToString()
                    << " with error: " << connect_result;
    Close();
    connect_status_ = ConnectStatus::kNotConnected;
  }

  return connect_result;
}

absl::Status EventLoopConnectingClientSocket::GetConnectResult() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnecting);
  QUICHE_DCHECK(!receive_max_size_.has_value());
  QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));

  absl::Status error = socket_api::GetSocketError(descriptor_);

  if (!error.ok()) {
    QUICHE_DVLOG(1) << "Asynchronously failed to connect socket to address: "
                    << peer_address_.ToString() << " with error: " << error;
    Close();
    connect_status_ = ConnectStatus::kNotConnected;
    return error;
  }

  // Peek at one byte to confirm the connection is actually alive. Motivation:
  // 1) Plausibly could have a lot of cases where the connection operation
  //    itself technically succeeds but the socket then quickly fails.  Don't
  //    want to claim connection success here if, by the time this code is
  //    running after event triggers and such, the socket has already failed.
  //    Lot of undefined room around whether or not such errors would be saved
  //    into SO_ERROR and returned by socket_api::GetSocketError().
  // 2) With the various platforms and event systems involved, less than 100%
  //    trust that it's impossible to end up in this method before the async
  //    connect has completed/errored. Given that Connect() and GetSocketError()
  //    does not difinitevely differentiate between success and
  //    still-in-progress, and given that there's a very simple and performant
  //    way to positively confirm the socket is connected (peek), do that here.
  //    (Could consider making the not-connected case a QUIC_BUG if a way is
  //    found to differentiate it from (1).)
  absl::StatusOr<bool> peek_data = OneBytePeek();
  if (peek_data.ok() || absl::IsUnavailable(peek_data.status())) {
    connect_status_ = ConnectStatus::kConnected;
  } else {
    error = peek_data.status();
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Socket to address: " << peer_address_.ToString()
        << " signalled writable after connect and no connect error found, "
           "but socket does not appear connected with error: "
        << error;
    Close();
    connect_status_ = ConnectStatus::kNotConnected;
  }

  return error;
}

void EventLoopConnectingClientSocket::FinishOrRearmAsyncConnect(
    absl::Status status) {
  if (absl::IsUnavailable(status)) {
    if (!event_loop_->SupportsEdgeTriggered()) {
      bool result = event_loop_->RearmSocket(
          descriptor_, kSocketEventWritable | kSocketEventError);
      QUICHE_DCHECK(result);
    }
    QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnecting);
  } else {
    QUICHE_DCHECK(connect_status_ != ConnectStatus::kConnecting);
    async_visitor_->ConnectComplete(status);
  }
}

absl::StatusOr<quiche::QuicheMemSlice>
EventLoopConnectingClientSocket::ReceiveInternal() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);
  QUICHE_CHECK(receive_max_size_.has_value());
  QUICHE_DCHECK_GE(receive_max_size_.value(), 1u);
  QUICHE_DCHECK_LE(receive_max_size_.value(),
                   std::numeric_limits<size_t>::max());

  // Before allocating a buffer, do a 1-byte peek to determine if needed.
  if (receive_max_size_.value() > 1) {
    absl::StatusOr<bool> peek_data = OneBytePeek();
    if (!peek_data.ok()) {
      if (!absl::IsUnavailable(peek_data.status())) {
        receive_max_size_.reset();
      }
      return peek_data.status();
    } else if (!peek_data.value()) {
      receive_max_size_.reset();
      return quiche::QuicheMemSlice();
    }
  }

  quiche::QuicheBuffer buffer(buffer_allocator_, receive_max_size_.value());
  absl::StatusOr<absl::Span<char>> received = socket_api::Receive(
      descriptor_, absl::MakeSpan(buffer.data(), buffer.size()));

  if (received.ok()) {
    QUICHE_DCHECK_LE(received.value().size(), buffer.size());
    QUICHE_DCHECK_EQ(received.value().data(), buffer.data());

    receive_max_size_.reset();
    return quiche::QuicheMemSlice(
        quiche::QuicheBuffer(buffer.Release(), received.value().size()));
  } else {
    if (!absl::IsUnavailable(received.status())) {
      QUICHE_DVLOG(1) << "Failed to receive from socket to address: "
                      << peer_address_.ToString()
                      << " with error: " << received.status();
      receive_max_size_.reset();
    }
    return received.status();
  }
}

void EventLoopConnectingClientSocket::FinishOrRearmAsyncReceive(
    absl::StatusOr<quiche::QuicheMemSlice> buffer) {
  QUICHE_DCHECK(async_visitor_);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);

  if (!buffer.ok() && absl::IsUnavailable(buffer.status())) {
    if (!event_loop_->SupportsEdgeTriggered()) {
      bool result = event_loop_->RearmSocket(
          descriptor_, kSocketEventReadable | kSocketEventError);
      QUICHE_DCHECK(result);
    }
    QUICHE_DCHECK(receive_max_size_.has_value());
  } else {
    QUICHE_DCHECK(!receive_max_size_.has_value());
    async_visitor_->ReceiveComplete(std::move(buffer));
  }
}

absl::StatusOr<bool> EventLoopConnectingClientSocket::OneBytePeek() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);

  char peek_buffer;
  absl::StatusOr<absl::Span<char>> peek_received = socket_api::Receive(
      descriptor_, absl::MakeSpan(&peek_buffer, /*size=*/1), /*peek=*/true);
  if (!peek_received.ok()) {
    return peek_received.status();
  } else {
    return !peek_received.value().empty();
  }
}

absl::Status EventLoopConnectingClientSocket::SendBlockingInternal() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);
  QUICHE_DCHECK(!absl::holds_alternative<absl::monostate>(send_data_));
  QUICHE_DCHECK(send_remaining_.empty());

  absl::Status status =
      socket_api::SetSocketBlocking(descriptor_, /*blocking=*/true);
  if (!status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Failed to set socket to address: " << peer_address_.ToString()
        << " as blocking for send with error: " << status;
    send_data_ = absl::monostate();
    return status;
  }

  if (absl::holds_alternative<std::string>(send_data_)) {
    send_remaining_ = absl::get<std::string>(send_data_);
  } else {
    send_remaining_ =
        absl::get<quiche::QuicheMemSlice>(send_data_).AsStringView();
  }

  status = SendInternal();
  if (absl::IsUnavailable(status)) {
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Non-blocking send for should-be blocking socket to address:"
        << peer_address_.ToString();
    send_data_ = absl::monostate();
    send_remaining_ = "";
  } else {
    QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));
    QUICHE_DCHECK(send_remaining_.empty());
  }

  absl::Status set_non_blocking_status =
      socket_api::SetSocketBlocking(descriptor_, /*blocking=*/false);
  if (!set_non_blocking_status.ok()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Failed to return socket to address: " << peer_address_.ToString()
        << " to non-blocking after send with error: "
        << set_non_blocking_status;
    return set_non_blocking_status;
  }

  return status;
}

absl::Status EventLoopConnectingClientSocket::SendInternal() {
  QUICHE_DCHECK_NE(descriptor_, kInvalidSocketFd);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);
  QUICHE_DCHECK(!absl::holds_alternative<absl::monostate>(send_data_));
  QUICHE_DCHECK(!send_remaining_.empty());

  // Repeat send until all data sent, unavailable, or error.
  while (!send_remaining_.empty()) {
    absl::StatusOr<absl::string_view> remainder =
        socket_api::Send(descriptor_, send_remaining_);

    if (remainder.ok()) {
      QUICHE_DCHECK(remainder.value().empty() ||
                    (remainder.value().data() >= send_remaining_.data() &&
                     remainder.value().data() <
                         send_remaining_.data() + send_remaining_.size()));
      QUICHE_DCHECK(remainder.value().empty() ||
                    (remainder.value().data() + remainder.value().size() ==
                     send_remaining_.data() + send_remaining_.size()));
      send_remaining_ = remainder.value();
    } else {
      if (!absl::IsUnavailable(remainder.status())) {
        QUICHE_DVLOG(1) << "Failed to send to socket to address: "
                        << peer_address_.ToString()
                        << " with error: " << remainder.status();
        send_data_ = absl::monostate();
        send_remaining_ = "";
      }
      return remainder.status();
    }
  }

  send_data_ = absl::monostate();
  return absl::OkStatus();
}

void EventLoopConnectingClientSocket::FinishOrRearmAsyncSend(
    absl::Status status) {
  QUICHE_DCHECK(async_visitor_);
  QUICHE_DCHECK(connect_status_ == ConnectStatus::kConnected);

  if (absl::IsUnavailable(status)) {
    if (!event_loop_->SupportsEdgeTriggered()) {
      bool result = event_loop_->RearmSocket(
          descriptor_, kSocketEventWritable | kSocketEventError);
      QUICHE_DCHECK(result);
    }
    QUICHE_DCHECK(!absl::holds_alternative<absl::monostate>(send_data_));
    QUICHE_DCHECK(!send_remaining_.empty());
  } else {
    QUICHE_DCHECK(absl::holds_alternative<absl::monostate>(send_data_));
    QUICHE_DCHECK(send_remaining_.empty());
    async_visitor_->SendComplete(status);
  }
}

}  // namespace quic
