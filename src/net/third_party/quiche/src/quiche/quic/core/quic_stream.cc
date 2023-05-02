// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_stream.h"

#include <limits>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_flow_controller.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

using spdy::SpdyPriority;

namespace quic {

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

namespace {

QuicByteCount DefaultFlowControlWindow(ParsedQuicVersion version) {
  if (!version.AllowsLowFlowControlLimits()) {
    return kDefaultFlowControlSendWindow;
  }
  return 0;
}

QuicByteCount GetInitialStreamFlowControlWindowToSend(QuicSession* session,
                                                      QuicStreamId stream_id) {
  ParsedQuicVersion version = session->connection()->version();
  if (version.handshake_protocol != PROTOCOL_TLS1_3) {
    return session->config()->GetInitialStreamFlowControlWindowToSend();
  }

  // Unidirectional streams (v99 only).
  if (VersionHasIetfQuicFrames(version.transport_version) &&
      !QuicUtils::IsBidirectionalStreamId(stream_id, version)) {
    return session->config()
        ->GetInitialMaxStreamDataBytesUnidirectionalToSend();
  }

  if (QuicUtils::IsOutgoingStreamId(version, stream_id,
                                    session->perspective())) {
    return session->config()
        ->GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend();
  }

  return session->config()
      ->GetInitialMaxStreamDataBytesIncomingBidirectionalToSend();
}

QuicByteCount GetReceivedFlowControlWindow(QuicSession* session,
                                           QuicStreamId stream_id) {
  ParsedQuicVersion version = session->connection()->version();
  if (version.handshake_protocol != PROTOCOL_TLS1_3) {
    if (session->config()->HasReceivedInitialStreamFlowControlWindowBytes()) {
      return session->config()->ReceivedInitialStreamFlowControlWindowBytes();
    }

    return DefaultFlowControlWindow(version);
  }

  // Unidirectional streams (v99 only).
  if (VersionHasIetfQuicFrames(version.transport_version) &&
      !QuicUtils::IsBidirectionalStreamId(stream_id, version)) {
    if (session->config()
            ->HasReceivedInitialMaxStreamDataBytesUnidirectional()) {
      return session->config()
          ->ReceivedInitialMaxStreamDataBytesUnidirectional();
    }

    return DefaultFlowControlWindow(version);
  }

  if (QuicUtils::IsOutgoingStreamId(version, stream_id,
                                    session->perspective())) {
    if (session->config()
            ->HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional()) {
      return session->config()
          ->ReceivedInitialMaxStreamDataBytesOutgoingBidirectional();
    }

    return DefaultFlowControlWindow(version);
  }

  if (session->config()
          ->HasReceivedInitialMaxStreamDataBytesIncomingBidirectional()) {
    return session->config()
        ->ReceivedInitialMaxStreamDataBytesIncomingBidirectional();
  }

  return DefaultFlowControlWindow(version);
}

}  // namespace

PendingStream::PendingStream(QuicStreamId id, QuicSession* session)
    : id_(id),
      version_(session->version()),
      stream_delegate_(session),
      stream_bytes_read_(0),
      fin_received_(false),
      is_bidirectional_(QuicUtils::GetStreamType(id, session->perspective(),
                                                 /*peer_initiated = */ true,
                                                 session->version()) ==
                        BIDIRECTIONAL),
      connection_flow_controller_(session->flow_controller()),
      flow_controller_(session, id,
                       /*is_connection_flow_controller*/ false,
                       GetReceivedFlowControlWindow(session, id),
                       GetInitialStreamFlowControlWindowToSend(session, id),
                       kStreamReceiveWindowLimit,
                       session->flow_controller()->auto_tune_receive_window(),
                       session->flow_controller()),
      sequencer_(this) {}

void PendingStream::OnDataAvailable() {
  // Data should be kept in the sequencer so that
  // QuicSession::ProcessPendingStream() can read it.
}

void PendingStream::OnFinRead() { QUICHE_DCHECK(sequencer_.IsClosed()); }

void PendingStream::AddBytesConsumed(QuicByteCount bytes) {
  // It will be called when the metadata of the stream is consumed.
  flow_controller_.AddBytesConsumed(bytes);
  connection_flow_controller_->AddBytesConsumed(bytes);
}

void PendingStream::ResetWithError(QuicResetStreamError /*error*/) {
  // Currently PendingStream is only read-unidirectional. It shouldn't send
  // Reset.
  QUICHE_NOTREACHED();
}

void PendingStream::OnUnrecoverableError(QuicErrorCode error,
                                         const std::string& details) {
  stream_delegate_->OnStreamError(error, details);
}

void PendingStream::OnUnrecoverableError(QuicErrorCode error,
                                         QuicIetfTransportErrorCodes ietf_error,
                                         const std::string& details) {
  stream_delegate_->OnStreamError(error, ietf_error, details);
}

QuicStreamId PendingStream::id() const { return id_; }

ParsedQuicVersion PendingStream::version() const { return version_; }

void PendingStream::OnStreamFrame(const QuicStreamFrame& frame) {
  QUICHE_DCHECK_EQ(frame.stream_id, id_);

  bool is_stream_too_long =
      (frame.offset > kMaxStreamLength) ||
      (kMaxStreamLength - frame.offset < frame.data_length);
  if (is_stream_too_long) {
    // Close connection if stream becomes too long.
    QUIC_PEER_BUG(quic_peer_bug_12570_1)
        << "Receive stream frame reaches max stream length. frame offset "
        << frame.offset << " length " << frame.data_length;
    OnUnrecoverableError(QUIC_STREAM_LENGTH_OVERFLOW,
                         "Peer sends more data than allowed on this stream.");
    return;
  }

  if (frame.offset + frame.data_length > sequencer_.close_offset()) {
    OnUnrecoverableError(
        QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET,
        absl::StrCat(
            "Stream ", id_,
            " received data with offset: ", frame.offset + frame.data_length,
            ", which is beyond close offset: ", sequencer()->close_offset()));
    return;
  }

  if (frame.fin) {
    fin_received_ = true;
  }

  // This count includes duplicate data received.
  QuicByteCount frame_payload_size = frame.data_length;
  stream_bytes_read_ += frame_payload_size;

  // Flow control is interested in tracking highest received offset.
  // Only interested in received frames that carry data.
  if (frame_payload_size > 0 &&
      MaybeIncreaseHighestReceivedOffset(frame.offset + frame_payload_size)) {
    // As the highest received offset has changed, check to see if this is a
    // violation of flow control.
    if (flow_controller_.FlowControlViolation() ||
        connection_flow_controller_->FlowControlViolation()) {
      OnUnrecoverableError(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
                           "Flow control violation after increasing offset");
      return;
    }
  }

  sequencer_.OnStreamFrame(frame);
}

void PendingStream::OnRstStreamFrame(const QuicRstStreamFrame& frame) {
  QUICHE_DCHECK_EQ(frame.stream_id, id_);

  if (frame.byte_offset > kMaxStreamLength) {
    // Peer are not suppose to write bytes more than maxium allowed.
    OnUnrecoverableError(QUIC_STREAM_LENGTH_OVERFLOW,
                         "Reset frame stream offset overflow.");
    return;
  }

  const QuicStreamOffset kMaxOffset =
      std::numeric_limits<QuicStreamOffset>::max();
  if (sequencer()->close_offset() != kMaxOffset &&
      frame.byte_offset != sequencer()->close_offset()) {
    OnUnrecoverableError(
        QUIC_STREAM_MULTIPLE_OFFSET,
        absl::StrCat("Stream ", id_,
                     " received new final offset: ", frame.byte_offset,
                     ", which is different from close offset: ",
                     sequencer()->close_offset()));
    return;
  }

  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  if (flow_controller_.FlowControlViolation() ||
      connection_flow_controller_->FlowControlViolation()) {
    OnUnrecoverableError(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
                         "Flow control violation after increasing offset");
    return;
  }
}

void PendingStream::OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) {
  QUICHE_DCHECK(is_bidirectional_);
  flow_controller_.UpdateSendWindowOffset(frame.max_data);
}

