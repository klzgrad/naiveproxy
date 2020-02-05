// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_stream.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_flow_controller.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

using spdy::SpdyPriority;

namespace quic {

#define ENDPOINT \
  (session_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

namespace {

size_t DefaultFlowControlWindow(ParsedQuicVersion version) {
  if (!version.AllowsLowFlowControlLimits()) {
    return kDefaultFlowControlSendWindow;
  }
  return 0;
}

size_t GetInitialStreamFlowControlWindowToSend(QuicSession* session,
                                               QuicStreamId stream_id) {
  ParsedQuicVersion version = session->connection()->version();
  if (version.handshake_protocol != PROTOCOL_TLS1_3) {
    return session->config()->GetInitialStreamFlowControlWindowToSend();
  }

  // Unidirectional streams (v99 only).
  if (VersionHasIetfQuicFrames(version.transport_version) &&
      !QuicUtils::IsBidirectionalStreamId(stream_id)) {
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

size_t GetReceivedFlowControlWindow(QuicSession* session,
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
      !QuicUtils::IsBidirectionalStreamId(stream_id)) {
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

// static
const SpdyPriority QuicStream::kDefaultPriority;

PendingStream::PendingStream(QuicStreamId id, QuicSession* session)
    : id_(id),
      session_(session),
      stream_bytes_read_(0),
      fin_received_(false),
      connection_flow_controller_(session->flow_controller()),
      flow_controller_(session,
                       id,
                       /*is_connection_flow_controller*/ false,
                       GetReceivedFlowControlWindow(session, id),
                       GetInitialStreamFlowControlWindowToSend(session, id),
                       kStreamReceiveWindowLimit,
                       session_->flow_controller()->auto_tune_receive_window(),
                       session_->flow_controller()),
      sequencer_(this) {}

void PendingStream::OnDataAvailable() {
  // Data should be kept in the sequencer so that
  // QuicSession::ProcessPendingStream() can read it.
}

void PendingStream::OnFinRead() {
  DCHECK(sequencer_.IsClosed());
}

void PendingStream::AddBytesConsumed(QuicByteCount bytes) {
  // It will be called when the metadata of the stream is consumed.
  flow_controller_.AddBytesConsumed(bytes);
  connection_flow_controller_->AddBytesConsumed(bytes);
}

void PendingStream::Reset(QuicRstStreamErrorCode /*error*/) {
  // Currently PendingStream is only read-unidirectional. It shouldn't send
  // Reset.
  DCHECK_EQ(READ_UNIDIRECTIONAL,
            QuicUtils::GetStreamType(id_, session_->perspective(),
                                     /*peer_initiated = */ true));
  QUIC_NOTREACHED();
}

void PendingStream::CloseConnectionWithDetails(QuicErrorCode error,
                                               const std::string& details) {
  session_->connection()->CloseConnection(
      error, details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

QuicStreamId PendingStream::id() const {
  return id_;
}

const QuicSocketAddress& PendingStream::PeerAddressOfLatestPacket() const {
  return session_->connection()->last_packet_source_address();
}

void PendingStream::OnStreamFrame(const QuicStreamFrame& frame) {
  DCHECK_EQ(frame.stream_id, id_);

  bool is_stream_too_long =
      (frame.offset > kMaxStreamLength) ||
      (kMaxStreamLength - frame.offset < frame.data_length);
  if (is_stream_too_long) {
    // Close connection if stream becomes too long.
    QUIC_PEER_BUG
        << "Receive stream frame reaches max stream length. frame offset "
        << frame.offset << " length " << frame.data_length;
    CloseConnectionWithDetails(
        QUIC_STREAM_LENGTH_OVERFLOW,
        "Peer sends more data than allowed on this stream.");
    return;
  }

  if (frame.offset + frame.data_length > sequencer_.close_offset()) {
    CloseConnectionWithDetails(
        QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET,
        QuicStrCat(
            "Stream ", id_,
            " received data with offset: ", frame.offset + frame.data_length,
            ", which is beyond close offset: ", sequencer()->close_offset()));
    return;
  }

  if (frame.fin) {
    fin_received_ = true;
  }

  // This count includes duplicate data received.
  size_t frame_payload_size = frame.data_length;
  stream_bytes_read_ += frame_payload_size;

  // Flow control is interested in tracking highest received offset.
  // Only interested in received frames that carry data.
  if (frame_payload_size > 0 &&
      MaybeIncreaseHighestReceivedOffset(frame.offset + frame_payload_size)) {
    // As the highest received offset has changed, check to see if this is a
    // violation of flow control.
    if (flow_controller_.FlowControlViolation() ||
        connection_flow_controller_->FlowControlViolation()) {
      CloseConnectionWithDetails(
          QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
          "Flow control violation after increasing offset");
      return;
    }
  }

  sequencer_.OnStreamFrame(frame);
}

void PendingStream::OnRstStreamFrame(const QuicRstStreamFrame& frame) {
  DCHECK_EQ(frame.stream_id, id_);

  if (frame.byte_offset > kMaxStreamLength) {
    // Peer are not suppose to write bytes more than maxium allowed.
    CloseConnectionWithDetails(QUIC_STREAM_LENGTH_OVERFLOW,
                               "Reset frame stream offset overflow.");
    return;
  }

  const QuicStreamOffset kMaxOffset =
      std::numeric_limits<QuicStreamOffset>::max();
  if (sequencer()->close_offset() != kMaxOffset &&
      frame.byte_offset != sequencer()->close_offset()) {
    CloseConnectionWithDetails(
        QUIC_STREAM_MULTIPLE_OFFSET,
        QuicStrCat("Stream ", id_,
                   " received new final offset: ", frame.byte_offset,
                   ", which is different from close offset: ",
                   sequencer()->close_offset()));
    return;
  }

  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  if (flow_controller_.FlowControlViolation() ||
      connection_flow_controller_->FlowControlViolation()) {
    CloseConnectionWithDetails(
        QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
        "Flow control violation after increasing offset");
    return;
  }
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

void PendingStream::MarkConsumed(size_t num_bytes) {
  sequencer_.MarkConsumed(num_bytes);
}

void PendingStream::StopReading() {
  QUIC_DVLOG(1) << "Stop reading from pending stream " << id();
  sequencer_.StopReading();
}

QuicStream::QuicStream(PendingStream* pending, StreamType type, bool is_static)
    : QuicStream(pending->id_,
                 pending->session_,
                 std::move(pending->sequencer_),
                 is_static,
                 type,
                 pending->stream_bytes_read_,
                 pending->fin_received_,
                 std::move(pending->flow_controller_),
                 pending->connection_flow_controller_) {
  sequencer_.set_stream(this);
}

namespace {

QuicOptional<QuicFlowController> FlowController(QuicStreamId id,
                                                QuicSession* session,
                                                StreamType type) {
  if (type == CRYPTO) {
    // The only QuicStream with a StreamType of CRYPTO is QuicCryptoStream, when
    // it is using crypto frames instead of stream frames. The QuicCryptoStream
    // doesn't have any flow control in that case, so we don't create a
    // QuicFlowController for it.
    return QuicOptional<QuicFlowController>();
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

QuicStream::QuicStream(QuicStreamId id,
                       QuicSession* session,
                       bool is_static,
                       StreamType type)
    : QuicStream(id,
                 session,
                 QuicStreamSequencer(this),
                 is_static,
                 type,
                 0,
                 false,
                 FlowController(id, session, type),
                 session->flow_controller()) {}

QuicStream::QuicStream(QuicStreamId id,
                       QuicSession* session,
                       QuicStreamSequencer sequencer,
                       bool is_static,
                       StreamType type,
                       uint64_t stream_bytes_read,
                       bool fin_received,
                       QuicOptional<QuicFlowController> flow_controller,
                       QuicFlowController* connection_flow_controller)
    : sequencer_(std::move(sequencer)),
      id_(id),
      session_(session),
      precedence_(
          session->use_http2_priority_write_scheduler()
              ? spdy::SpdyStreamPrecedence(0,
                                           spdy::kHttp2DefaultStreamWeight,
                                           false)
              : spdy::SpdyStreamPrecedence(kDefaultPriority)),
      stream_bytes_read_(stream_bytes_read),
      stream_error_(QUIC_STREAM_NO_ERROR),
      connection_error_(QUIC_NO_ERROR),
      read_side_closed_(false),
      write_side_closed_(false),
      fin_buffered_(false),
      fin_sent_(false),
      fin_outstanding_(false),
      fin_lost_(false),
      fin_received_(fin_received),
      rst_sent_(false),
      rst_received_(false),
      flow_controller_(std::move(flow_controller)),
      connection_flow_controller_(connection_flow_controller),
      stream_contributes_to_connection_flow_control_(true),
      busy_counter_(0),
      add_random_padding_after_fin_(false),
      send_buffer_(
          session->connection()->helper()->GetStreamSendBufferAllocator()),
      buffered_data_threshold_(GetQuicFlag(FLAGS_quic_buffered_data_threshold)),
      is_static_(is_static),
      deadline_(QuicTime::Zero()),
      type_(VersionHasIetfQuicFrames(session->transport_version()) &&
                    type != CRYPTO
                ? QuicUtils::GetStreamType(id_,
                                           session->perspective(),
                                           session->IsIncomingStream(id_))
                : type) {
  if (type_ == WRITE_UNIDIRECTIONAL) {
    set_fin_received(true);
    CloseReadSide();
  } else if (type_ == READ_UNIDIRECTIONAL) {
    set_fin_sent(true);
    CloseWriteSide();
  }
  if (type_ != CRYPTO) {
    session_->RegisterStreamPriority(id, is_static_, precedence_);
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
  if (session_ != nullptr && type_ != CRYPTO) {
    session_->UnregisterStreamPriority(id(), is_static_);
  }
}

void QuicStream::OnStreamFrame(const QuicStreamFrame& frame) {
  DCHECK_EQ(frame.stream_id, id_);

  DCHECK(!(read_side_closed_ && write_side_closed_));

  if (type_ == WRITE_UNIDIRECTIONAL) {
    CloseConnectionWithDetails(
        QUIC_DATA_RECEIVED_ON_WRITE_UNIDIRECTIONAL_STREAM,
        "Data received on write unidirectional stream");
    return;
  }

  bool is_stream_too_long =
      (frame.offset > kMaxStreamLength) ||
      (kMaxStreamLength - frame.offset < frame.data_length);
  if (is_stream_too_long) {
    // Close connection if stream becomes too long.
    QUIC_PEER_BUG << "Receive stream frame on stream " << id_
                  << " reaches max stream length. frame offset " << frame.offset
                  << " length " << frame.data_length << ". "
                  << sequencer_.DebugString();
    CloseConnectionWithDetails(
        QUIC_STREAM_LENGTH_OVERFLOW,
        QuicStrCat("Peer sends more data than allowed on stream ", id_,
                   ". frame: offset = ", frame.offset, ", length = ",
                   frame.data_length, ". ", sequencer_.DebugString()));
    return;
  }

  if (frame.offset + frame.data_length > sequencer_.close_offset()) {
    if (!GetQuicReloadableFlag(quic_close_connection_on_wrong_offset)) {
      Reset(QUIC_DATA_AFTER_CLOSE_OFFSET);
      return;
    }
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_close_connection_on_wrong_offset, 1, 2);
    CloseConnectionWithDetails(
        QUIC_STREAM_DATA_BEYOND_CLOSE_OFFSET,
        QuicStrCat(
            "Stream ", id_,
            " received data with offset: ", frame.offset + frame.data_length,
            ", which is beyond close offset: ", sequencer_.close_offset()));
    return;
  }

  if (frame.fin) {
    fin_received_ = true;
    if (fin_sent_) {
      session_->StreamDraining(id_);
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
  size_t frame_payload_size = frame.data_length;
  stream_bytes_read_ += frame_payload_size;

  // Flow control is interested in tracking highest received offset.
  // Only interested in received frames that carry data.
  if (frame_payload_size > 0 &&
      MaybeIncreaseHighestReceivedOffset(frame.offset + frame_payload_size)) {
    // As the highest received offset has changed, check to see if this is a
    // violation of flow control.
    if (flow_controller_->FlowControlViolation() ||
        connection_flow_controller_->FlowControlViolation()) {
      CloseConnectionWithDetails(
          QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
          "Flow control violation after increasing offset");
      return;
    }
  }

  sequencer_.OnStreamFrame(frame);
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
    CloseConnectionWithDetails(QUIC_STREAM_LENGTH_OVERFLOW,
                               "Reset frame stream offset overflow.");
    return;
  }

  if (GetQuicReloadableFlag(quic_close_connection_on_wrong_offset)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_close_connection_on_wrong_offset, 2, 2);
    const QuicStreamOffset kMaxOffset =
        std::numeric_limits<QuicStreamOffset>::max();
    if (sequencer()->close_offset() != kMaxOffset &&
        frame.byte_offset != sequencer()->close_offset()) {
      CloseConnectionWithDetails(
          QUIC_STREAM_MULTIPLE_OFFSET,
          QuicStrCat("Stream ", id_,
                     " received new final offset: ", frame.byte_offset,
                     ", which is different from close offset: ",
                     sequencer_.close_offset()));
      return;
    }
  }

  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  if (flow_controller_->FlowControlViolation() ||
      connection_flow_controller_->FlowControlViolation()) {
    CloseConnectionWithDetails(
        QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
        "Flow control violation after increasing offset");
    return;
  }

  stream_error_ = frame.error_code;
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
    stream_error_ = QUIC_STREAM_CONNECTION_ERROR;
    connection_error_ = error;
  }

  CloseWriteSide();
  CloseReadSide();
}

void QuicStream::OnFinRead() {
  DCHECK(sequencer_.IsClosed());
  // OnFinRead can be called due to a FIN flag in a headers block, so there may
  // have been no OnStreamFrame call with a FIN in the frame.
  fin_received_ = true;
  // If fin_sent_ is true, then CloseWriteSide has already been called, and the
  // stream will be destroyed by CloseReadSide, so don't need to call
  // StreamDraining.
  CloseReadSide();
}

void QuicStream::Reset(QuicRstStreamErrorCode error) {
  stream_error_ = error;
  // Sending a RstStream results in calling CloseStream.
  session()->SendRstStream(id(), error, stream_bytes_written());
  rst_sent_ = true;
}

void QuicStream::CloseConnectionWithDetails(QuicErrorCode error,
                                            const std::string& details) {
  session()->connection()->CloseConnection(
      error, details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

const spdy::SpdyStreamPrecedence& QuicStream::precedence() const {
  return precedence_;
}

void QuicStream::SetPriority(const spdy::SpdyStreamPrecedence& precedence) {
  precedence_ = precedence;
  session_->UpdateStreamPriority(id(), precedence);
}

void QuicStream::WriteOrBufferData(
    QuicStringPiece data,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  if (data.empty() && !fin) {
    QUIC_BUG << "data.empty() && !fin";
    return;
  }

  if (fin_buffered_) {
    QUIC_BUG << "Fin already buffered";
    return;
  }
  if (write_side_closed_) {
    QUIC_DLOG(ERROR) << ENDPOINT
                     << "Attempt to write when the write side is closed";
    if (type_ == READ_UNIDIRECTIONAL) {
      CloseConnectionWithDetails(
          QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM,
          "Try to send data on read unidirectional stream");
    }
    return;
  }

  fin_buffered_ = fin;

  bool had_buffered_data = HasBufferedData();
  // Do not respect buffered data upper limit as WriteOrBufferData guarantees
  // all data to be consumed.
  if (data.length() > 0) {
    struct iovec iov(QuicUtils::MakeIovec(data));
    QuicStreamOffset offset = send_buffer_.stream_offset();
    if (kMaxStreamLength - offset < data.length()) {
      QUIC_BUG << "Write too many data via stream " << id_;
      CloseConnectionWithDetails(
          QUIC_STREAM_LENGTH_OVERFLOW,
          QuicStrCat("Write too many data via stream ", id_));
      return;
    }
    send_buffer_.SaveStreamData(&iov, 1, 0, data.length());
    OnDataBuffered(offset, data.length(), ack_listener);
  }
  if (!had_buffered_data && (HasBufferedData() || fin_buffered_)) {
    // Write data if there is no buffered data before.
    WriteBufferedData();
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
    WriteBufferedData();
  }
  if (!fin_buffered_ && !fin_sent_ && CanWriteNewData()) {
    // Notify upper layer to write new data when buffered data size is below
    // low water mark.
    OnCanWriteNewData();
  }
}

void QuicStream::MaybeSendBlocked() {
  if (flow_controller_->ShouldSendBlocked()) {
    session_->SendBlocked(id_);
  }
  if (!stream_contributes_to_connection_flow_control_) {
    return;
  }
  if (connection_flow_controller_->ShouldSendBlocked()) {
    session_->SendBlocked(QuicUtils::GetInvalidStreamId(transport_version()));
  }
  // If the stream is blocked by connection-level flow control but not by
  // stream-level flow control, add the stream to the write blocked list so that
  // the stream will be given a chance to write when a connection-level
  // WINDOW_UPDATE arrives.
  if (connection_flow_controller_->IsBlocked() &&
      !flow_controller_->IsBlocked()) {
    session_->MarkConnectionLevelWriteBlocked(id());
  }
}

QuicConsumedData QuicStream::WriteMemSlices(QuicMemSliceSpan span, bool fin) {
  QuicConsumedData consumed_data(0, false);
  if (span.empty() && !fin) {
    QUIC_BUG << "span.empty() && !fin";
    return consumed_data;
  }

  if (fin_buffered_) {
    QUIC_BUG << "Fin already buffered";
    return consumed_data;
  }

  if (write_side_closed_) {
    QUIC_DLOG(ERROR) << ENDPOINT << "Stream " << id()
                     << " attempting to write when the write side is closed";
    if (type_ == READ_UNIDIRECTIONAL) {
      CloseConnectionWithDetails(
          QUIC_TRY_TO_WRITE_DATA_ON_READ_UNIDIRECTIONAL_STREAM,
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
        QUIC_BUG << "Write too many data via stream " << id_;
        CloseConnectionWithDetails(
            QUIC_STREAM_LENGTH_OVERFLOW,
            QuicStrCat("Write too many data via stream ", id_));
        return consumed_data;
      }
      OnDataBuffered(offset, consumed_data.bytes_consumed, nullptr);
    }
  }
  fin_buffered_ = consumed_data.fin_consumed;

  if (!had_buffered_data && (HasBufferedData() || fin_buffered_)) {
    // Write data if there is no buffered data before.
    WriteBufferedData();
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
    session_->CloseStream(id());
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
    session_->CloseStream(id());
  }
}

bool QuicStream::HasBufferedData() const {
  DCHECK_GE(send_buffer_.stream_offset(), stream_bytes_written());
  return send_buffer_.stream_offset() > stream_bytes_written();
}

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

const QuicSocketAddress& QuicStream::PeerAddressOfLatestPacket() const {
  return session_->connection()->last_packet_source_address();
}

void QuicStream::OnClose() {
  CloseReadSide();
  CloseWriteSide();

  if (!fin_sent_ && !rst_sent_) {
    // For flow control accounting, tell the peer how many bytes have been
    // written on this stream before termination. Done here if needed, using a
    // RST_STREAM frame.
    QUIC_DLOG(INFO) << ENDPOINT << "Sending RST_STREAM in OnClose: " << id();
    session_->SendRstStream(id(), QUIC_RST_ACKNOWLEDGEMENT,
                            stream_bytes_written());
    session_->OnStreamDoneWaitingForAcks(id_);
    rst_sent_ = true;
  }

  if (flow_controller_->FlowControlViolation() ||
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
    CloseConnectionWithDetails(
        QUIC_WINDOW_UPDATE_RECEIVED_ON_READ_UNIDIRECTIONAL_STREAM,
        "WindowUpdateFrame received on READ_UNIDIRECTIONAL stream.");
    return;
  }

  if (flow_controller_->UpdateSendWindowOffset(frame.max_data)) {
    // Let session unblock this stream.
    session_->MarkConnectionLevelWriteBlocked(id_);
  }
}

bool QuicStream::MaybeIncreaseHighestReceivedOffset(
    QuicStreamOffset new_offset) {
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
  // Only adjust stream level flow controller if still reading.
  if (!read_side_closed_) {
    flow_controller_->AddBytesConsumed(bytes);
  }

  if (stream_contributes_to_connection_flow_control_) {
    connection_flow_controller_->AddBytesConsumed(bytes);
  }
}

void QuicStream::UpdateSendWindowOffset(QuicStreamOffset new_window) {
  if (flow_controller_->UpdateSendWindowOffset(new_window)) {
    // Let session unblock this stream.
    session_->MarkConnectionLevelWriteBlocked(id_);
  }
}

void QuicStream::AddRandomPaddingAfterFin() {
  add_random_padding_after_fin_ = true;
}

bool QuicStream::OnStreamFrameAcked(QuicStreamOffset offset,
                                    QuicByteCount data_length,
                                    bool fin_acked,
                                    QuicTime::Delta /*ack_delay_time*/,
                                    QuicByteCount* newly_acked_length) {
  QUIC_DVLOG(1) << ENDPOINT << "stream " << id_ << " Acking "
                << "[" << offset << ", " << offset + data_length << "]"
                << " fin = " << fin_acked;
  *newly_acked_length = 0;
  if (!send_buffer_.OnStreamDataAcked(offset, data_length,
                                      newly_acked_length)) {
    CloseConnectionWithDetails(QUIC_INTERNAL_ERROR,
                               "Trying to ack unsent data.");
    return false;
  }
  if (!fin_sent_ && fin_acked) {
    CloseConnectionWithDetails(QUIC_INTERNAL_ERROR,
                               "Trying to ack unsent fin.");
    return false;
  }
  // Indicates whether ack listener's OnPacketAcked should be called.
  const bool new_data_acked =
      *newly_acked_length > 0 || (fin_acked && fin_outstanding_);
  if (fin_acked) {
    fin_outstanding_ = false;
    fin_lost_ = false;
  }
  if (!IsWaitingForAcks()) {
    session_->OnStreamDoneWaitingForAcks(id_);
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
                                   QuicByteCount data_length,
                                   bool fin_lost) {
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
                                      QuicByteCount data_length,
                                      bool fin) {
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
    consumed = session()->WritevData(this, id_, retransmission_length,
                                     retransmission_offset,
                                     can_bundle_fin ? FIN : NO_FIN);
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
    consumed = session()->WritevData(this, id_, 0, stream_bytes_written(), FIN);
    if (!consumed.fin_consumed) {
      return false;
    }
  }
  return true;
}

bool QuicStream::IsWaitingForAcks() const {
  return (!rst_sent_ || stream_error_ == QUIC_STREAM_NO_ERROR) &&
         (send_buffer_.stream_bytes_outstanding() || fin_outstanding_);
}

size_t QuicStream::ReadableBytes() const {
  return sequencer_.ReadableBytes();
}

bool QuicStream::WriteStreamData(QuicStreamOffset offset,
                                 QuicByteCount data_length,
                                 QuicDataWriter* writer) {
  DCHECK_LT(0u, data_length);
  QUIC_DVLOG(2) << ENDPOINT << "Write stream " << id_ << " data from offset "
                << offset << " length " << data_length;
  return send_buffer_.WriteStreamData(offset, data_length, writer);
}

void QuicStream::WriteBufferedData() {
  DCHECK(!write_side_closed_ && (HasBufferedData() || fin_buffered_));

  if (session_->ShouldYield(id())) {
    session_->MarkConnectionLevelWriteBlocked(id());
    return;
  }

  // Size of buffered data.
  size_t write_length = BufferedDataBytes();

  // A FIN with zero data payload should not be flow control blocked.
  bool fin_with_zero_data = (fin_buffered_ && write_length == 0);

  bool fin = fin_buffered_;

  // How much data flow control permits to be written.
  QuicByteCount send_window = flow_controller_->SendWindowSize();
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
    write_length = static_cast<size_t>(send_window);
    QUIC_DVLOG(1) << "stream " << id() << " shortens write length to "
                  << write_length << " due to flow control";
  }
  session_->SetTransmissionType(NOT_RETRANSMISSION);

  StreamSendingState state = fin ? FIN : NO_FIN;
  if (fin && add_random_padding_after_fin_) {
    state = FIN_AND_PADDING;
  }
  QuicConsumedData consumed_data = session_->WritevData(
      this, id(), write_length, stream_bytes_written(), state);

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
      fin_sent_ = true;
      fin_outstanding_ = true;
      if (fin_received_) {
        session_->StreamDraining(id_);
      }
      CloseWriteSide();
    } else if (fin && !consumed_data.fin_consumed) {
      session_->MarkConnectionLevelWriteBlocked(id());
    }
  } else {
    session_->MarkConnectionLevelWriteBlocked(id());
  }
  if (consumed_data.bytes_consumed > 0 || consumed_data.fin_consumed) {
    busy_counter_ = 0;
  }

  if (IsWaitingForAcks()) {
    session_->OnStreamWaitingForAcks(id_);
  }
}

uint64_t QuicStream::BufferedDataBytes() const {
  DCHECK_GE(send_buffer_.stream_offset(), stream_bytes_written());
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

void QuicStream::OnStreamDataConsumed(size_t bytes_consumed) {
  send_buffer_.OnStreamDataConsumed(bytes_consumed);
}

void QuicStream::WritePendingRetransmission() {
  while (HasPendingRetransmission()) {
    QuicConsumedData consumed(0, false);
    if (!send_buffer_.HasPendingRetransmission()) {
      QUIC_DVLOG(1) << ENDPOINT << "stream " << id_
                    << " retransmits fin only frame.";
      consumed =
          session()->WritevData(this, id_, 0, stream_bytes_written(), FIN);
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
      consumed =
          session()->WritevData(this, id_, pending.length, pending.offset,
                                can_bundle_fin ? FIN : NO_FIN);
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
    QUIC_BUG << "Cannot set TTL of a static stream.";
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

void QuicStream::OnDeadlinePassed() {
  Reset(QUIC_STREAM_TTL_EXPIRED);
}

void QuicStream::SendStopSending(uint16_t code) {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // If the connection is not version 99, do nothing.
    // Do not QUIC_BUG or anything; the application really does not need to know
    // what version the connection is in.
    return;
  }
  session_->SendStopSending(code, id_);
}

}  // namespace quic
