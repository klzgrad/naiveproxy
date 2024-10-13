// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/web_transport/encapsulated/encapsulated_web_transport.h"

#include <stdbool.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/capsule.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_circular_deque.h"
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

constexpr StreamPriority kDefaultPriority = StreamPriority{0, 0};

}  // namespace

EncapsulatedSession::EncapsulatedSession(
    Perspective perspective, FatalErrorCallback fatal_error_callback)
    : perspective_(perspective),
      fatal_error_callback_(std::move(fatal_error_callback)),
      capsule_parser_(this),
      next_outgoing_bidi_stream_(perspective == Perspective::kClient ? 0 : 1),
      next_outgoing_unidi_stream_(perspective == Perspective::kClient ? 2 : 3) {
  QUICHE_DCHECK(IsIdOpenedBy(next_outgoing_bidi_stream_, perspective));
  QUICHE_DCHECK(IsIdOpenedBy(next_outgoing_unidi_stream_, perspective));
}

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

Stream* EncapsulatedSession::AcceptIncomingStream(
    quiche::QuicheCircularDeque<StreamId>& queue) {
  while (!queue.empty()) {
    StreamId id = queue.front();
    queue.pop_front();
    Stream* stream = GetStreamById(id);
    if (stream == nullptr) {
      // Stream got reset and garbage collected before the peer ever had a
      // chance to look at it.
      continue;
    }
    return stream;
  }
  return nullptr;
}

Stream* EncapsulatedSession::AcceptIncomingBidirectionalStream() {
  return AcceptIncomingStream(incoming_bidirectional_streams_);
}
Stream* EncapsulatedSession::AcceptIncomingUnidirectionalStream() {
  return AcceptIncomingStream(incoming_unidirectional_streams_);
}
bool EncapsulatedSession::CanOpenNextOutgoingBidirectionalStream() {
  // TODO: implement flow control.
  return true;
}
bool EncapsulatedSession::CanOpenNextOutgoingUnidirectionalStream() {
  // TODO: implement flow control.
  return true;
}
Stream* EncapsulatedSession::OpenOutgoingStream(StreamId& counter) {
  StreamId stream_id = counter;
  counter += 4;
  auto [it, inserted] = streams_.emplace(
      std::piecewise_construct, std::forward_as_tuple(stream_id),
      std::forward_as_tuple(this, stream_id));
  QUICHE_DCHECK(inserted);
  return &it->second;
}
Stream* EncapsulatedSession::OpenOutgoingBidirectionalStream() {
  if (!CanOpenNextOutgoingBidirectionalStream()) {
    return nullptr;
  }
  return OpenOutgoingStream(next_outgoing_bidi_stream_);
}
Stream* EncapsulatedSession::OpenOutgoingUnidirectionalStream() {
  if (!CanOpenNextOutgoingUnidirectionalStream()) {
    return nullptr;
  }
  return OpenOutgoingStream(next_outgoing_unidi_stream_);
}

Stream* EncapsulatedSession::GetStreamById(StreamId id) {
  auto it = streams_.find(id);
  if (it == streams_.end()) {
    return nullptr;
  }
  return &it->second;
}

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
  SendControlCapsule(quiche::DrainWebTransportSessionCapsule());
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

  while (writer_->CanWrite()) {
    absl::StatusOr<StreamId> next_id = scheduler_.PopFront();
    if (!next_id.ok()) {
      QUICHE_DCHECK_EQ(next_id.status().code(), absl::StatusCode::kNotFound);
      return;
    }
    auto it = streams_.find(*next_id);
    if (it == streams_.end()) {
      QUICHE_BUG(WT_H2_NextStreamNotInTheMap);
      OnFatalError("Next scheduled stream is not in the map");
      return;
    }
    QUICHE_DCHECK(it->second.HasPendingWrite());
    it->second.FlushPendingWrite();
  }
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
  if (state_ == kSessionOpen) {
    GarbageCollectStreams();
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
    case CapsuleType::WT_STREAM:
    case CapsuleType::WT_STREAM_WITH_FIN:
      ProcessStreamCapsule(capsule,
                           capsule.web_transport_stream_data().stream_id);
      break;
    case CapsuleType::WT_RESET_STREAM:
      ProcessStreamCapsule(capsule,
                           capsule.web_transport_reset_stream().stream_id);
      break;
    case CapsuleType::WT_STOP_SENDING:
      ProcessStreamCapsule(capsule,
                           capsule.web_transport_stop_sending().stream_id);
      break;
    default:
      break;
  }
  return state_ != kSessionClosed;
}