bool PendingStream::MaybeIncreaseHighestReceivedOffset(
    QuicStreamOffset new_offset) {
  uint64_t increment =
      new_offset - flow_controller_.highest_received_byte_offset();
  if (!flow_controller_.UpdateHighestReceivedOffset(new_offset)) {
    return false;
  }

  // If |new_offset| increased the stream flow controller's highest received
  // offset, increase the connection flow controller's value by the incremental
  // difference.
  connection_flow_controller_->UpdateHighestReceivedOffset(
      connection_flow_controller_->highest_received_byte_offset() + increment);
  return true;
}

void PendingStream::OnStopSending(
    QuicResetStreamError stop_sending_error_code) {
  if (!stop_sending_error_code_) {
    stop_sending_error_code_ = stop_sending_error_code;
  }
}

void PendingStream::MarkConsumed(QuicByteCount num_bytes) {
  sequencer_.MarkConsumed(num_bytes);
}

void PendingStream::StopReading() {
  QUIC_DVLOG(1) << "Stop reading from pending stream " << id();
  sequencer_.StopReading();
}

QuicStream::QuicStream(PendingStream* pending, QuicSession* session,
                       bool is_static)
    : QuicStream(pending->id_, session, std::move(pending->sequencer_),
                 is_static,
                 QuicUtils::GetStreamType(pending->id_, session->perspective(),
                                          /*peer_initiated = */ true,
                                          session->version()),
                 pending->stream_bytes_read_, pending->fin_received_,
                 std::move(pending->flow_controller_),
                 pending->connection_flow_controller_) {
  QUICHE_DCHECK(session->version().HasIetfQuicFrames());
  sequencer_.set_stream(this);
}

namespace {

absl::optional<QuicFlowController> FlowController(QuicStreamId id,
                                                  QuicSession* session,
                                                  StreamType type) {
  if (type == CRYPTO) {
    // The only QuicStream with a StreamType of CRYPTO is QuicCryptoStream, when
    // it is using crypto frames instead of stream frames. The QuicCryptoStream
    // doesn't have any flow control in that case, so we don't create a
    // QuicFlowController for it.
    return absl::nullopt;
  }
  return QuicFlowController(
      session, id,
      /*is_connection_flow_controller*/ false,
      GetReceivedFlowControlWindow(session, id),
      GetInitialStreamFlowControlWindowToSend(session, id),
      kStreamReceiveWindowLimit,
      session->flow_controller()->auto_tune_receive_window(),
      session->flow_controller());
}

}  // namespace

QuicStream::QuicStream(QuicStreamId id, QuicSession* session, bool is_static,
                       StreamType type)
    : QuicStream(id, session, QuicStreamSequencer(this), is_static, type, 0,
                 false, FlowController(id, session, type),
                 session->flow_controller()) {}

QuicStream::QuicStream(QuicStreamId id, QuicSession* session,
                       QuicStreamSequencer sequencer, bool is_static,
                       StreamType type, uint64_t stream_bytes_read,
                       bool fin_received,
                       absl::optional<QuicFlowController> flow_controller,
                       QuicFlowController* connection_flow_controller)
    : sequencer_(std::move(sequencer)),
      id_(id),
      session_(session),
      stream_delegate_(session),
      stream_bytes_read_(stream_bytes_read),
      stream_error_(QuicResetStreamError::NoError()),
      connection_error_(QUIC_NO_ERROR),
      read_side_closed_(false),
      write_side_closed_(false),
      write_side_data_recvd_state_notified_(false),
      fin_buffered_(false),
      fin_sent_(false),
      fin_outstanding_(false),
      fin_lost_(false),
      fin_received_(fin_received),
      rst_sent_(false),
      rst_received_(false),
      stop_sending_sent_(false),
      flow_controller_(std::move(flow_controller)),
      connection_flow_controller_(connection_flow_controller),
      stream_contributes_to_connection_flow_control_(true),
      busy_counter_(0),
      add_random_padding_after_fin_(false),
      send_buffer_(
          session->connection()->helper()->GetStreamSendBufferAllocator()),
      buffered_data_threshold_(GetQuicFlag(quic_buffered_data_threshold)),
      is_static_(is_static),
      deadline_(QuicTime::Zero()),
      was_draining_(false),
      type_(VersionHasIetfQuicFrames(session->transport_version()) &&
                    type != CRYPTO
                ? QuicUtils::GetStreamType(id_, session->perspective(),
                                           session->IsIncomingStream(id_),
                                           session->version())
                : type),
      creation_time_(session->connection()->clock()->ApproximateNow()),
      perspective_(session->perspective()) {
  if (type_ == WRITE_UNIDIRECTIONAL) {
    fin_received_ = true;
    CloseReadSide();
  } else if (type_ == READ_UNIDIRECTIONAL) {
    fin_sent_ = true;
    CloseWriteSide();
  }
  if (type_ != CRYPTO) {
    stream_delegate_->RegisterStreamPriority(id, is_static_, priority_);
  }
}

