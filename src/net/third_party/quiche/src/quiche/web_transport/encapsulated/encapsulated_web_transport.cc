// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/web_transport/encapsulated/encapsulated_web_transport.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/capsule.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {

namespace {

using ::quiche::Capsule;
using ::quiche::CapsuleType;
using ::quiche::CloseWebTransportSessionCapsule;

// This is arbitrary, since we don't have any real MTU restriction when running
// over TCP.
constexpr uint64_t kEncapsulatedMaxDatagramSize = 9000;

}  // namespace

EncapsulatedSession::EncapsulatedSession(
    Perspective perspective, FatalErrorCallback fatal_error_callback)
    : perspective_(perspective),
      fatal_error_callback_(std::move(fatal_error_callback)),
      capsule_parser_(this) {}

void EncapsulatedSession::InitializeClient(
    std::unique_ptr<SessionVisitor> visitor,
    quiche::HttpHeaderBlock& /*outgoing_headers*/, quiche::WriteStream* writer,
    quiche::ReadStream* reader) {
  if (state_ != kUninitialized) {
    OnFatalError("Called InitializeClient() in an invalid state");
    return;
  }
  if (perspective_ != Perspective::kClient) {
    OnFatalError("Called InitializeClient() on a server session");
    return;
  }

  visitor_ = std::move(visitor);
  writer_ = writer;
  reader_ = reader;
  state_ = kWaitingForHeaders;
}

void EncapsulatedSession::InitializeServer(
    std::unique_ptr<SessionVisitor> visitor,
    const quiche::HttpHeaderBlock& /*incoming_headers*/,
    quiche::HttpHeaderBlock& /*outgoing_headers*/, quiche::WriteStream* writer,
    quiche::ReadStream* reader) {
  if (state_ != kUninitialized) {
    OnFatalError("Called InitializeServer() in an invalid state");
    return;
  }
  if (perspective_ != Perspective::kServer) {
    OnFatalError("Called InitializeServer() on a client session");
    return;
  }

  visitor_ = std::move(visitor);
  writer_ = writer;
  reader_ = reader;
  OpenSession();
}
void EncapsulatedSession::ProcessIncomingServerHeaders(
    const quiche::HttpHeaderBlock& /*headers*/) {
  if (state_ != kWaitingForHeaders) {
    OnFatalError("Called ProcessIncomingServerHeaders() in an invalid state");
    return;
  }
  OpenSession();
}

void EncapsulatedSession::CloseSession(SessionErrorCode error_code,
                                       absl::string_view error_message) {
  switch (state_) {
    case kUninitialized:
    case kWaitingForHeaders:
      OnFatalError(absl::StrCat(
          "Attempted to close a session before it opened with error 0x",
          absl::Hex(error_code), ": ", error_message));
      return;
    case kSessionClosing:
    case kSessionClosed:
      OnFatalError(absl::StrCat(
          "Attempted to close a session that is already closed with error 0x",
          absl::Hex(error_code), ": ", error_message));
      return;
    case kSessionOpen:
      break;
  }
  state_ = kSessionClosing;
  buffered_session_close_ =
      BufferedClose{error_code, std::string(error_message)};
  OnCanWrite();
}

Stream* EncapsulatedSession::AcceptIncomingBidirectionalStream() {
  return nullptr;
}
Stream* EncapsulatedSession::AcceptIncomingUnidirectionalStream() {
  return nullptr;
}
bool EncapsulatedSession::CanOpenNextOutgoingBidirectionalStream() {
  return false;
}
bool EncapsulatedSession::CanOpenNextOutgoingUnidirectionalStream() {
  return false;
}
Stream* EncapsulatedSession::OpenOutgoingBidirectionalStream() {
  return nullptr;
}
Stream* EncapsulatedSession::OpenOutgoingUnidirectionalStream() {
  return nullptr;
}

Stream* EncapsulatedSession::GetStreamById(StreamId /*id*/) { return nullptr; }
DatagramStats EncapsulatedSession::GetDatagramStats() {
  DatagramStats stats;
  stats.expired_outgoing = 0;
  stats.lost_outgoing = 0;
  return stats;
}

SessionStats EncapsulatedSession::GetSessionStats() {
  // We could potentially get stats via tcp_info and similar mechanisms, but
  // that would require us knowing what the underlying socket is.
  return SessionStats();
}

void EncapsulatedSession::NotifySessionDraining() {
  control_capsule_queue_.push_back(quiche::SerializeCapsule(
      quiche::Capsule(quiche::DrainWebTransportSessionCapsule()), allocator_));
  OnCanWrite();
}
void EncapsulatedSession::SetOnDraining(
    quiche::SingleUseCallback<void()> callback) {
  draining_callback_ = std::move(callback);
}