void EncapsulatedSession::OnCapsuleParseFailure(
    absl::string_view error_message) {
  if (state_ == kSessionClosed) {
    return;
  }
  OnFatalError(absl::StrCat("Stream parse error: ", error_message));
}

void EncapsulatedSession::ProcessStreamCapsule(const quiche::Capsule& capsule,
                                               StreamId stream_id) {
  bool new_stream_created = false;
  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    if (IsOutgoing(stream_id)) {
      // Ignore this frame, as it is possible that it refers to an outgoing
      // stream that has been closed.
      return;
    }
    // TODO: check flow control here.
    it = streams_.emplace_hint(it, std::piecewise_construct,
                               std::forward_as_tuple(stream_id),
                               std::forward_as_tuple(this, stream_id));
    new_stream_created = true;
  }
  InnerStream& stream = it->second;
  stream.ProcessCapsule(capsule);
  if (new_stream_created) {
    if (IsBidirectionalId(stream_id)) {
      incoming_bidirectional_streams_.push_back(stream_id);
      visitor_->OnIncomingBidirectionalStreamAvailable();
    } else {
      incoming_unidirectional_streams_.push_back(stream_id);
      visitor_->OnIncomingUnidirectionalStreamAvailable();
    }
  }
}

void EncapsulatedSession::InnerStream::ProcessCapsule(
    const quiche::Capsule& capsule) {
  switch (capsule.capsule_type()) {
    case CapsuleType::WT_STREAM:
    case CapsuleType::WT_STREAM_WITH_FIN: {
      if (fin_received_) {
        session_->OnFatalError(
            "Received stream data for a stream that has already received a "
            "FIN");
        return;
      }
      if (read_side_closed_) {
        // It is possible that we sent STOP_SENDING but it has not been received
        // yet. Ignore.
        return;
      }
      fin_received_ = capsule.capsule_type() == CapsuleType::WT_STREAM_WITH_FIN;
      const quiche::WebTransportStreamDataCapsule& data =
          capsule.web_transport_stream_data();
      if (!data.data.empty()) {
        incoming_reads_.push_back(IncomingRead{data.data, std::string()});
      }
      // Fast path: if the visitor consumes all of the incoming reads, we don't
      // need to copy data from the capsule parser.
      if (visitor_ != nullptr) {
        visitor_->OnCanRead();
      }
      // Slow path: copy all data that the visitor have not consumed.
      for (IncomingRead& read : incoming_reads_) {
        QUICHE_DCHECK(!read.data.empty());
        if (read.storage.empty()) {
          read.storage = std::string(read.data);
          read.data = read.storage;
        }
      }
      return;
    }
    case CapsuleType::WT_RESET_STREAM:
      CloseReadSide(capsule.web_transport_reset_stream().error_code);
      return;
    case CapsuleType::WT_STOP_SENDING:
      CloseWriteSide(capsule.web_transport_stop_sending().error_code);
      return;
    default:
      QUICHE_BUG(WT_H2_ProcessStreamCapsule_Unknown)
          << "Unexpected capsule dispatched to InnerStream: " << capsule;
      session_->OnFatalError(
          "Internal error: Unexpected capsule dispatched to InnerStream");
      return;
  }
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
    fatal_error_callback_ = nullptr;
  }
}

void EncapsulatedSession::OnWriteError(absl::Status error) {
  OnFatalError(absl::StrCat(
      error, " while trying to write encapsulated WebTransport data"));
}

EncapsulatedSession::InnerStream::InnerStream(EncapsulatedSession* session,
                                              StreamId id)
    : session_(session),
      id_(id),
      read_side_closed_(IsUnidirectionalId(id) &&
                        IsIdOpenedBy(id, session->perspective_)),
      write_side_closed_(IsUnidirectionalId(id) &&
                         !IsIdOpenedBy(id, session->perspective_)) {
  if (!write_side_closed_) {
    absl::Status status = session_->scheduler_.Register(id_, kDefaultPriority);
    if (!status.ok()) {
      QUICHE_BUG(WT_H2_FailedToRegisterNewStream) << status;
      session_->OnFatalError(
          "Failed to register new stream with the scheduler");
      return;
    }
  }
}