QuicStream::~QuicStream() {
  if (session_ != nullptr && IsWaitingForAcks()) {
    QUIC_DVLOG(1)
        << ENDPOINT << "Stream " << id_
        << " gets destroyed while waiting for acks. stream_bytes_outstanding = "
        << send_buffer_.stream_bytes_outstanding()
        << ", fin_outstanding: " << fin_outstanding_;
  }
  if (stream_delegate_ != nullptr && type_ != CRYPTO) {
    stream_delegate_->UnregisterStreamPriority(id());
  }
}

void QuicStream::OnStreamFrame(const QuicStreamFrame& frame) {
  QUICHE_DCHECK_EQ(frame.stream_id, id_);

  QUICHE_DCHECK(!(read_side_closed_ && write_side_closed_));

  if (frame.fin && is_static_) {
    OnUnrecoverableError(QUIC_INVALID_STREAM_ID,
                         "Attempt to close a static stream");
    return;
  }

  if (type_ == WRITE_UNIDIRECTIONAL) {
    OnUnrecoverableError(QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM,
                         "Data received on write unidirectional stream");
    return;
  }

  bool is_stream_too_long =
      (frame.offset > kMaxStreamLength) ||
      (kMaxStreamLength - frame.offset < frame.data_length);
  if (is_stream_too_long) {
    // Close connection if stream becomes too long.
    QUIC_PEER_BUG(quic_peer_bug_10586_1)
        << "Receive stream frame on stream " << id_
        << " reaches max stream length. frame offset " << frame.offset
        << " length " << frame.data_length << ". " << sequencer_.DebugString();
    OnUnrecoverableError(
        QUIC_STREAM_LENGTH_OVERFLOW,
        absl::StrCat("Peer sends more data than allowed on stream ", id_,
                     ". frame: offset = ", frame.offset, ", length = ",
                     frame.data_length, ". ", sequencer_.DebugString()));
    return;
  }

  if (frame.offset + frame.data_length > sequencer_.close_offset()) {
    OnUnrecoverableError(
        QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET,
        absl::StrCat(
            "Stream ", id_,
            " received data with offset: ", frame.offset + frame.data_length,
            ", which is beyond close offset: ", sequencer_.close_offset()));
    return;
  }

  if (frame.fin && !fin_received_) {
    fin_received_ = true;
    if (fin_sent_) {
      QUICHE_DCHECK(!was_draining_);
      session_->StreamDraining(id_,
                               /*unidirectional=*/type_ != BIDIRECTIONAL);
      was_draining_ = true;
    }
  }

  if (read_side_closed_) {
    QUIC_DLOG(INFO)
        << ENDPOINT << "Stream " << frame.stream_id
        << " is closed for reading. Ignoring newly received stream data.";
    // The subclass does not want to read data:  blackhole the data.
    return;
  }

  // This count includes duplicate data received.
  QuicByteCount frame_payload_size = frame.data_length;
  stream_bytes_read_ += frame_payload_size;

  // Flow control is interested in tracking highest received offset.
  // Only interested in received frames that carry data.
  if (frame_payload_size > 0 &&
      MaybeIncreaseHighestReceivedOffset(frame.offset + frame_payload_size)) {
    // As the highest received offset has changed, check to see if this is a
    // violation of flow control.
    QUIC_BUG_IF(quic_bug_12570_2, !flow_controller_.has_value())
        << ENDPOINT << "OnStreamFrame called on stream without flow control";
    if ((flow_controller_.has_value() &&
         flow_controller_->FlowControlViolation()) ||
        connection_flow_controller_->FlowControlViolation()) {
      OnUnrecoverableError(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
                           "Flow control violation after increasing offset");
      return;
    }
  }

  sequencer_.OnStreamFrame(frame);
}

bool QuicStream::OnStopSending(QuicResetStreamError error) {
  // Do not reset the stream if all data has been sent and acknowledged.
  if (write_side_closed() && !IsWaitingForAcks()) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Ignoring STOP_SENDING for a write closed stream, id: "
                  << id_;
    return false;
  }

  if (is_static_) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING for a static stream, id: " << id_
                  << " Closing connection";
    OnUnrecoverableError(QUIC_INVALID_STREAM_ID,
                         "Received STOP_SENDING for a static stream");
    return false;
  }

  stream_error_ = error;
  MaybeSendRstStream(error);
  return true;
}

int QuicStream::num_frames_received() const {
  return sequencer_.num_frames_received();
}

int QuicStream::num_duplicate_frames_received() const {
  return sequencer_.num_duplicate_frames_received();
}

void QuicStream::OnStreamReset(const QuicRstStreamFrame& frame) {
  rst_received_ = true;
  if (frame.byte_offset > kMaxStreamLength) {
    // Peer are not suppose to write bytes more than maxium allowed.
    OnUnrecoverableError(QUIC_STREAM_LENGTH_OVERFLOW,
                         "Reset frame stream offset overflow.");
    return;
  }

  const QuicStreamOffset kMaxOffset =
      std::numeric_limits<QuicStreamOffset>::max();
  if (sequencer()->close_offset() != kMaxOffset &&
      frame.byte_offset != sequencer()->close_offset()) {
    OnUnrecoverableError(
        QUIC_STREAM_MULTIPLE_OFFSET,
        absl::StrCat("Stream ", id_,
                     " received new final offset: ", frame.byte_offset,
                     ", which is different from close offset: ",
                     sequencer_.close_offset()));
    return;
  }

  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  QUIC_BUG_IF(quic_bug_12570_3, !flow_controller_.has_value())
      << ENDPOINT << "OnStreamReset called on stream without flow control";
  if ((flow_controller_.has_value() &&
       flow_controller_->FlowControlViolation()) ||
      connection_flow_controller_->FlowControlViolation()) {
    OnUnrecoverableError(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
                         "Flow control violation after increasing offset");
    return;
  }

  stream_error_ = frame.error();
  // Google QUIC closes both sides of the stream in response to a
  // RESET_STREAM, IETF QUIC closes only the read side.
  if (!VersionHasIetfQuicFrames(transport_version())) {
    CloseWriteSide();
  }
  CloseReadSide();
}