DatagramStatus EncapsulatedSession::SendOrQueueDatagram(
    absl::string_view datagram) {
  if (datagram.size() > GetMaxDatagramSize()) {
    return DatagramStatus{
        DatagramStatusCode::kTooBig,
        absl::StrCat("Datagram is ", datagram.size(),
                     " bytes long, while the specified maximum size is ",
                     GetMaxDatagramSize())};
  }

  bool write_blocked;
  switch (state_) {
    case kUninitialized:
      write_blocked = true;
      break;
    // We can send datagrams before receiving any headers from the peer, since
    // datagrams are not subject to queueing.
    case kWaitingForHeaders:
    case kSessionOpen:
      write_blocked = !writer_->CanWrite();
      break;
    case kSessionClosing:
    case kSessionClosed:
      return DatagramStatus{DatagramStatusCode::kInternalError,
                            "Writing into an already closed session"};
  }

  if (write_blocked) {
    // TODO: this *may* be useful to split into a separate queue.
    control_capsule_queue_.push_back(
        quiche::SerializeCapsule(Capsule::Datagram(datagram), allocator_));
    return DatagramStatus{DatagramStatusCode::kSuccess, ""};
  }

  // We could always write via OnCanWrite() above, but the optimistic path below
  // allows us to avoid a copy.
  quiche::QuicheBuffer buffer =
      quiche::SerializeDatagramCapsuleHeader(datagram.size(), allocator_);
  std::array spans = {buffer.AsStringView(), datagram};
  absl::Status write_status =
      writer_->Writev(absl::MakeConstSpan(spans), quiche::StreamWriteOptions());
  if (!write_status.ok()) {
    OnWriteError(write_status);
    return DatagramStatus{
        DatagramStatusCode::kInternalError,
        absl::StrCat("Write error for datagram: ", write_status.ToString())};
  }
  return DatagramStatus{DatagramStatusCode::kSuccess, ""};
}

uint64_t EncapsulatedSession::GetMaxDatagramSize() const {
  return kEncapsulatedMaxDatagramSize;
}

void EncapsulatedSession::SetDatagramMaxTimeInQueue(
    absl::Duration /*max_time_in_queue*/) {
  // TODO(b/264263113): implement this (requires having a mockable clock).
}

void EncapsulatedSession::OnCanWrite() {
  if (state_ == kUninitialized || !writer_) {
    OnFatalError("Trying to write before the session is initialized");
    return;
  }
  if (state_ == kSessionClosed) {
    OnFatalError("Trying to write before the session is closed");
    return;
  }

  if (state_ == kSessionClosing) {
    if (writer_->CanWrite()) {
      CloseWebTransportSessionCapsule capsule{
          buffered_session_close_.error_code,
          buffered_session_close_.error_message};
      quiche::QuicheBuffer buffer =
          quiche::SerializeCapsule(Capsule(std::move(capsule)), allocator_);
      absl::Status write_status = SendFin(buffer.AsStringView());
      if (!write_status.ok()) {
        OnWriteError(quiche::AppendToStatus(write_status,
                                            " while writing WT_CLOSE_SESSION"));
        return;
      }
      OnSessionClosed(buffered_session_close_.error_code,
                      buffered_session_close_.error_message);
    }
    return;
  }

  while (writer_->CanWrite() && !control_capsule_queue_.empty()) {
    absl::Status write_status = quiche::WriteIntoStream(
        *writer_, control_capsule_queue_.front().AsStringView());
    if (!write_status.ok()) {
      OnWriteError(write_status);
      return;
    }
    control_capsule_queue_.pop_front();
  }

  // TODO(b/264263113): send stream data.
}

void EncapsulatedSession::OnCanRead() {
  if (state_ == kSessionClosed || state_ == kSessionClosing) {
    return;
  }
  bool has_fin = quiche::ProcessAllReadableRegions(
      *reader_, [&](absl::string_view fragment) {
        capsule_parser_.IngestCapsuleFragment(fragment);
      });
  if (has_fin) {
    capsule_parser_.ErrorIfThereIsRemainingBufferedData();
    OnSessionClosed(0, "");
  }
}

bool EncapsulatedSession::OnCapsule(const quiche::Capsule& capsule) {
  switch (capsule.capsule_type()) {
    case CapsuleType::DATAGRAM:
      visitor_->OnDatagramReceived(
          capsule.datagram_capsule().http_datagram_payload);
      break;
    case CapsuleType::DRAIN_WEBTRANSPORT_SESSION:
      if (draining_callback_) {
        std::move(draining_callback_)();
      }
      break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      OnSessionClosed(
          capsule.close_web_transport_session_capsule().error_code,
          std::string(
              capsule.close_web_transport_session_capsule().error_message));
      break;
    default:
      break;
  }
  return true;
}

void EncapsulatedSession::OnCapsuleParseFailure(
    absl::string_view error_message) {
  OnFatalError(absl::StrCat("Stream parse error: ", error_message));
}

void EncapsulatedSession::OpenSession() {
  state_ = kSessionOpen;
  visitor_->OnSessionReady();
  OnCanWrite();
  OnCanRead();
}

absl::Status EncapsulatedSession::SendFin(absl::string_view data) {
  QUICHE_DCHECK(!fin_sent_);
  fin_sent_ = true;
  quiche::StreamWriteOptions options;
  options.set_send_fin(true);
  return quiche::WriteIntoStream(*writer_, data, options);
}

void EncapsulatedSession::OnSessionClosed(SessionErrorCode error_code,
                                          const std::string& error_message) {
  if (!fin_sent_) {
    absl::Status status = SendFin("");
    if (!status.ok()) {
      OnWriteError(status);
      return;
    }
  }

  if (session_close_notified_) {
    QUICHE_DCHECK_EQ(state_, kSessionClosed);
    return;
  }
  state_ = kSessionClosed;
  session_close_notified_ = true;

  if (visitor_ != nullptr) {
    visitor_->OnSessionClosed(error_code, error_message);
  }
}

void EncapsulatedSession::OnFatalError(absl::string_view error_message) {
  QUICHE_DLOG(ERROR) << "Fatal error in encapsulated WebTransport: "
                     << error_message;
  state_ = kSessionClosed;
  if (fatal_error_callback_) {
    std::move(fatal_error_callback_)(error_message);
  }
}

void EncapsulatedSession::OnWriteError(absl::Status error) {
  OnFatalError(absl::StrCat(
      error, " while trying to write encapsulated WebTransport data"));
}

}  // namespace webtransport