quiche::ReadStream::ReadResult EncapsulatedSession::InnerStream::Read(
    absl::Span<char> output) {
  const size_t total_size = output.size();
  for (const IncomingRead& read : incoming_reads_) {
    size_t size_to_read = std::min(read.size(), output.size());
    if (size_to_read == 0) {
      break;
    }
    memcpy(output.data(), read.data.data(), size_to_read);
    output = output.subspan(size_to_read);
  }
  bool fin_consumed = SkipBytes(total_size);
  return ReadResult{total_size, fin_consumed};
}
quiche::ReadStream::ReadResult EncapsulatedSession::InnerStream::Read(
    std::string* output) {
  const size_t total_size = ReadableBytes();
  const size_t initial_offset = output->size();
  output->resize(initial_offset + total_size);
  return Read(absl::Span<char>(&((*output)[initial_offset]), total_size));
}
size_t EncapsulatedSession::InnerStream::ReadableBytes() const {
  size_t total_size = 0;
  for (const IncomingRead& read : incoming_reads_) {
    total_size += read.size();
  }
  return total_size;
}
quiche::ReadStream::PeekResult
EncapsulatedSession::InnerStream::PeekNextReadableRegion() const {
  if (incoming_reads_.empty()) {
    return PeekResult{absl::string_view(), fin_received_, fin_received_};
  }
  return PeekResult{incoming_reads_.front().data,
                    fin_received_ && incoming_reads_.size() == 1,
                    fin_received_};
}

bool EncapsulatedSession::InnerStream::SkipBytes(size_t bytes) {
  size_t remaining = bytes;
  while (remaining > 0) {
    if (incoming_reads_.empty()) {
      QUICHE_BUG(WT_H2_SkipBytes_toomuch)
          << "Requested to skip " << remaining
          << " bytes that are not present in the read buffer.";
      return false;
    }
    IncomingRead& current = incoming_reads_.front();
    if (remaining < current.size()) {
      current.data = current.data.substr(remaining);
      return false;
    }
    remaining -= current.size();
    incoming_reads_.pop_front();
  }
  if (incoming_reads_.empty() && fin_received_) {
    fin_consumed_ = true;
    CloseReadSide(std::nullopt);
    return true;
  }
  return false;
}

absl::Status EncapsulatedSession::InnerStream::Writev(
    const absl::Span<const absl::string_view> data,
    const quiche::StreamWriteOptions& options) {
  if (write_side_closed_) {
    return absl::FailedPreconditionError(
        "Trying to write into an already-closed stream");
  }
  if (fin_buffered_) {
    return absl::FailedPreconditionError("FIN already buffered");
  }
  if (!CanWrite()) {
    return absl::FailedPreconditionError(
        "Trying to write into a stream when CanWrite() = false");
  }

  const absl::StatusOr<bool> should_yield =
      session_->scheduler_.ShouldYield(id_);
  if (!should_yield.ok()) {
    QUICHE_BUG(WT_H2_Writev_NotRegistered) << should_yield.status();
    session_->OnFatalError("Stream not registered with the scheduler");
    return absl::InternalError("Stream not registered with the scheduler");
  }
  const bool write_blocked = !session_->writer_->CanWrite() || *should_yield ||
                             !pending_write_.empty();
  if (write_blocked) {
    fin_buffered_ = options.send_fin();
    for (absl::string_view chunk : data) {
      absl::StrAppend(&pending_write_, chunk);
    }
    absl::Status status = session_->scheduler_.Schedule(id_);
    if (!status.ok()) {
      QUICHE_BUG(WT_H2_Writev_CantSchedule) << status;
      session_->OnFatalError("Could not schedule a write-blocked stream");
      return absl::InternalError("Could not schedule a write-blocked stream");
    }
    return absl::OkStatus();
  }

  size_t bytes_written = WriteInner(data, options.send_fin());
  // TODO: handle partial writes when flow control requires those.
  QUICHE_DCHECK(bytes_written == 0 ||
                bytes_written == quiche::TotalStringViewSpanSize(data));
  if (bytes_written == 0) {
    for (absl::string_view chunk : data) {
      absl::StrAppend(&pending_write_, chunk);
    }
  }

  if (options.send_fin()) {
    CloseWriteSide(std::nullopt);
  }
  return absl::OkStatus();
}

bool EncapsulatedSession::InnerStream::CanWrite() const {
  return session_->state_ != EncapsulatedSession::kSessionClosed &&
         !write_side_closed_ &&
         (pending_write_.size() <= session_->max_stream_data_buffered_);
}