void QuicStream::OnConnectionClosed(QuicErrorCode error,
                                    ConnectionCloseSource /*source*/) {
  if (read_side_closed_ && write_side_closed_) {
    return;
  }
  if (error != QUIC_NO_ERROR) {
    stream_error_ =
        QuicResetStreamError::FromInternal(QUIC_STREAM_CONNECTION_ERROR);
    connection_error_ = error;
  }

  CloseWriteSide();
  CloseReadSide();
}

void QuicStream::OnFinRead() {
  QUICHE_DCHECK(sequencer_.IsClosed());
  // OnFinRead can be called due to a FIN flag in a headers block, so there may
  // have been no OnStreamFrame call with a FIN in the frame.
  fin_received_ = true;
  // If fin_sent_ is true, then CloseWriteSide has already been called, and the
  // stream will be destroyed by CloseReadSide, so don't need to call
  // StreamDraining.
  CloseReadSide();
}

void QuicStream::SetFinSent() {
  QUICHE_DCHECK(!VersionUsesHttp3(transport_version()));
  fin_sent_ = true;
}

void QuicStream::Reset(QuicRstStreamErrorCode error) {
  ResetWithError(QuicResetStreamError::FromInternal(error));
}

void QuicStream::ResetWithError(QuicResetStreamError error) {
  stream_error_ = error;
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendStopSending(error);
  MaybeSendRstStream(error);

  if (read_side_closed_ && write_side_closed_ && !IsWaitingForAcks()) {
    session()->MaybeCloseZombieStream(id_);
  }
}

void QuicStream::ResetWriteSide(QuicResetStreamError error) {
  stream_error_ = error;
  MaybeSendRstStream(error);

  if (read_side_closed_ && write_side_closed_ && !IsWaitingForAcks()) {
    session()->MaybeCloseZombieStream(id_);
  }
}

void QuicStream::SendStopSending(QuicResetStreamError error) {
  stream_error_ = error;
  MaybeSendStopSending(error);

  if (read_side_closed_ && write_side_closed_ && !IsWaitingForAcks()) {
    session()->MaybeCloseZombieStream(id_);
  }
}

void QuicStream::OnUnrecoverableError(QuicErrorCode error,
                                      const std::string& details) {
  stream_delegate_->OnStreamError(error, details);
}

void QuicStream::OnUnrecoverableError(QuicErrorCode error,
                                      QuicIetfTransportErrorCodes ietf_error,
                                      const std::string& details) {
  stream_delegate_->OnStreamError(error, ietf_error, details);
}

const QuicStreamPriority& QuicStream::priority() const { return priority_; }

void QuicStream::SetPriority(const QuicStreamPriority& priority) {
  priority_ = priority;

  MaybeSendPriorityUpdateFrame();

  stream_delegate_->UpdateStreamPriority(id(), priority);
}

void QuicStream::WriteOrBufferData(
    absl::string_view data, bool fin,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
  QUIC_BUG_IF(quic_bug_12570_4,
              QuicUtils::IsCryptoStreamId(transport_version(), id_))
      << ENDPOINT
      << "WriteOrBufferData is used to send application data, use "
         "WriteOrBufferDataAtLevel to send crypto data.";
  return WriteOrBufferDataAtLevel(
      data, fin, session()->GetEncryptionLevelToSendApplicationData(),
      ack_listener);
}

void QuicStream::WriteOrBufferDataAtLevel(
    absl::string_view data, bool fin, EncryptionLevel level,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
  if (data.empty() && !fin) {
    QUIC_BUG(quic_bug_10586_2) << "data.empty() && !fin";
    return;
  }

  if (fin_buffered_) {
    QUIC_BUG(quic_bug_10586_3) << "Fin already buffered";
    return;
  }
  if (write_side_closed_) {
    QUIC_DLOG(ERROR) << ENDPOINT
                     << "Attempt to write when the write side is closed";
    if (type_ == READ_UNIDIRECTIONAL) {
      OnUnrecoverableError(QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM,
                           "Try to send data on read unidirectional stream");
    }
    return;
  }

  fin_buffered_ = fin;

  bool had_buffered_data = HasBufferedData();
  // Do not respect buffered data upper limit as WriteOrBufferData guarantees
  // all data to be consumed.
  if (data.length() > 0) {
    QuicStreamOffset offset = send_buffer_.stream_offset();
    if (kMaxStreamLength - offset < data.length()) {
      QUIC_BUG(quic_bug_10586_4) << "Write too many data via stream " << id_;
      OnUnrecoverableError(
          QUIC_STREAM_LENGTH_OVERFLOW,
          absl::StrCat("Write too many data via stream ", id_));
      return;
    }
    send_buffer_.SaveStreamData(data);
    OnDataBuffered(offset, data.length(), ack_listener);
  }
  if (!had_buffered_data && (HasBufferedData() || fin_buffered_)) {
    // Write data if there is no buffered data before.
    WriteBufferedData(level);
  }
}

void QuicStream::OnCanWrite() {
  if (HasDeadlinePassed()) {
    OnDeadlinePassed();
    return;
  }
  if (HasPendingRetransmission()) {
    WritePendingRetransmission();
    // Exit early to allow other streams to write pending retransmissions if
    // any.
    return;
  }

  if (write_side_closed_) {
    QUIC_DLOG(ERROR)
        << ENDPOINT << "Stream " << id()
        << " attempting to write new data when the write side is closed";
    return;
  }
  if (HasBufferedData() || (fin_buffered_ && !fin_sent_)) {
    WriteBufferedData(session()->GetEncryptionLevelToSendApplicationData());
  }
  if (!fin_buffered_ && !fin_sent_ && CanWriteNewData()) {
    // Notify upper layer to write new data when buffered data size is below
    // low water mark.
    OnCanWriteNewData();
  }
}

void QuicStream::MaybeSendBlocked() {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_5)
        << ENDPOINT << "MaybeSendBlocked called on stream without flow control";
    return;
  }
  flow_controller_->MaybeSendBlocked();
  if (!stream_contributes_to_connection_flow_control_) {
    return;
  }
  connection_flow_controller_->MaybeSendBlocked();

  // If the stream is blocked by connection-level flow control but not by
  // stream-level flow control, add the stream to the write blocked list so that
  // the stream will be given a chance to write when a connection-level
  // WINDOW_UPDATE arrives.
  if (!write_side_closed_ && connection_flow_controller_->IsBlocked() &&
      !flow_controller_->IsBlocked()) {
    session_->MarkConnectionLevelWriteBlocked(id());
  }
}

QuicConsumedData QuicStream::WriteMemSlice(quiche::QuicheMemSlice span,
                                           bool fin) {
  return WriteMemSlices(absl::MakeSpan(&span, 1), fin);
}

QuicConsumedData QuicStream::WriteMemSlices(
    absl::Span<quiche::QuicheMemSlice> span, bool fin) {
  QuicConsumedData consumed_data(0, false);
  if (span.empty() && !fin) {
    QUIC_BUG(quic_bug_10586_6) << "span.empty() && !fin";
    return consumed_data;
  }

  if (fin_buffered_) {
    QUIC_BUG(quic_bug_10586_7) << "Fin already buffered";
    return consumed_data;
  }

  if (write_side_closed_) {
    QUIC_DLOG(ERROR) << ENDPOINT << "Stream " << id()
                     << " attempting to write when the write side is closed";
    if (type_ == READ_UNIDIRECTIONAL) {
      OnUnrecoverableError(QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM,
                           "Try to send data on read unidirectional stream");
    }
    return consumed_data;
  }

  bool had_buffered_data = HasBufferedData();
  if (CanWriteNewData() || span.empty()) {
    consumed_data.fin_consumed = fin;
    if (!span.empty()) {
      // Buffer all data if buffered data size is below limit.
      QuicStreamOffset offset = send_buffer_.stream_offset();
      consumed_data.bytes_consumed = send_buffer_.SaveMemSliceSpan(span);
      if (offset > send_buffer_.stream_offset() ||
          kMaxStreamLength < send_buffer_.stream_offset()) {
        QUIC_BUG(quic_bug_10586_8) << "Write too many data via stream " << id_;
        OnUnrecoverableError(
            QUIC_STREAM_LENGTH_OVERFLOW,
            absl::StrCat("Write too many data via stream ", id_));
        return consumed_data;
      }
      OnDataBuffered(offset, consumed_data.bytes_consumed, nullptr);
    }
  }
  fin_buffered_ = consumed_data.fin_consumed;

  if (!had_buffered_data && (HasBufferedData() || fin_buffered_)) {
    // Write data if there is no buffered data before.
    WriteBufferedData(session()->GetEncryptionLevelToSendApplicationData());
  }

  return consumed_data;
}

bool QuicStream::HasPendingRetransmission() const {
  return send_buffer_.HasPendingRetransmission() || fin_lost_;
}

bool QuicStream::IsStreamFrameOutstanding(QuicStreamOffset offset,
                                          QuicByteCount data_length,
                                          bool fin) const {
  return send_buffer_.IsStreamDataOutstanding(offset, data_length) ||
         (fin && fin_outstanding_);
}

void QuicStream::CloseReadSide() {
  if (read_side_closed_) {
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Done reading from stream " << id();

  read_side_closed_ = true;
  sequencer_.ReleaseBuffer();

  if (write_side_closed_) {
    QUIC_DVLOG(1) << ENDPOINT << "Closing stream " << id();
    session_->OnStreamClosed(id());
    OnClose();
  }
}

void QuicStream::CloseWriteSide() {
  if (write_side_closed_) {
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Done writing to stream " << id();

  write_side_closed_ = true;
  if (read_side_closed_) {
    QUIC_DVLOG(1) << ENDPOINT << "Closing stream " << id();
    session_->OnStreamClosed(id());
    OnClose();
  }
}

void QuicStream::MaybeSendStopSending(QuicResetStreamError error) {
  if (stop_sending_sent_) {
    return;
  }

  if (!session()->version().UsesHttp3() && !error.ok()) {
    // In gQUIC, RST with error closes both read and write side.
    return;
  }

  if (session()->version().UsesHttp3()) {
    session()->MaybeSendStopSendingFrame(id(), error);
  } else {
    QUICHE_DCHECK_EQ(QUIC_STREAM_NO_ERROR, error.internal_code());
    session()->MaybeSendRstStreamFrame(id(), QuicResetStreamError::NoError(),
                                       stream_bytes_written());
  }
  stop_sending_sent_ = true;
  CloseReadSide();
}

void QuicStream::MaybeSendRstStream(QuicResetStreamError error) {
  if (rst_sent_) {
    return;
  }

  if (!session()->version().UsesHttp3()) {
    QUIC_BUG_IF(quic_bug_12570_5, error.ok());
    stop_sending_sent_ = true;
    CloseReadSide();
  }
  session()->MaybeSendRstStreamFrame(id(), error, stream_bytes_written());
  rst_sent_ = true;
  CloseWriteSide();
}

bool QuicStream::HasBufferedData() const {
  QUICHE_DCHECK_GE(send_buffer_.stream_offset(), stream_bytes_written());
  return send_buffer_.stream_offset() > stream_bytes_written();
}

ParsedQuicVersion QuicStream::version() const { return session_->version(); }

QuicTransportVersion QuicStream::transport_version() const {
  return session_->transport_version();
}

HandshakeProtocol QuicStream::handshake_protocol() const {
  return session_->connection()->version().handshake_protocol;
}

void QuicStream::StopReading() {
  QUIC_DVLOG(1) << ENDPOINT << "Stop reading from stream " << id();
  sequencer_.StopReading();
}

void QuicStream::OnClose() {
  QUICHE_DCHECK(read_side_closed_ && write_side_closed_);

  if (!fin_sent_ && !rst_sent_) {
    QUIC_BUG_IF(quic_bug_12570_6, session()->connection()->connected() &&
                                      session()->version().UsesHttp3())
        << "The stream should've already sent RST in response to "
           "STOP_SENDING";
    // For flow control accounting, tell the peer how many bytes have been
    // written on this stream before termination. Done here if needed, using a
    // RST_STREAM frame.
    MaybeSendRstStream(QUIC_RST_ACKNOWLEDGEMENT);
    session_->MaybeCloseZombieStream(id_);
  }

  if (!flow_controller_.has_value() ||
      flow_controller_->FlowControlViolation() ||
      connection_flow_controller_->FlowControlViolation()) {
    return;
  }
  // The stream is being closed and will not process any further incoming bytes.
  // As there may be more bytes in flight, to ensure that both endpoints have
  // the same connection level flow control state, mark all unreceived or
  // buffered bytes as consumed.
  QuicByteCount bytes_to_consume =
      flow_controller_->highest_received_byte_offset() -
      flow_controller_->bytes_consumed();
  AddBytesConsumed(bytes_to_consume);
}

void QuicStream::OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) {
  if (type_ == READ_UNIDIRECTIONAL) {
    OnUnrecoverableError(
        QUIC_WINDOW_UPDATE_RECEIVED_ON_READ_UNIDIRECTIONAL_STREAM,
        "WindowUpdateFrame received on READ_UNIDIRECTIONAL stream.");
    return;
  }

  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_9)
        << ENDPOINT
        << "OnWindowUpdateFrame called on stream without flow control";
    return;
  }

  if (flow_controller_->UpdateSendWindowOffset(frame.max_data)) {
    // Let session unblock this stream.
    session_->MarkConnectionLevelWriteBlocked(id_);
  }
}