void EncapsulatedSession::InnerStream::FlushPendingWrite() {
  QUICHE_DCHECK(!write_side_closed_);
  QUICHE_DCHECK(session_->writer_->CanWrite());
  QUICHE_DCHECK(!pending_write_.empty());
  absl::string_view to_write = pending_write_;
  size_t bytes_written =
      WriteInner(absl::MakeSpan(&to_write, 1), fin_buffered_);
  if (bytes_written < to_write.size()) {
    pending_write_ = pending_write_.substr(bytes_written);
    return;
  }
  pending_write_.clear();
  if (fin_buffered_) {
    CloseWriteSide(std::nullopt);
  }
  if (!write_side_closed_ && visitor_ != nullptr) {
    visitor_->OnCanWrite();
  }
}

size_t EncapsulatedSession::InnerStream::WriteInner(
    absl::Span<const absl::string_view> data, bool fin) {
  size_t total_size = quiche::TotalStringViewSpanSize(data);
  if (total_size == 0 && !fin) {
    session_->OnFatalError("Attempted to make an empty write with fin=false");
    return 0;
  }
  quiche::QuicheBuffer header =
      quiche::SerializeWebTransportStreamCapsuleHeader(id_, fin, total_size,
                                                       session_->allocator_);
  std::vector<absl::string_view> views_to_write;
  views_to_write.reserve(data.size() + 1);
  views_to_write.push_back(header.AsStringView());
  absl::c_copy(data, std::back_inserter(views_to_write));
  absl::Status write_status = session_->writer_->Writev(
      views_to_write, quiche::kDefaultStreamWriteOptions);
  if (!write_status.ok()) {
    session_->OnWriteError(write_status);
    return 0;
  }
  return total_size;
}

void EncapsulatedSession::InnerStream::AbruptlyTerminate(absl::Status error) {
  QUICHE_DLOG(INFO) << "Abruptly terminating the stream due to error: "
                    << error;
  ResetDueToInternalError();
}

void EncapsulatedSession::InnerStream::ResetWithUserCode(
    StreamErrorCode error) {
  if (reset_frame_sent_) {
    return;
  }
  reset_frame_sent_ = true;

  session_->SendControlCapsule(
      quiche::WebTransportResetStreamCapsule{id_, error});
  CloseWriteSide(std::nullopt);
}

void EncapsulatedSession::InnerStream::SendStopSending(StreamErrorCode error) {
  if (stop_sending_sent_) {
    return;
  }
  stop_sending_sent_ = true;

  session_->SendControlCapsule(
      quiche::WebTransportStopSendingCapsule{id_, error});
  CloseReadSide(std::nullopt);
}

void EncapsulatedSession::InnerStream::CloseReadSide(
    std::optional<StreamErrorCode> error) {
  if (read_side_closed_) {
    return;
  }
  read_side_closed_ = true;
  incoming_reads_.clear();
  if (error.has_value() && visitor_ != nullptr) {
    visitor_->OnResetStreamReceived(*error);
  }
  if (CanBeGarbageCollected()) {
    session_->streams_to_garbage_collect_.push_back(id_);
  }
}

void EncapsulatedSession::InnerStream::CloseWriteSide(
    std::optional<StreamErrorCode> error) {
  if (write_side_closed_) {
    return;
  }
  write_side_closed_ = true;
  pending_write_.clear();
  absl::Status status = session_->scheduler_.Unregister(id_);
  if (!status.ok()) {
    session_->OnFatalError("Failed to unregister closed stream");
    return;
  }
  if (error.has_value() && visitor_ != nullptr) {
    visitor_->OnStopSendingReceived(*error);
  }
  if (CanBeGarbageCollected()) {
    session_->streams_to_garbage_collect_.push_back(id_);
  }
}

void EncapsulatedSession::GarbageCollectStreams() {
  for (StreamId id : streams_to_garbage_collect_) {
    streams_.erase(id);
  }
  streams_to_garbage_collect_.clear();
}

void EncapsulatedSession::InnerStream::SetPriority(
    const StreamPriority& priority) {
  absl::Status status;
  status = session_->scheduler_.UpdateSendGroup(id_, priority.send_group_id);
  QUICHE_BUG_IF(EncapsulatedWebTransport_SetPriority_group, !status.ok())
      << status;
  status = session_->scheduler_.UpdateSendOrder(id_, priority.send_order);
  QUICHE_BUG_IF(EncapsulatedWebTransport_SetPriority_order, !status.ok())
      << status;
}
}  // namespace webtransport