bool QuicStream::MaybeIncreaseHighestReceivedOffset(
    QuicStreamOffset new_offset) {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_10)
        << ENDPOINT
        << "MaybeIncreaseHighestReceivedOffset called on stream without "
           "flow control";
    return false;
  }
  uint64_t increment =
      new_offset - flow_controller_->highest_received_byte_offset();
  if (!flow_controller_->UpdateHighestReceivedOffset(new_offset)) {
    return false;
  }

  // If |new_offset| increased the stream flow controller's highest received
  // offset, increase the connection flow controller's value by the incremental
  // difference.
  if (stream_contributes_to_connection_flow_control_) {
    connection_flow_controller_->UpdateHighestReceivedOffset(
        connection_flow_controller_->highest_received_byte_offset() +
        increment);
  }
  return true;
}

void QuicStream::AddBytesSent(QuicByteCount bytes) {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_11)
        << ENDPOINT << "AddBytesSent called on stream without flow control";
    return;
  }
  flow_controller_->AddBytesSent(bytes);
  if (stream_contributes_to_connection_flow_control_) {
    connection_flow_controller_->AddBytesSent(bytes);
  }
}

void QuicStream::AddBytesConsumed(QuicByteCount bytes) {
  if (type_ == CRYPTO) {
    // A stream with type CRYPTO has no flow control, so there's nothing this
    // function needs to do. This function still gets called by the
    // QuicStreamSequencers used by QuicCryptoStream.
    return;
  }
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_12570_7)
        << ENDPOINT
        << "AddBytesConsumed called on non-crypto stream without flow control";
    return;
  }
  // Only adjust stream level flow controller if still reading.
  if (!read_side_closed_) {
    flow_controller_->AddBytesConsumed(bytes);
  }

  if (stream_contributes_to_connection_flow_control_) {
    connection_flow_controller_->AddBytesConsumed(bytes);
  }
}

bool QuicStream::MaybeConfigSendWindowOffset(QuicStreamOffset new_offset,
                                             bool was_zero_rtt_rejected) {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_12)
        << ENDPOINT
        << "ConfigSendWindowOffset called on stream without flow control";
    return false;
  }

  // The validation code below is for QUIC with TLS only.
  if (new_offset < flow_controller_->send_window_offset()) {
    QUICHE_DCHECK(session()->version().UsesTls());
    if (was_zero_rtt_rejected && new_offset < flow_controller_->bytes_sent()) {
      // The client is given flow control window lower than what's written in
      // 0-RTT. This QUIC implementation is unable to retransmit them.
      QUIC_BUG_IF(quic_bug_12570_8, perspective_ == Perspective::IS_SERVER)
          << "Server streams' flow control should never be configured twice.";
      OnUnrecoverableError(
          QUIC_ZERO_RTT_UNRETRANSMITTABLE,
          absl::StrCat(
              "Server rejected 0-RTT, aborting because new stream max data ",
              new_offset, " for stream ", id_, " is less than currently used: ",
              flow_controller_->bytes_sent()));
      return false;
    } else if (session()->version().AllowsLowFlowControlLimits()) {
      // In IETF QUIC, if the client receives flow control limit lower than what
      // was resumed from 0-RTT, depending on 0-RTT status, it's either the
      // peer's fault or our implementation's fault.
      QUIC_BUG_IF(quic_bug_12570_9, perspective_ == Perspective::IS_SERVER)
          << "Server streams' flow control should never be configured twice.";
      OnUnrecoverableError(
          was_zero_rtt_rejected ? QUIC_ZERO_RTT_REJECTION_LIMIT_REDUCED
                                : QUIC_ZERO_RTT_RESUMPTION_LIMIT_REDUCED,
          absl::StrCat(
              was_zero_rtt_rejected ? "Server rejected 0-RTT, aborting because "
                                    : "",
              "new stream max data ", new_offset, " decreases current limit: ",
              flow_controller_->send_window_offset()));
      return false;
    }
  }

  if (flow_controller_->UpdateSendWindowOffset(new_offset)) {
    // Let session unblock this stream.
    session_->MarkConnectionLevelWriteBlocked(id_);
  }
  return true;
}

void QuicStream::AddRandomPaddingAfterFin() {
  add_random_padding_after_fin_ = true;
}

bool QuicStream::OnStreamFrameAcked(QuicStreamOffset offset,
                                    QuicByteCount data_length, bool fin_acked,
                                    QuicTime::Delta /*ack_delay_time*/,
                                    QuicTime /*receive_timestamp*/,
                                    QuicByteCount* newly_acked_length) {
  QUIC_DVLOG(1) << ENDPOINT << "stream " << id_ << " Acking "
                << "[" << offset << ", " << offset + data_length << "]"
                << " fin = " << fin_acked;
  *newly_acked_length = 0;
  if (!send_buffer_.OnStreamDataAcked(offset, data_length,
                                      newly_acked_length)) {
    OnUnrecoverableError(QUIC_INTERNAL_ERROR, "Trying to ack unsent data.");
    return false;
  }
  if (!fin_sent_ && fin_acked) {
    OnUnrecoverableError(QUIC_INTERNAL_ERROR, "Trying to ack unsent fin.");
    return false;
  }
  // Indicates whether ack listener's OnPacketAcked should be called.
  const bool new_data_acked =
      *newly_acked_length > 0 || (fin_acked && fin_outstanding_);
  if (fin_acked) {
    fin_outstanding_ = false;
    fin_lost_ = false;
  }
  if (!IsWaitingForAcks() && write_side_closed_ &&
      !write_side_data_recvd_state_notified_) {
    OnWriteSideInDataRecvdState();
    write_side_data_recvd_state_notified_ = true;
  }
  if (!IsWaitingForAcks() && read_side_closed_ && write_side_closed_) {
    session_->MaybeCloseZombieStream(id_);
  }
  return new_data_acked;
}

void QuicStream::OnStreamFrameRetransmitted(QuicStreamOffset offset,
                                            QuicByteCount data_length,
                                            bool fin_retransmitted) {
  send_buffer_.OnStreamDataRetransmitted(offset, data_length);
  if (fin_retransmitted) {
    fin_lost_ = false;
  }
}

void QuicStream::OnStreamFrameLost(QuicStreamOffset offset,
                                   QuicByteCount data_length, bool fin_lost) {
  QUIC_DVLOG(1) << ENDPOINT << "stream " << id_ << " Losting "
                << "[" << offset << ", " << offset + data_length << "]"
                << " fin = " << fin_lost;
  if (data_length > 0) {
    send_buffer_.OnStreamDataLost(offset, data_length);
  }
  if (fin_lost && fin_outstanding_) {
    fin_lost_ = true;
  }
}

bool QuicStream::RetransmitStreamData(QuicStreamOffset offset,
                                      QuicByteCount data_length, bool fin,
                                      TransmissionType type) {
  QUICHE_DCHECK(type == PTO_RETRANSMISSION);
  if (HasDeadlinePassed()) {
    OnDeadlinePassed();
    return true;
  }
  QuicIntervalSet<QuicStreamOffset> retransmission(offset,
                                                   offset + data_length);
  retransmission.Difference(bytes_acked());
  bool retransmit_fin = fin && fin_outstanding_;
  if (retransmission.Empty() && !retransmit_fin) {
    return true;
  }
  QuicConsumedData consumed(0, false);
  for (const auto& interval : retransmission) {
    QuicStreamOffset retransmission_offset = interval.min();
    QuicByteCount retransmission_length = interval.max() - interval.min();
    const bool can_bundle_fin =
        retransmit_fin && (retransmission_offset + retransmission_length ==
                           stream_bytes_written());
    consumed = stream_delegate_->WritevData(
        id_, retransmission_length, retransmission_offset,
        can_bundle_fin ? FIN : NO_FIN, type,
        session()->GetEncryptionLevelToSendApplicationData());
    QUIC_DVLOG(1) << ENDPOINT << "stream " << id_
                  << " is forced to retransmit stream data ["
                  << retransmission_offset << ", "
                  << retransmission_offset + retransmission_length
                  << ") and fin: " << can_bundle_fin
                  << ", consumed: " << consumed;
    OnStreamFrameRetransmitted(retransmission_offset, consumed.bytes_consumed,
                               consumed.fin_consumed);
    if (can_bundle_fin) {
      retransmit_fin = !consumed.fin_consumed;
    }
    if (consumed.bytes_consumed < retransmission_length ||
        (can_bundle_fin && !consumed.fin_consumed)) {
      // Connection is write blocked.
      return false;
    }
  }
  if (retransmit_fin) {
    QUIC_DVLOG(1) << ENDPOINT << "stream " << id_
                  << " retransmits fin only frame.";
    consumed = stream_delegate_->WritevData(
        id_, 0, stream_bytes_written(), FIN, type,
        session()->GetEncryptionLevelToSendApplicationData());
    if (!consumed.fin_consumed) {
      return false;
    }
  }
  return true;
}

bool QuicStream::IsWaitingForAcks() const {
  return (!rst_sent_ || stream_error_.ok()) &&
         (send_buffer_.stream_bytes_outstanding() || fin_outstanding_);
}

QuicByteCount QuicStream::ReadableBytes() const {
  return sequencer_.ReadableBytes();
}

bool QuicStream::WriteStreamData(QuicStreamOffset offset,
                                 QuicByteCount data_length,
                                 QuicDataWriter* writer) {
  QUICHE_DCHECK_LT(0u, data_length);
  QUIC_DVLOG(2) << ENDPOINT << "Write stream " << id_ << " data from offset "
                << offset << " length " << data_length;
  return send_buffer_.WriteStreamData(offset, data_length, writer);
}

void QuicStream::WriteBufferedData(EncryptionLevel level) {
  QUICHE_DCHECK(!write_side_closed_ && (HasBufferedData() || fin_buffered_));

  if (session_->ShouldYield(id())) {
    session_->MarkConnectionLevelWriteBlocked(id());
    return;
  }

  // Size of buffered data.
  QuicByteCount write_length = BufferedDataBytes();

  // A FIN with zero data payload should not be flow control blocked.
  bool fin_with_zero_data = (fin_buffered_ && write_length == 0);

  bool fin = fin_buffered_;

  // How much data flow control permits to be written.
  QuicByteCount send_window;
  if (flow_controller_.has_value()) {
    send_window = flow_controller_->SendWindowSize();
  } else {
    send_window = std::numeric_limits<QuicByteCount>::max();
    QUIC_BUG(quic_bug_10586_13)
        << ENDPOINT
        << "WriteBufferedData called on stream without flow control";
  }
  if (stream_contributes_to_connection_flow_control_) {
    send_window =
        std::min(send_window, connection_flow_controller_->SendWindowSize());
  }

  if (send_window == 0 && !fin_with_zero_data) {
    // Quick return if nothing can be sent.
    MaybeSendBlocked();
    return;
  }

  if (write_length > send_window) {
    // Don't send the FIN unless all the data will be sent.
    fin = false;

    // Writing more data would be a violation of flow control.
    write_length = send_window;
    QUIC_DVLOG(1) << "stream " << id() << " shortens write length to "
                  << write_length << " due to flow control";
  }

  StreamSendingState state = fin ? FIN : NO_FIN;
  if (fin && add_random_padding_after_fin_) {
    state = FIN_AND_PADDING;
  }
  QuicConsumedData consumed_data =
      stream_delegate_->WritevData(id(), write_length, stream_bytes_written(),
                                   state, NOT_RETRANSMISSION, level);

  OnStreamDataConsumed(consumed_data.bytes_consumed);

  AddBytesSent(consumed_data.bytes_consumed);
  QUIC_DVLOG(1) << ENDPOINT << "stream " << id_ << " sends "
                << stream_bytes_written() << " bytes "
                << " and has buffered data " << BufferedDataBytes() << " bytes."
                << " fin is sent: " << consumed_data.fin_consumed
                << " fin is buffered: " << fin_buffered_;

  // The write may have generated a write error causing this stream to be
  // closed. If so, simply return without marking the stream write blocked.
  if (write_side_closed_) {
    return;
  }

  if (consumed_data.bytes_consumed == write_length) {
    if (!fin_with_zero_data) {
      MaybeSendBlocked();
    }
    if (fin && consumed_data.fin_consumed) {
      QUICHE_DCHECK(!fin_sent_);
      fin_sent_ = true;
      fin_outstanding_ = true;
      if (fin_received_) {
        QUICHE_DCHECK(!was_draining_);
        session_->StreamDraining(id_,
                                 /*unidirectional=*/type_ != BIDIRECTIONAL);
        was_draining_ = true;
      }
      CloseWriteSide();
    } else if (fin && !consumed_data.fin_consumed && !write_side_closed_) {
      session_->MarkConnectionLevelWriteBlocked(id());
    }
  } else {
    session_->MarkConnectionLevelWriteBlocked(id());
  }
  if (consumed_data.bytes_consumed > 0 || consumed_data.fin_consumed) {
    busy_counter_ = 0;
  }
}

uint64_t QuicStream::BufferedDataBytes() const {
  QUICHE_DCHECK_GE(send_buffer_.stream_offset(), stream_bytes_written());
  return send_buffer_.stream_offset() - stream_bytes_written();
}

bool QuicStream::CanWriteNewData() const {
  return BufferedDataBytes() < buffered_data_threshold_;
}

bool QuicStream::CanWriteNewDataAfterData(QuicByteCount length) const {
  return (BufferedDataBytes() + length) < buffered_data_threshold_;
}

uint64_t QuicStream::stream_bytes_written() const {
  return send_buffer_.stream_bytes_written();
}

const QuicIntervalSet<QuicStreamOffset>& QuicStream::bytes_acked() const {
  return send_buffer_.bytes_acked();
}

void QuicStream::OnStreamDataConsumed(QuicByteCount bytes_consumed) {
  send_buffer_.OnStreamDataConsumed(bytes_consumed);
}

void QuicStream::WritePendingRetransmission() {
  while (HasPendingRetransmission()) {
    QuicConsumedData consumed(0, false);
    if (!send_buffer_.HasPendingRetransmission()) {
      QUIC_DVLOG(1) << ENDPOINT << "stream " << id_
                    << " retransmits fin only frame.";
      consumed = stream_delegate_->WritevData(
          id_, 0, stream_bytes_written(), FIN, LOSS_RETRANSMISSION,
          session()->GetEncryptionLevelToSendApplicationData());
      fin_lost_ = !consumed.fin_consumed;
      if (fin_lost_) {
        // Connection is write blocked.
        return;
      }
    } else {
      StreamPendingRetransmission pending =
          send_buffer_.NextPendingRetransmission();
      // Determine whether the lost fin can be bundled with the data.
      const bool can_bundle_fin =
          fin_lost_ &&
          (pending.offset + pending.length == stream_bytes_written());
      consumed = stream_delegate_->WritevData(
          id_, pending.length, pending.offset, can_bundle_fin ? FIN : NO_FIN,
          LOSS_RETRANSMISSION,
          session()->GetEncryptionLevelToSendApplicationData());
      QUIC_DVLOG(1) << ENDPOINT << "stream " << id_
                    << " tries to retransmit stream data [" << pending.offset
                    << ", " << pending.offset + pending.length
                    << ") and fin: " << can_bundle_fin
                    << ", consumed: " << consumed;
      OnStreamFrameRetransmitted(pending.offset, consumed.bytes_consumed,
                                 consumed.fin_consumed);
      if (consumed.bytes_consumed < pending.length ||
          (can_bundle_fin && !consumed.fin_consumed)) {
        // Connection is write blocked.
        return;
      }
    }
  }
}

bool QuicStream::MaybeSetTtl(QuicTime::Delta ttl) {
  if (is_static_) {
    QUIC_BUG(quic_bug_10586_14) << "Cannot set TTL of a static stream.";
    return false;
  }
  if (deadline_.IsInitialized()) {
    QUIC_DLOG(WARNING) << "Deadline has already been set.";
    return false;
  }
  QuicTime now = session()->connection()->clock()->ApproximateNow();
  deadline_ = now + ttl;
  return true;
}

bool QuicStream::HasDeadlinePassed() const {
  if (!deadline_.IsInitialized()) {
    // No deadline has been set.
    return false;
  }
  QuicTime now = session()->connection()->clock()->ApproximateNow();
  if (now < deadline_) {
    return false;
  }
  // TTL expired.
  QUIC_DVLOG(1) << "stream " << id() << " deadline has passed";
  return true;
}

void QuicStream::OnDeadlinePassed() { Reset(QUIC_STREAM_TTL_EXPIRED); }

bool QuicStream::IsFlowControlBlocked() const {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_15)
        << "Trying to access non-existent flow controller.";
    return false;
  }
  return flow_controller_->IsBlocked();
}

QuicStreamOffset QuicStream::highest_received_byte_offset() const {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_16)
        << "Trying to access non-existent flow controller.";
    return 0;
  }
  return flow_controller_->highest_received_byte_offset();
}

void QuicStream::UpdateReceiveWindowSize(QuicStreamOffset size) {
  if (!flow_controller_.has_value()) {
    QUIC_BUG(quic_bug_10586_17)
        << "Trying to access non-existent flow controller.";
    return;
  }
  flow_controller_->UpdateReceiveWindowSize(size);
}

absl::optional<QuicByteCount> QuicStream::GetSendWindow() const {
  return flow_controller_.has_value()
             ? absl::optional<QuicByteCount>(flow_controller_->SendWindowSize())
             : absl::nullopt;
}

absl::optional<QuicByteCount> QuicStream::GetReceiveWindow() const {
  return flow_controller_.has_value()
             ? absl::optional<QuicByteCount>(
                   flow_controller_->receive_window_size())
             : absl::nullopt;
}

void QuicStream::OnStreamCreatedFromPendingStream() {
  sequencer()->SetUnblocked();
}

}  // namespace quic
