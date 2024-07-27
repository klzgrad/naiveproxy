// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_session.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"
#include "quiche/quic/core/frames/quic_window_update_frame.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_flow_controller.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/quic_write_blocked_list.h"
#include "quiche/quic/core/web_transport_write_blocked_list.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_server_stats.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

namespace {

class ClosedStreamsCleanUpDelegate : public QuicAlarm::Delegate {
 public:
  explicit ClosedStreamsCleanUpDelegate(QuicSession* session)
      : session_(session) {}
  ClosedStreamsCleanUpDelegate(const ClosedStreamsCleanUpDelegate&) = delete;
  ClosedStreamsCleanUpDelegate& operator=(const ClosedStreamsCleanUpDelegate&) =
      delete;

  QuicConnectionContext* GetConnectionContext() override {
    return (session_->connection() == nullptr)
               ? nullptr
               : session_->connection()->context();
  }

  void OnAlarm() override { session_->CleanUpClosedStreams(); }

 private:
  QuicSession* session_;
};

class StreamCountResetAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit StreamCountResetAlarmDelegate(QuicSession* session)
      : session_(session) {}
  StreamCountResetAlarmDelegate(const StreamCountResetAlarmDelegate&) = delete;
  StreamCountResetAlarmDelegate& operator=(
      const StreamCountResetAlarmDelegate&) = delete;

  QuicConnectionContext* GetConnectionContext() override {
    return (session_->connection() == nullptr)
               ? nullptr
               : session_->connection()->context();
  }

  void OnAlarm() override { session_->OnStreamCountReset(); }

 private:
  QuicSession* session_;
};

std::unique_ptr<QuicWriteBlockedListInterface> CreateWriteBlockedList(
    QuicPriorityType priority_type) {
  switch (priority_type) {
    case QuicPriorityType::kHttp:
      return std::make_unique<QuicWriteBlockedList>();
    case QuicPriorityType::kWebTransport:
      return std::make_unique<WebTransportWriteBlockedList>();
  }
  QUICHE_NOTREACHED();
  return nullptr;
}

}  // namespace

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicSession::QuicSession(
    QuicConnection* connection, Visitor* owner, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicStreamCount num_expected_unidirectional_static_streams)
    : QuicSession(connection, owner, config, supported_versions,
                  num_expected_unidirectional_static_streams, nullptr) {}

QuicSession::QuicSession(
    QuicConnection* connection, Visitor* owner, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicStreamCount num_expected_unidirectional_static_streams,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
    QuicPriorityType priority_type)
    : connection_(connection),
      perspective_(connection->perspective()),
      visitor_(owner),
      write_blocked_streams_(CreateWriteBlockedList(priority_type)),
      config_(config),
      stream_id_manager_(perspective(), connection->transport_version(),
                         kDefaultMaxStreamsPerConnection,
                         config_.GetMaxBidirectionalStreamsToSend()),
      ietf_streamid_manager_(perspective(), connection->version(), this, 0,
                             num_expected_unidirectional_static_streams,
                             config_.GetMaxBidirectionalStreamsToSend(),
                             config_.GetMaxUnidirectionalStreamsToSend() +
                                 num_expected_unidirectional_static_streams),
      num_draining_streams_(0),
      num_outgoing_draining_streams_(0),
      num_static_streams_(0),
      num_zombie_streams_(0),
      flow_controller_(
          this, QuicUtils::GetInvalidStreamId(connection->transport_version()),
          /*is_connection_flow_controller*/ true,
          connection->version().AllowsLowFlowControlLimits()
              ? 0
              : kMinimumFlowControlSendWindow,
          config_.GetInitialSessionFlowControlWindowToSend(),
          kSessionReceiveWindowLimit, perspective() == Perspective::IS_SERVER,
          nullptr),
      currently_writing_stream_id_(0),
      transport_goaway_sent_(false),
      transport_goaway_received_(false),
      control_frame_manager_(this),
      last_message_id_(0),
      datagram_queue_(this, std::move(datagram_observer)),
      closed_streams_clean_up_alarm_(nullptr),
      supported_versions_(supported_versions),
      is_configured_(false),
      was_zero_rtt_rejected_(false),
      liveness_testing_in_progress_(false),
      stream_count_reset_alarm_(
          absl::WrapUnique<QuicAlarm>(connection->alarm_factory()->CreateAlarm(
              new StreamCountResetAlarmDelegate(this)))),
      priority_type_(priority_type) {
  closed_streams_clean_up_alarm_ =
      absl::WrapUnique<QuicAlarm>(connection_->alarm_factory()->CreateAlarm(
          new ClosedStreamsCleanUpDelegate(this)));
  if (VersionHasIetfQuicFrames(transport_version())) {
    config_.SetMaxUnidirectionalStreamsToSend(
        config_.GetMaxUnidirectionalStreamsToSend() +
        num_expected_unidirectional_static_streams);
  }
}

void QuicSession::Initialize() {
  connection_->set_visitor(this);
  connection_->SetSessionNotifier(this);
  connection_->SetDataProducer(this);
  connection_->SetUnackedMapInitialCapacity();
  connection_->SetFromConfig(config_);
  if (perspective_ == Perspective::IS_CLIENT) {
    if (config_.HasClientRequestedIndependentOption(kAFFE, perspective_) &&
        version().HasIetfQuicFrames()) {
      connection_->set_can_receive_ack_frequency_frame();
      config_.SetMinAckDelayMs(kDefaultMinAckDelayTimeMs);
    }
  }
  if (perspective() == Perspective::IS_SERVER &&
      connection_->version().handshake_protocol == PROTOCOL_TLS1_3) {
    config_.SetStatelessResetTokenToSend(GetStatelessResetToken());
  }

  connection_->CreateConnectionIdManager();

  // On the server side, version negotiation has been done by the dispatcher,
  // and the server session is created with the right version.
  if (perspective() == Perspective::IS_SERVER) {
    connection_->OnSuccessfulVersionNegotiation();
  }

  if (QuicVersionUsesCryptoFrames(transport_version())) {
    return;
  }

  QUICHE_DCHECK_EQ(QuicUtils::GetCryptoStreamId(transport_version()),
                   GetMutableCryptoStream()->id());
}

QuicSession::~QuicSession() {
  if (closed_streams_clean_up_alarm_ != nullptr) {
    closed_streams_clean_up_alarm_->PermanentCancel();
  }
  if (stream_count_reset_alarm_ != nullptr) {
    stream_count_reset_alarm_->PermanentCancel();
  }
}

PendingStream* QuicSession::PendingStreamOnStreamFrame(
    const QuicStreamFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QuicStreamId stream_id = frame.stream_id;

  PendingStream* pending = GetOrCreatePendingStream(stream_id);

  if (!pending) {
    if (frame.fin) {
      QuicStreamOffset final_byte_offset = frame.offset + frame.data_length;
      OnFinalByteOffsetReceived(stream_id, final_byte_offset);
    }
    return nullptr;
  }

  pending->OnStreamFrame(frame);
  if (!connection()->connected()) {
    return nullptr;
  }
  return pending;
}

bool QuicSession::MaybeProcessPendingStream(PendingStream* pending) {
  QUICHE_DCHECK(pending != nullptr && connection()->connected());

  if (ExceedsPerLoopStreamLimit()) {
    QUIC_DLOG(INFO) << "Skip processing pending stream " << pending->id()
                    << " because it exceeds per loop limit.";
    QUIC_CODE_COUNT_N(quic_pending_stream, 1, 3);
    return false;
  }

  QuicStreamId stream_id = pending->id();
  std::optional<QuicResetStreamError> stop_sending_error_code =
      pending->GetStopSendingErrorCode();
  QUIC_DLOG(INFO) << "Process pending stream " << pending->id();
  QuicStream* stream = ProcessPendingStream(pending);
  if (stream != nullptr) {
    // The pending stream should now be in the scope of normal streams.
    QUICHE_DCHECK(IsClosedStream(stream_id) || IsOpenStream(stream_id))
        << "Stream " << stream_id << " not created";
    if (!stream->pending_duration().IsZero()) {
      QUIC_SERVER_HISTOGRAM_TIMES("QuicStream.PendingDurationUs",
                                  stream->pending_duration().ToMicroseconds(),
                                  0, 1000 * 100, 20,
                                  "Time a stream has been pending at server.");
      ++connection()->mutable_stats().num_total_pending_streams;
    }
    pending_stream_map_.erase(stream_id);
    if (stop_sending_error_code) {
      stream->OnStopSending(*stop_sending_error_code);
      if (!connection()->connected()) {
        return false;
      }
    }
    stream->OnStreamCreatedFromPendingStream();
    return connection()->connected();
  }
  // At this point, none of the bytes has been successfully consumed by the
  // application layer. We should close the pending stream even if it is
  // bidirectionl as no application will be able to write in a bidirectional
  // stream with zero byte as input.
  if (pending->sequencer()->IsClosed()) {
    ClosePendingStream(stream_id);
  }
  return connection()->connected();
}

void QuicSession::PendingStreamOnWindowUpdateFrame(
    const QuicWindowUpdateFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  PendingStream* pending = GetOrCreatePendingStream(frame.stream_id);
  if (pending) {
    pending->OnWindowUpdateFrame(frame);
  }
}

void QuicSession::PendingStreamOnStopSendingFrame(
    const QuicStopSendingFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  PendingStream* pending = GetOrCreatePendingStream(frame.stream_id);
  if (pending) {
    pending->OnStopSending(frame.error());
  }
}

void QuicSession::OnStreamFrame(const QuicStreamFrame& frame) {
  QuicStreamId stream_id = frame.stream_id;
  if (stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received data for an invalid stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (ShouldProcessFrameByPendingStream(STREAM_FRAME, stream_id)) {
    PendingStream* pending = PendingStreamOnStreamFrame(frame);
    if (pending != nullptr && IsEncryptionEstablished()) {
      MaybeProcessPendingStream(pending);
    }
    return;
  }

  QuicStream* stream = GetOrCreateStream(stream_id);

  if (!stream) {
    // The stream no longer exists, but we may still be interested in the
    // final stream byte offset sent by the peer. A frame with a FIN can give
    // us this offset.
    if (frame.fin) {
      QuicStreamOffset final_byte_offset = frame.offset + frame.data_length;
      OnFinalByteOffsetReceived(stream_id, final_byte_offset);
    }
    return;
  }
  stream->OnStreamFrame(frame);
}

void QuicSession::OnCryptoFrame(const QuicCryptoFrame& frame) {
  GetMutableCryptoStream()->OnCryptoFrame(frame);
}

void QuicSession::OnStopSendingFrame(const QuicStopSendingFrame& frame) {
  // STOP_SENDING is in IETF QUIC only.
  QUICHE_DCHECK(VersionHasIetfQuicFrames(transport_version()));
  QUICHE_DCHECK(QuicVersionUsesCryptoFrames(transport_version()));

  QuicStreamId stream_id = frame.stream_id;
  // If Stream ID is invalid then close the connection.
  // TODO(ianswett): This check is redundant to checks for IsClosedStream,
  // but removing it requires removing multiple QUICHE_DCHECKs.
  // TODO(ianswett): Multiple QUIC_DVLOGs could be QUIC_PEER_BUGs.
  if (stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING with invalid stream_id: "
                  << stream_id << " Closing connection";
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received STOP_SENDING for an invalid stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  // If stream_id is READ_UNIDIRECTIONAL, close the connection.
  if (QuicUtils::GetStreamType(stream_id, perspective(),
                               IsIncomingStream(stream_id),
                               version()) == READ_UNIDIRECTIONAL) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING for a read-only stream_id: "
                  << stream_id << ".";
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received STOP_SENDING for a read-only stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (visitor_) {
    visitor_->OnStopSendingReceived(frame);
  }
  if (ShouldProcessFrameByPendingStream(STOP_SENDING_FRAME, stream_id)) {
    PendingStreamOnStopSendingFrame(frame);
    return;
  }

  QuicStream* stream = GetOrCreateStream(stream_id);
  if (!stream) {
    // Errors are handled by GetOrCreateStream.
    return;
  }

  stream->OnStopSending(frame.error());
}

void QuicSession::OnPacketDecrypted(EncryptionLevel level) {
  GetMutableCryptoStream()->OnPacketDecrypted(level);
  if (liveness_testing_in_progress_) {
    liveness_testing_in_progress_ = false;
    OnCanCreateNewOutgoingStream(/*unidirectional=*/false);
  }
}

void QuicSession::OnOneRttPacketAcknowledged() {
  GetMutableCryptoStream()->OnOneRttPacketAcknowledged();
}

void QuicSession::OnHandshakePacketSent() {
  GetMutableCryptoStream()->OnHandshakePacketSent();
}

std::unique_ptr<QuicDecrypter>
QuicSession::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  return GetMutableCryptoStream()->AdvanceKeysAndCreateCurrentOneRttDecrypter();
}

std::unique_ptr<QuicEncrypter> QuicSession::CreateCurrentOneRttEncrypter() {
  return GetMutableCryptoStream()->CreateCurrentOneRttEncrypter();
}

void QuicSession::PendingStreamOnRstStream(const QuicRstStreamFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QuicStreamId stream_id = frame.stream_id;

  PendingStream* pending = GetOrCreatePendingStream(stream_id);

  if (!pending) {
    HandleRstOnValidNonexistentStream(frame);
    return;
  }

  pending->OnRstStreamFrame(frame);
  // At this point, none of the bytes has been consumed by the application
  // layer. It is safe to close the pending stream even if it is bidirectionl as
  // no application will be able to write in a bidirectional stream with zero
  // byte as input.
  ClosePendingStream(stream_id);
}

void QuicSession::OnRstStream(const QuicRstStreamFrame& frame) {
  QuicStreamId stream_id = frame.stream_id;
  if (stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received data for an invalid stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (VersionHasIetfQuicFrames(transport_version()) &&
      QuicUtils::GetStreamType(stream_id, perspective(),
                               IsIncomingStream(stream_id),
                               version()) == WRITE_UNIDIRECTIONAL) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received RESET_STREAM for a write-only stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (visitor_) {
    visitor_->OnRstStreamReceived(frame);
  }

  if (ShouldProcessFrameByPendingStream(RST_STREAM_FRAME, stream_id)) {
    PendingStreamOnRstStream(frame);
    return;
  }

  QuicStream* stream = GetOrCreateStream(stream_id);

  if (!stream) {
    HandleRstOnValidNonexistentStream(frame);
    return;  // Errors are handled by GetOrCreateStream.
  }
  stream->OnStreamReset(frame);
}

void QuicSession::OnGoAway(const QuicGoAwayFrame& /*frame*/) {
  QUIC_BUG_IF(quic_bug_12435_1, version().UsesHttp3())
      << "gQUIC GOAWAY received on version " << version();

  transport_goaway_received_ = true;
}

void QuicSession::OnMessageReceived(absl::string_view message) {
  QUIC_DVLOG(1) << ENDPOINT << "Received message of length "
                << message.length();
  QUIC_DVLOG(2) << ENDPOINT << "Contents of message of length "
                << message.length() << ":" << std::endl
                << quiche::QuicheTextUtils::HexDump(message);
}

void QuicSession::OnHandshakeDoneReceived() {
  QUIC_DVLOG(1) << ENDPOINT << "OnHandshakeDoneReceived";
  GetMutableCryptoStream()->OnHandshakeDoneReceived();
}

void QuicSession::OnNewTokenReceived(absl::string_view token) {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  GetMutableCryptoStream()->OnNewTokenReceived(token);
}

// static
void QuicSession::RecordConnectionCloseAtServer(QuicErrorCode error,
                                                ConnectionCloseSource source) {
  if (error != QUIC_NO_ERROR) {
    if (source == ConnectionCloseSource::FROM_SELF) {
      QUIC_SERVER_HISTOGRAM_ENUM(
          "quic_server_connection_close_errors", error, QUIC_LAST_ERROR,
          "QuicErrorCode for server-closed connections.");
    } else {
      QUIC_SERVER_HISTOGRAM_ENUM(
          "quic_client_connection_close_errors", error, QUIC_LAST_ERROR,
          "QuicErrorCode for client-closed connections.");
    }
  }
}

void QuicSession::OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                                     ConnectionCloseSource source) {
  QUICHE_DCHECK(!connection_->connected());
  if (perspective() == Perspective::IS_SERVER) {
    RecordConnectionCloseAtServer(frame.quic_error_code, source);
  }

  if (on_closed_frame_.quic_error_code == QUIC_NO_ERROR) {
    // Save all of the connection close information
    on_closed_frame_ = frame;
    source_ = source;
  }

  GetMutableCryptoStream()->OnConnectionClosed(frame.quic_error_code, source);

  PerformActionOnActiveStreams([this, frame, source](QuicStream* stream) {
    QuicStreamId id = stream->id();
    stream->OnConnectionClosed(frame.quic_error_code, source);
    auto it = stream_map_.find(id);
    if (it != stream_map_.end()) {
      QUIC_BUG_IF(quic_bug_12435_2, !it->second->IsZombie())
          << ENDPOINT << "Non-zombie stream " << id
          << " failed to close under OnConnectionClosed";
    }
    return true;
  });

  closed_streams_clean_up_alarm_->Cancel();
  stream_count_reset_alarm_->Cancel();

  if (visitor_) {
    visitor_->OnConnectionClosed(connection_->GetOneActiveServerConnectionId(),
                                 frame.quic_error_code, frame.error_details,
                                 source);
  }
}

void QuicSession::OnWriteBlocked() {
  if (!connection_->connected()) {
    return;
  }
  if (visitor_) {
    visitor_->OnWriteBlocked(connection_);
  }
}

void QuicSession::OnSuccessfulVersionNegotiation(
    const ParsedQuicVersion& /*version*/) {}

void QuicSession::OnPacketReceived(const QuicSocketAddress& /*self_address*/,
                                   const QuicSocketAddress& peer_address,
                                   bool is_connectivity_probe) {
  QUICHE_DCHECK(!connection_->ignore_gquic_probing());
  if (is_connectivity_probe && perspective() == Perspective::IS_SERVER) {
    // Server only sends back a connectivity probe after received a
    // connectivity probe from a new peer address.
    connection_->SendConnectivityProbingPacket(nullptr, peer_address);
  }
}

void QuicSession::OnPathDegrading() {
  if (visitor_) {
    visitor_->OnPathDegrading();
  }
}

void QuicSession::OnForwardProgressMadeAfterPathDegrading() {}

bool QuicSession::AllowSelfAddressChange() const { return false; }

void QuicSession::OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) {
  // Stream may be closed by the time we receive a WINDOW_UPDATE, so we can't
  // assume that it still exists.
  QuicStreamId stream_id = frame.stream_id;
  if (stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
    // This is a window update that applies to the connection, rather than an
    // individual stream.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received connection level flow control window "
                     "update with max data: "
                  << frame.max_data;
    flow_controller_.UpdateSendWindowOffset(frame.max_data);
    return;
  }

  if (VersionHasIetfQuicFrames(transport_version()) &&
      QuicUtils::GetStreamType(stream_id, perspective(),
                               IsIncomingStream(stream_id),
                               version()) == READ_UNIDIRECTIONAL) {
    connection()->CloseConnection(
        QUIC_WINDOW_UPDATE_RECEIVED_ON_READ_UNIDIRECTIONAL_STREAM,
        "WindowUpdateFrame received on READ_UNIDIRECTIONAL stream.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (ShouldProcessFrameByPendingStream(WINDOW_UPDATE_FRAME, stream_id)) {
    PendingStreamOnWindowUpdateFrame(frame);
    return;
  }

  QuicStream* stream = GetOrCreateStream(stream_id);
  if (stream != nullptr) {
    stream->OnWindowUpdateFrame(frame);
  }
}

void QuicSession::OnBlockedFrame(const QuicBlockedFrame& frame) {
  // TODO(rjshade): Compare our flow control receive windows for specified
  //                streams: if we have a large window then maybe something
  //                had gone wrong with the flow control accounting.
  QUIC_DLOG(INFO) << ENDPOINT << "Received BLOCKED frame with stream id: "
                  << frame.stream_id << ", offset: " << frame.offset;
}

bool QuicSession::CheckStreamNotBusyLooping(QuicStream* stream,
                                            uint64_t previous_bytes_written,
                                            bool previous_fin_sent) {
  if (  // Stream should not be closed.
      !stream->write_side_closed() &&
      // Not connection flow control blocked.
      !flow_controller_.IsBlocked() &&
      // Detect lack of forward progress.
      previous_bytes_written == stream->stream_bytes_written() &&
      previous_fin_sent == stream->fin_sent()) {
    stream->set_busy_counter(stream->busy_counter() + 1);
    QUIC_DVLOG(1) << ENDPOINT << "Suspected busy loop on stream id "
                  << stream->id() << " stream_bytes_written "
                  << stream->stream_bytes_written() << " fin "
                  << stream->fin_sent() << " count " << stream->busy_counter();
    // Wait a few iterations before firing, the exact count is
    // arbitrary, more than a few to cover a few test-only false
    // positives.
    if (stream->busy_counter() > 20) {
      QUIC_LOG(ERROR) << ENDPOINT << "Detected busy loop on stream id "
                      << stream->id() << " stream_bytes_written "
                      << stream->stream_bytes_written() << " fin "
                      << stream->fin_sent();
      return false;
    }
  } else {
    stream->set_busy_counter(0);
  }
  return true;
}

bool QuicSession::CheckStreamWriteBlocked(QuicStream* stream) const {
  if (!stream->write_side_closed() && stream->HasBufferedData() &&
      !stream->IsFlowControlBlocked() &&
      !write_blocked_streams_->IsStreamBlocked(stream->id())) {
    QUIC_DLOG(ERROR) << ENDPOINT << "stream " << stream->id()
                     << " has buffered " << stream->BufferedDataBytes()
                     << " bytes, and is not flow control blocked, "
                        "but it is not in the write block list.";
    return false;
  }
  return true;
}

void QuicSession::OnCanWrite() {
  if (connection_->framer().is_processing_packet()) {
    // Do not write data in the middle of packet processing because rest
    // frames in the packet may change the data to write. For example, lost
    // data could be acknowledged. Also, connection is going to emit
    // OnCanWrite signal post packet processing.
    QUIC_BUG(session_write_mid_packet_processing)
        << ENDPOINT << "Try to write mid packet processing.";
    return;
  }
  if (!RetransmitLostData()) {
    // Cannot finish retransmitting lost data, connection is write blocked.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Cannot finish retransmitting lost data, connection is "
                     "write blocked.";
    return;
  }
  // We limit the number of writes to the number of pending streams. If more
  // streams become pending, WillingAndAbleToWrite will be true, which will
  // cause the connection to request resumption before yielding to other
  // connections.
  // If we are connection level flow control blocked, then only allow the
  // crypto and headers streams to try writing as all other streams will be
  // blocked.
  size_t num_writes = flow_controller_.IsBlocked()
                          ? write_blocked_streams_->NumBlockedSpecialStreams()
                          : write_blocked_streams_->NumBlockedStreams();
  if (num_writes == 0 && !control_frame_manager_.WillingToWrite() &&
      datagram_queue_.empty() &&
      (!QuicVersionUsesCryptoFrames(transport_version()) ||
       !GetCryptoStream()->HasBufferedCryptoFrames())) {
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(connection_);
  if (QuicVersionUsesCryptoFrames(transport_version())) {
    QuicCryptoStream* crypto_stream = GetMutableCryptoStream();
    if (crypto_stream->HasBufferedCryptoFrames()) {
      crypto_stream->WriteBufferedCryptoFrames();
    }
    if ((GetQuicReloadableFlag(
             quic_no_write_control_frame_upon_connection_close) &&
         !connection_->connected()) ||
        crypto_stream->HasBufferedCryptoFrames()) {
      if (!connection_->connected()) {
        QUIC_RELOADABLE_FLAG_COUNT(
            quic_no_write_control_frame_upon_connection_close);
      }
      // Cannot finish writing buffered crypto frames, connection is either
      // write blocked or closed.
      return;
    }
  }
  if (control_frame_manager_.WillingToWrite()) {
    control_frame_manager_.OnCanWrite();
  }
  if (version().UsesTls() && GetHandshakeState() != HANDSHAKE_CONFIRMED &&
      connection_->in_probe_time_out()) {
    QUIC_CODE_COUNT(quic_donot_pto_stream_data_before_handshake_confirmed);
    // Do not PTO stream data before handshake gets confirmed.
    return;
  }
  // TODO(b/147146815): this makes all datagrams go before stream data.  We
  // should have a better priority scheme for this.
  if (!datagram_queue_.empty()) {
    size_t written = datagram_queue_.SendDatagrams();
    QUIC_DVLOG(1) << ENDPOINT << "Sent " << written << " datagrams";
    if (!datagram_queue_.empty()) {
      return;
    }
  }
  std::vector<QuicStreamId> last_writing_stream_ids;
  for (size_t i = 0; i < num_writes; ++i) {
    if (!(write_blocked_streams_->HasWriteBlockedSpecialStream() ||
          write_blocked_streams_->HasWriteBlockedDataStreams())) {
      // Writing one stream removed another!? Something's broken.
      QUIC_BUG(quic_bug_10866_1)
          << "WriteBlockedStream is missing, num_writes: " << num_writes
          << ", finished_writes: " << i
          << ", connected: " << connection_->connected()
          << ", connection level flow control blocked: "
          << flow_controller_.IsBlocked();
      for (QuicStreamId id : last_writing_stream_ids) {
        QUIC_LOG(WARNING) << "last_writing_stream_id: " << id;
      }
      connection_->CloseConnection(QUIC_INTERNAL_ERROR,
                                   "WriteBlockedStream is missing",
                                   ConnectionCloseBehavior::SILENT_CLOSE);
      return;
    }
    if (!CanWriteStreamData()) {
      return;
    }
    currently_writing_stream_id_ = write_blocked_streams_->PopFront();
    last_writing_stream_ids.push_back(currently_writing_stream_id_);
    QUIC_DVLOG(1) << ENDPOINT << "Removing stream "
                  << currently_writing_stream_id_ << " from write-blocked list";
    QuicStream* stream = GetOrCreateStream(currently_writing_stream_id_);
    if (stream != nullptr && !stream->IsFlowControlBlocked()) {
      // If the stream can't write all bytes it'll re-add itself to the blocked
      // list.
      uint64_t previous_bytes_written = stream->stream_bytes_written();
      bool previous_fin_sent = stream->fin_sent();
      QUIC_DVLOG(1) << ENDPOINT << "stream " << stream->id()
                    << " bytes_written " << previous_bytes_written << " fin "
                    << previous_fin_sent;
      stream->OnCanWrite();
      QUICHE_DCHECK(CheckStreamWriteBlocked(stream));
      QUICHE_DCHECK(CheckStreamNotBusyLooping(stream, previous_bytes_written,
                                              previous_fin_sent));
    }
    currently_writing_stream_id_ = 0;
  }
}

bool QuicSession::WillingAndAbleToWrite() const {
  // Schedule a write when:
  // 1) control frame manager has pending or new control frames, or
  // 2) any stream has pending retransmissions, or
  // 3) If the crypto or headers streams are blocked, or
  // 4) connection is not flow control blocked and there are write blocked
  // streams.
  if (QuicVersionUsesCryptoFrames(transport_version())) {
    if (HasPendingHandshake()) {
      return true;
    }
    if (!IsEncryptionEstablished()) {
      return false;
    }
  }
  if (control_frame_manager_.WillingToWrite() ||
      !streams_with_pending_retransmission_.empty()) {
    return true;
  }
  if (flow_controller_.IsBlocked()) {
    if (VersionUsesHttp3(transport_version())) {
      return false;
    }
    // Crypto and headers streams are not blocked by connection level flow
    // control.
    return write_blocked_streams_->HasWriteBlockedSpecialStream();
  }
  return write_blocked_streams_->HasWriteBlockedSpecialStream() ||
         write_blocked_streams_->HasWriteBlockedDataStreams();
}

std::string QuicSession::GetStreamsInfoForLogging() const {
  std::string info = absl::StrCat(
      "num_active_streams: ", GetNumActiveStreams(),
      ", num_pending_streams: ", pending_streams_size(),
      ", num_outgoing_draining_streams: ", num_outgoing_draining_streams(),
      " ");
  // Log info for up to 5 streams.
  size_t i = 5;
  for (const auto& it : stream_map_) {
    if (it.second->is_static()) {
      continue;
    }
    // Calculate the stream creation delay.
    const QuicTime::Delta delay =
        connection_->clock()->ApproximateNow() - it.second->creation_time();
    absl::StrAppend(
        &info, "{", it.second->id(), ":", delay.ToDebuggingValue(), ";",
        it.second->stream_bytes_written(), ",", it.second->fin_sent(), ",",
        it.second->HasBufferedData(), ",", it.second->fin_buffered(), ";",
        it.second->stream_bytes_read(), ",", it.second->fin_received(), "}");
    --i;
    if (i == 0) {
      break;
    }
  }
  return info;
}

bool QuicSession::HasPendingHandshake() const {
  if (QuicVersionUsesCryptoFrames(transport_version())) {
    return GetCryptoStream()->HasPendingCryptoRetransmission() ||
           GetCryptoStream()->HasBufferedCryptoFrames();
  }
  return streams_with_pending_retransmission_.contains(
             QuicUtils::GetCryptoStreamId(transport_version())) ||
         write_blocked_streams_->IsStreamBlocked(
             QuicUtils::GetCryptoStreamId(transport_version()));
}

void QuicSession::ProcessUdpPacket(const QuicSocketAddress& self_address,
                                   const QuicSocketAddress& peer_address,
                                   const QuicReceivedPacket& packet) {
  QuicConnectionContextSwitcher cs(connection_->context());
  connection_->ProcessUdpPacket(self_address, peer_address, packet);
}

std::string QuicSession::on_closed_frame_string() const {
  std::stringstream ss;
  ss << on_closed_frame_;
  if (source_.has_value()) {
    ss << " " << ConnectionCloseSourceToString(*source_);
  }
  return ss.str();
}

QuicConsumedData QuicSession::WritevData(QuicStreamId id, size_t write_length,
                                         QuicStreamOffset offset,
                                         StreamSendingState state,
                                         TransmissionType type,
                                         EncryptionLevel level) {
  QUIC_BUG_IF(session writevdata when disconnected, !connection()->connected())
      << ENDPOINT << "Try to write stream data when connection is closed: "
      << on_closed_frame_string();
  if (!IsEncryptionEstablished() &&
      !QuicUtils::IsCryptoStreamId(transport_version(), id)) {
    // Do not let streams write without encryption. The calling stream will end
    // up write blocked until OnCanWrite is next called.
    if (was_zero_rtt_rejected_ && !OneRttKeysAvailable()) {
      QUICHE_DCHECK(version().UsesTls() &&
                    perspective() == Perspective::IS_CLIENT);
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Suppress the write while 0-RTT gets rejected and "
                         "1-RTT keys are not available. Version: "
                      << ParsedQuicVersionToString(version());
    } else if (version().UsesTls() || perspective() == Perspective::IS_SERVER) {
      QUIC_BUG(quic_bug_10866_2)
          << ENDPOINT << "Try to send data of stream " << id
          << " before encryption is established. Version: "
          << ParsedQuicVersionToString(version());
    } else {
      // In QUIC crypto, this could happen when the client sends full CHLO and
      // 0-RTT request, then receives an inchoate REJ and sends an inchoate
      // CHLO. The client then gets the ACK of the inchoate CHLO or the client
      // gets the full REJ and needs to verify the proof (before it sends the
      // full CHLO), such that there is no outstanding crypto data.
      // Retransmission alarm fires in TLP mode which tries to retransmit the
      // 0-RTT request (without encryption).
      QUIC_DLOG(INFO) << ENDPOINT << "Try to send data of stream " << id
                      << " before encryption is established.";
    }
    return QuicConsumedData(0, false);
  }

  SetTransmissionType(type);
  QuicConnection::ScopedEncryptionLevelContext context(connection(), level);

  QuicConsumedData data =
      connection_->SendStreamData(id, write_length, offset, state);
  if (type == NOT_RETRANSMISSION) {
    // This is new stream data.
    write_blocked_streams_->UpdateBytesForStream(id, data.bytes_consumed);
  }

  return data;
}

size_t QuicSession::SendCryptoData(EncryptionLevel level, size_t write_length,
                                   QuicStreamOffset offset,
                                   TransmissionType type) {
  QUICHE_DCHECK(QuicVersionUsesCryptoFrames(transport_version()));
  if (!connection()->framer().HasEncrypterOfEncryptionLevel(level)) {
    const std::string error_details = absl::StrCat(
        "Try to send crypto data with missing keys of encryption level: ",
        EncryptionLevelToString(level));
    QUIC_BUG(quic_bug_10866_3) << ENDPOINT << error_details;
    connection()->CloseConnection(
        QUIC_MISSING_WRITE_KEYS, error_details,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return 0;
  }
  SetTransmissionType(type);
  QuicConnection::ScopedEncryptionLevelContext context(connection(), level);
  const auto bytes_consumed =
      connection_->SendCryptoData(level, write_length, offset);
  return bytes_consumed;
}

void QuicSession::OnControlFrameManagerError(QuicErrorCode error_code,
                                             std::string error_details) {
  connection_->CloseConnection(
      error_code, error_details,
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

bool QuicSession::WriteControlFrame(const QuicFrame& frame,
                                    TransmissionType type) {
  QUIC_BUG_IF(quic_bug_12435_11, !connection()->connected())
      << ENDPOINT
      << absl::StrCat("Try to write control frame: ", QuicFrameToString(frame),
                      " when connection is closed: ")
      << on_closed_frame_string();
  if (!IsEncryptionEstablished()) {
    // Suppress the write before encryption gets established.
    return false;
  }
  SetTransmissionType(type);
  QuicConnection::ScopedEncryptionLevelContext context(
      connection(), GetEncryptionLevelToSendApplicationData());
  return connection_->SendControlFrame(frame);
}

void QuicSession::ResetStream(QuicStreamId id, QuicRstStreamErrorCode error) {
  QuicStream* stream = GetStream(id);
  if (stream != nullptr && stream->is_static()) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Try to reset a static stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (stream != nullptr) {
    stream->Reset(error);
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(connection());
  MaybeSendStopSendingFrame(id, QuicResetStreamError::FromInternal(error));
  MaybeSendRstStreamFrame(id, QuicResetStreamError::FromInternal(error), 0);
}

void QuicSession::MaybeSendRstStreamFrame(QuicStreamId id,
                                          QuicResetStreamError error,
                                          QuicStreamOffset bytes_written) {
  if (!connection()->connected()) {
    return;
  }
  if (!VersionHasIetfQuicFrames(transport_version()) ||
      QuicUtils::GetStreamType(id, perspective(), IsIncomingStream(id),
                               version()) != READ_UNIDIRECTIONAL) {
    control_frame_manager_.WriteOrBufferRstStream(id, error, bytes_written);
  }

  connection_->OnStreamReset(id, error.internal_code());
}

void QuicSession::MaybeSendStopSendingFrame(QuicStreamId id,
                                            QuicResetStreamError error) {
  if (!connection()->connected()) {
    return;
  }
  if (VersionHasIetfQuicFrames(transport_version()) &&
      QuicUtils::GetStreamType(id, perspective(), IsIncomingStream(id),
                               version()) != WRITE_UNIDIRECTIONAL) {
    control_frame_manager_.WriteOrBufferStopSending(error, id);
  }
}

void QuicSession::SendGoAway(QuicErrorCode error_code,
                             const std::string& reason) {
  // GOAWAY frame is not supported in IETF QUIC.
  QUICHE_DCHECK(!VersionHasIetfQuicFrames(transport_version()));
  if (!IsEncryptionEstablished()) {
    QUIC_CODE_COUNT(quic_goaway_before_encryption_established);
    connection_->CloseConnection(
        error_code, reason,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (transport_goaway_sent_) {
    return;
  }
  transport_goaway_sent_ = true;

  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);
  control_frame_manager_.WriteOrBufferGoAway(
      error_code,
      QuicUtils::GetMaxClientInitiatedBidirectionalStreamId(
          transport_version()),
      reason);
}

void QuicSession::SendBlocked(QuicStreamId id, QuicStreamOffset byte_offset) {
  control_frame_manager_.WriteOrBufferBlocked(id, byte_offset);
}

void QuicSession::SendWindowUpdate(QuicStreamId id,
                                   QuicStreamOffset byte_offset) {
  control_frame_manager_.WriteOrBufferWindowUpdate(id, byte_offset);
}

void QuicSession::OnStreamError(QuicErrorCode error_code,
                                std::string error_details) {
  connection_->CloseConnection(
      error_code, error_details,
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSession::OnStreamError(QuicErrorCode error_code,
                                QuicIetfTransportErrorCodes ietf_error,
                                std::string error_details) {
  connection_->CloseConnection(
      error_code, ietf_error, error_details,
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

bool QuicSession::CanSendMaxStreams() {
  return control_frame_manager_.NumBufferedMaxStreams() < 2;
}

void QuicSession::SendMaxStreams(QuicStreamCount stream_count,
                                 bool unidirectional) {
  if (!is_configured_) {
    QUIC_BUG(quic_bug_10866_5)
        << "Try to send max streams before config negotiated.";
    return;
  }
  control_frame_manager_.WriteOrBufferMaxStreams(stream_count, unidirectional);
}

void QuicSession::InsertLocallyClosedStreamsHighestOffset(
    const QuicStreamId id, QuicStreamOffset offset) {
  locally_closed_streams_highest_offset_[id] = offset;
}

void QuicSession::OnStreamClosed(QuicStreamId stream_id) {
  QUIC_DVLOG(1) << ENDPOINT << "Closing stream: " << stream_id;
  StreamMap::iterator it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    QUIC_BUG(quic_bug_10866_6)
        << ENDPOINT << "Stream is already closed: " << stream_id;
    return;
  }
  QuicStream* stream = it->second.get();
  StreamType type = stream->type();

  const bool stream_waiting_for_acks = stream->IsWaitingForAcks();
  if (stream_waiting_for_acks) {
    // The stream needs to be kept alive because it's waiting for acks.
    ++num_zombie_streams_;
  } else {
    closed_streams_.push_back(std::move(it->second));
    stream_map_.erase(it);
    // Do not retransmit data of a closed stream.
    streams_with_pending_retransmission_.erase(stream_id);
    if (!closed_streams_clean_up_alarm_->IsSet()) {
      closed_streams_clean_up_alarm_->Set(
          connection_->clock()->ApproximateNow());
    }
    connection_->QuicBugIfHasPendingFrames(stream_id);
  }

  if (!stream->HasReceivedFinalOffset()) {
    // If we haven't received a FIN or RST for this stream, we need to keep
    // track of the how many bytes the stream's flow controller believes it has
    // received, for accurate connection level flow control accounting.
    // If this is an outgoing stream, it is technically open from peer's
    // perspective. Do not inform stream Id manager yet.
    QUICHE_DCHECK(!stream->was_draining());
    InsertLocallyClosedStreamsHighestOffset(
        stream_id, stream->highest_received_byte_offset());
    return;
  }

  const bool stream_was_draining = stream->was_draining();
  QUIC_DVLOG_IF(1, stream_was_draining)
      << ENDPOINT << "Stream " << stream_id << " was draining";
  if (stream_was_draining) {
    QUIC_BUG_IF(quic_bug_12435_4, num_draining_streams_ == 0);
    --num_draining_streams_;
    if (!IsIncomingStream(stream_id)) {
      QUIC_BUG_IF(quic_bug_12435_5, num_outgoing_draining_streams_ == 0);
      --num_outgoing_draining_streams_;
    }
    // Stream Id manager has been informed with draining streams.
    return;
  }
  if (!VersionHasIetfQuicFrames(transport_version())) {
    stream_id_manager_.OnStreamClosed(
        /*is_incoming=*/IsIncomingStream(stream_id));
  }
  if (!connection_->connected()) {
    return;
  }
  if (IsIncomingStream(stream_id)) {
    // Stream Id manager is only interested in peer initiated stream IDs.
    if (VersionHasIetfQuicFrames(transport_version())) {
      ietf_streamid_manager_.OnStreamClosed(stream_id);
    }
    return;
  }
  if (!VersionHasIetfQuicFrames(transport_version())) {
    OnCanCreateNewOutgoingStream(type != BIDIRECTIONAL);
  }
}

void QuicSession::ClosePendingStream(QuicStreamId stream_id) {
  QUIC_DVLOG(1) << ENDPOINT << "Closing stream " << stream_id;
  QUICHE_DCHECK(VersionHasIetfQuicFrames(transport_version()));
  pending_stream_map_.erase(stream_id);
  if (connection_->connected()) {
    ietf_streamid_manager_.OnStreamClosed(stream_id);
  }
}

bool QuicSession::ShouldProcessFrameByPendingStream(QuicFrameType type,
                                                    QuicStreamId id) const {
  return stream_map_.find(id) == stream_map_.end() &&
         ((version().HasIetfQuicFrames() && ExceedsPerLoopStreamLimit()) ||
          UsesPendingStreamForFrame(type, id));
}

void QuicSession::OnFinalByteOffsetReceived(
    QuicStreamId stream_id, QuicStreamOffset final_byte_offset) {
  auto it = locally_closed_streams_highest_offset_.find(stream_id);
  if (it == locally_closed_streams_highest_offset_.end()) {
    return;
  }

  QUIC_DVLOG(1) << ENDPOINT << "Received final byte offset "
                << final_byte_offset << " for stream " << stream_id;
  QuicByteCount offset_diff = final_byte_offset - it->second;
  if (flow_controller_.UpdateHighestReceivedOffset(
          flow_controller_.highest_received_byte_offset() + offset_diff)) {
    // If the final offset violates flow control, close the connection now.
    if (flow_controller_.FlowControlViolation()) {
      connection_->CloseConnection(
          QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA,
          "Connection level flow control violation",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
  }

  flow_controller_.AddBytesConsumed(offset_diff);
  locally_closed_streams_highest_offset_.erase(it);
  if (!VersionHasIetfQuicFrames(transport_version())) {
    stream_id_manager_.OnStreamClosed(
        /*is_incoming=*/IsIncomingStream(stream_id));
  }
  if (IsIncomingStream(stream_id)) {
    if (VersionHasIetfQuicFrames(transport_version())) {
      ietf_streamid_manager_.OnStreamClosed(stream_id);
    }
  } else if (!VersionHasIetfQuicFrames(transport_version())) {
    OnCanCreateNewOutgoingStream(false);
  }
}

bool QuicSession::IsEncryptionEstablished() const {
  if (GetCryptoStream() == nullptr) {
    return false;
  }
  return GetCryptoStream()->encryption_established();
}

bool QuicSession::OneRttKeysAvailable() const {
  if (GetCryptoStream() == nullptr) {
    return false;
  }
  return GetCryptoStream()->one_rtt_keys_available();
}

void QuicSession::OnConfigNegotiated() {
  // In versions with TLS, the configs will be set twice if 0-RTT is available.
  // In the second config setting, 1-RTT keys are guaranteed to be available.
  if (version().UsesTls() && is_configured_ &&
      connection_->encryption_level() != ENCRYPTION_FORWARD_SECURE) {
    QUIC_BUG(quic_bug_12435_6)
        << ENDPOINT
        << "1-RTT keys missing when config is negotiated for the second time.";
    connection_->CloseConnection(
        QUIC_INTERNAL_ERROR,
        "1-RTT keys missing when config is negotiated for the second time.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  QUIC_DVLOG(1) << ENDPOINT << "OnConfigNegotiated";
  connection_->SetFromConfig(config_);

  if (VersionHasIetfQuicFrames(transport_version())) {
    uint32_t max_streams = 0;
    if (config_.HasReceivedMaxBidirectionalStreams()) {
      max_streams = config_.ReceivedMaxBidirectionalStreams();
    }
    if (was_zero_rtt_rejected_ &&
        max_streams <
            ietf_streamid_manager_.outgoing_bidirectional_stream_count()) {
      connection_->CloseConnection(
          QUIC_ZERO_RTT_UNRETRANSMITTABLE,
          absl::StrCat(
              "Server rejected 0-RTT, aborting because new bidirectional "
              "initial stream limit ",
              max_streams, " is less than current open streams: ",
              ietf_streamid_manager_.outgoing_bidirectional_stream_count()),
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
    QUIC_DVLOG(1) << ENDPOINT
                  << "Setting Bidirectional outgoing_max_streams_ to "
                  << max_streams;
    if (perspective_ == Perspective::IS_CLIENT &&
        max_streams <
            ietf_streamid_manager_.max_outgoing_bidirectional_streams()) {
      connection_->CloseConnection(
          was_zero_rtt_rejected_ ? QUIC_ZERO_RTT_REJECTION_LIMIT_REDUCED
                                 : QUIC_ZERO_RTT_RESUMPTION_LIMIT_REDUCED,
          absl::StrCat(
              was_zero_rtt_rejected_
                  ? "Server rejected 0-RTT, aborting because "
                  : "",
              "new bidirectional limit ", max_streams,
              " decreases the current limit: ",
              ietf_streamid_manager_.max_outgoing_bidirectional_streams()),
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
    if (ietf_streamid_manager_.MaybeAllowNewOutgoingBidirectionalStreams(
            max_streams)) {
      OnCanCreateNewOutgoingStream(/*unidirectional = */ false);
    }

    max_streams = 0;
    if (config_.HasReceivedMaxUnidirectionalStreams()) {
      max_streams = config_.ReceivedMaxUnidirectionalStreams();
    }

    if (was_zero_rtt_rejected_ &&
        max_streams <
            ietf_streamid_manager_.outgoing_unidirectional_stream_count()) {
      connection_->CloseConnection(
          QUIC_ZERO_RTT_UNRETRANSMITTABLE,
          absl::StrCat(
              "Server rejected 0-RTT, aborting because new unidirectional "
              "initial stream limit ",
              max_streams, " is less than current open streams: ",
              ietf_streamid_manager_.outgoing_unidirectional_stream_count()),
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }

    if (max_streams <
        ietf_streamid_manager_.max_outgoing_unidirectional_streams()) {
      connection_->CloseConnection(
          was_zero_rtt_rejected_ ? QUIC_ZERO_RTT_REJECTION_LIMIT_REDUCED
                                 : QUIC_ZERO_RTT_RESUMPTION_LIMIT_REDUCED,
          absl::StrCat(
              was_zero_rtt_rejected_
                  ? "Server rejected 0-RTT, aborting because "
                  : "",
              "new unidirectional limit ", max_streams,
              " decreases the current limit: ",
              ietf_streamid_manager_.max_outgoing_unidirectional_streams()),
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
    QUIC_DVLOG(1) << ENDPOINT
                  << "Setting Unidirectional outgoing_max_streams_ to "
                  << max_streams;
    if (ietf_streamid_manager_.MaybeAllowNewOutgoingUnidirectionalStreams(
            max_streams)) {
      OnCanCreateNewOutgoingStream(/*unidirectional = */ true);
    }
  } else {
    uint32_t max_streams = 0;
    if (config_.HasReceivedMaxBidirectionalStreams()) {
      max_streams = config_.ReceivedMaxBidirectionalStreams();
    }
    QUIC_DVLOG(1) << ENDPOINT << "Setting max_open_outgoing_streams_ to "
                  << max_streams;
    if (was_zero_rtt_rejected_ &&
        max_streams < stream_id_manager_.num_open_outgoing_streams()) {
      connection_->CloseConnection(
          QUIC_INTERNAL_ERROR,
          absl::StrCat(
              "Server rejected 0-RTT, aborting because new stream limit ",
              max_streams, " is less than current open streams: ",
              stream_id_manager_.num_open_outgoing_streams()),
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
    stream_id_manager_.set_max_open_outgoing_streams(max_streams);
  }

  if (perspective() == Perspective::IS_SERVER) {
    if (config_.HasReceivedConnectionOptions()) {
      // The following variations change the initial receive flow control
      // window sizes.
      if (ContainsQuicTag(config_.ReceivedConnectionOptions(), kIFW6)) {
        AdjustInitialFlowControlWindows(64 * 1024);
      }
      if (ContainsQuicTag(config_.ReceivedConnectionOptions(), kIFW7)) {
        AdjustInitialFlowControlWindows(128 * 1024);
      }
      if (ContainsQuicTag(config_.ReceivedConnectionOptions(), kIFW8)) {
        AdjustInitialFlowControlWindows(256 * 1024);
      }
      if (ContainsQuicTag(config_.ReceivedConnectionOptions(), kIFW9)) {
        AdjustInitialFlowControlWindows(512 * 1024);
      }
      if (ContainsQuicTag(config_.ReceivedConnectionOptions(), kIFWA)) {
        AdjustInitialFlowControlWindows(1024 * 1024);
      }
    }

    config_.SetStatelessResetTokenToSend(GetStatelessResetToken());
  }

  if (VersionHasIetfQuicFrames(transport_version())) {
    ietf_streamid_manager_.SetMaxOpenIncomingBidirectionalStreams(
        config_.GetMaxBidirectionalStreamsToSend());
    ietf_streamid_manager_.SetMaxOpenIncomingUnidirectionalStreams(
        config_.GetMaxUnidirectionalStreamsToSend());
  } else {
    // A small number of additional incoming streams beyond the limit should be
    // allowed. This helps avoid early connection termination when FIN/RSTs for
    // old streams are lost or arrive out of order.
    // Use a minimum number of additional streams, or a percentage increase,
    // whichever is larger.
    uint32_t max_incoming_streams_to_send =
        config_.GetMaxBidirectionalStreamsToSend();
    uint32_t max_incoming_streams =
        std::max(max_incoming_streams_to_send + kMaxStreamsMinimumIncrement,
                 static_cast<uint32_t>(max_incoming_streams_to_send *
                                       kMaxStreamsMultiplier));
    stream_id_manager_.set_max_open_incoming_streams(max_incoming_streams);
  }

  if (connection_->version().handshake_protocol == PROTOCOL_TLS1_3) {
    // When using IETF-style TLS transport parameters, inform existing streams
    // of new flow-control limits.
    if (config_.HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional()) {
      OnNewStreamOutgoingBidirectionalFlowControlWindow(
          config_.ReceivedInitialMaxStreamDataBytesOutgoingBidirectional());
    }
    if (config_.HasReceivedInitialMaxStreamDataBytesIncomingBidirectional()) {
      OnNewStreamIncomingBidirectionalFlowControlWindow(
          config_.ReceivedInitialMaxStreamDataBytesIncomingBidirectional());
    }
    if (config_.HasReceivedInitialMaxStreamDataBytesUnidirectional()) {
      OnNewStreamUnidirectionalFlowControlWindow(
          config_.ReceivedInitialMaxStreamDataBytesUnidirectional());
    }
  } else {  // The version uses Google QUIC Crypto.
    if (config_.HasReceivedInitialStreamFlowControlWindowBytes()) {
      // Streams which were created before the SHLO was received (0-RTT
      // requests) are now informed of the peer's initial flow control window.
      OnNewStreamFlowControlWindow(
          config_.ReceivedInitialStreamFlowControlWindowBytes());
    }
  }

  if (config_.HasReceivedInitialSessionFlowControlWindowBytes()) {
    OnNewSessionFlowControlWindow(
        config_.ReceivedInitialSessionFlowControlWindowBytes());
  }

  if (perspective_ == Perspective::IS_SERVER && version().HasIetfQuicFrames() &&
      connection_->effective_peer_address().IsInitialized()) {
    if (config_.SupportsServerPreferredAddress(perspective_)) {
      quiche::IpAddressFamily address_family =
          connection_->effective_peer_address()
              .Normalized()
              .host()
              .address_family();
      std::optional<QuicSocketAddress> expected_preferred_address =
          config_.GetMappedAlternativeServerAddress(address_family);
      if (expected_preferred_address.has_value()) {
        // Set connection ID and token if SPAD has received and a preferred
        // address of the same address family is configured.
        std::optional<QuicNewConnectionIdFrame> frame =
            connection_->MaybeIssueNewConnectionIdForPreferredAddress();
        if (frame.has_value()) {
          config_.SetPreferredAddressConnectionIdAndTokenToSend(
              frame->connection_id, frame->stateless_reset_token);
        }
        connection_->set_expected_server_preferred_address(
            *expected_preferred_address);
      }
      // Clear the alternative address of the other address family in the
      // config.
      config_.ClearAlternateServerAddressToSend(
          address_family == quiche::IpAddressFamily::IP_V4
              ? quiche::IpAddressFamily::IP_V6
              : quiche::IpAddressFamily::IP_V4);
    } else {
      // Clear alternative IPv(4|6) addresses in config if the server hasn't
      // received 'SPAD' connection option.
      config_.ClearAlternateServerAddressToSend(quiche::IpAddressFamily::IP_V4);
      config_.ClearAlternateServerAddressToSend(quiche::IpAddressFamily::IP_V6);
    }
  }

  is_configured_ = true;
  connection()->OnConfigNegotiated();

  // Ask flow controllers to try again since the config could have unblocked us.
  // Or if this session is configured on TLS enabled QUIC versions,
  // attempt to retransmit 0-RTT data if there's any.
  // TODO(fayang): consider removing this OnCanWrite call.
  if (!connection_->framer().is_processing_packet() &&
      (connection_->version().AllowsLowFlowControlLimits() ||
       version().UsesTls())) {
    QUIC_CODE_COUNT(quic_session_on_can_write_on_config_negotiated);
    OnCanWrite();
  }
}

std::optional<std::string> QuicSession::OnAlpsData(const uint8_t* /*alps_data*/,
                                                   size_t /*alps_length*/) {
  return std::nullopt;
}

void QuicSession::AdjustInitialFlowControlWindows(size_t stream_window) {
  const float session_window_multiplier =
      config_.GetInitialStreamFlowControlWindowToSend()
          ? static_cast<float>(
                config_.GetInitialSessionFlowControlWindowToSend()) /
                config_.GetInitialStreamFlowControlWindowToSend()
          : 1.5;

  QUIC_DVLOG(1) << ENDPOINT << "Set stream receive window to " << stream_window;
  config_.SetInitialStreamFlowControlWindowToSend(stream_window);

  size_t session_window = session_window_multiplier * stream_window;
  QUIC_DVLOG(1) << ENDPOINT << "Set session receive window to "
                << session_window;
  config_.SetInitialSessionFlowControlWindowToSend(session_window);
  flow_controller_.UpdateReceiveWindowSize(session_window);
  // Inform all existing streams about the new window.
  for (auto const& kv : stream_map_) {
    kv.second->UpdateReceiveWindowSize(stream_window);
  }
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    GetMutableCryptoStream()->UpdateReceiveWindowSize(stream_window);
  }
}

void QuicSession::HandleFrameOnNonexistentOutgoingStream(
    QuicStreamId stream_id) {
  QUICHE_DCHECK(!IsClosedStream(stream_id));
  // Received a frame for a locally-created stream that is not currently
  // active. This is an error.
  if (VersionHasIetfQuicFrames(transport_version())) {
    connection()->CloseConnection(
        QUIC_HTTP_STREAM_WRONG_DIRECTION, "Data for nonexistent stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Data for nonexistent stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSession::HandleRstOnValidNonexistentStream(
    const QuicRstStreamFrame& frame) {
  // If the stream is neither originally in active streams nor created in
  // GetOrCreateStream(), it could be a closed stream in which case its
  // final received byte offset need to be updated.
  if (IsClosedStream(frame.stream_id)) {
    // The RST frame contains the final byte offset for the stream: we can now
    // update the connection level flow controller if needed.
    OnFinalByteOffsetReceived(frame.stream_id, frame.byte_offset);
  }
}

void QuicSession::OnNewStreamFlowControlWindow(QuicStreamOffset new_window) {
  QUICHE_DCHECK(version().UsesQuicCrypto());
  QUIC_DVLOG(1) << ENDPOINT << "OnNewStreamFlowControlWindow " << new_window;
  if (new_window < kMinimumFlowControlSendWindow) {
    QUIC_LOG_FIRST_N(ERROR, 1)
        << "Peer sent us an invalid stream flow control send window: "
        << new_window << ", below minimum: " << kMinimumFlowControlSendWindow;
    connection_->CloseConnection(
        QUIC_FLOW_CONTROL_INVALID_WINDOW, "New stream window too low",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  // Inform all existing streams about the new window.
  for (auto const& kv : stream_map_) {
    QUIC_DVLOG(1) << ENDPOINT << "Informing stream " << kv.first
                  << " of new stream flow control window " << new_window;
    if (!kv.second->MaybeConfigSendWindowOffset(
            new_window, /* was_zero_rtt_rejected = */ false)) {
      return;
    }
  }
  if (!QuicVersionUsesCryptoFrames(transport_version())) {
    QUIC_DVLOG(1)
        << ENDPOINT
        << "Informing crypto stream of new stream flow control window "
        << new_window;
    GetMutableCryptoStream()->MaybeConfigSendWindowOffset(
        new_window, /* was_zero_rtt_rejected = */ false);
  }
}

void QuicSession::OnNewStreamUnidirectionalFlowControlWindow(
    QuicStreamOffset new_window) {
  QUICHE_DCHECK_EQ(connection_->version().handshake_protocol, PROTOCOL_TLS1_3);
  QUIC_DVLOG(1) << ENDPOINT << "OnNewStreamUnidirectionalFlowControlWindow "
                << new_window;
  // Inform all existing outgoing unidirectional streams about the new window.
  for (auto const& kv : stream_map_) {
    const QuicStreamId id = kv.first;
    if (!version().HasIetfQuicFrames()) {
      if (kv.second->type() == BIDIRECTIONAL) {
        continue;
      }
    } else {
      if (QuicUtils::IsBidirectionalStreamId(id, version())) {
        continue;
      }
    }
    if (!QuicUtils::IsOutgoingStreamId(connection_->version(), id,
                                       perspective())) {
      continue;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Informing unidirectional stream " << id
                  << " of new stream flow control window " << new_window;
    if (!kv.second->MaybeConfigSendWindowOffset(new_window,
                                                was_zero_rtt_rejected_)) {
      return;
    }
  }
}

void QuicSession::OnNewStreamOutgoingBidirectionalFlowControlWindow(
    QuicStreamOffset new_window) {
  QUICHE_DCHECK_EQ(connection_->version().handshake_protocol, PROTOCOL_TLS1_3);
  QUIC_DVLOG(1) << ENDPOINT
                << "OnNewStreamOutgoingBidirectionalFlowControlWindow "
                << new_window;
  // Inform all existing outgoing bidirectional streams about the new window.
  for (auto const& kv : stream_map_) {
    const QuicStreamId id = kv.first;
    if (!version().HasIetfQuicFrames()) {
      if (kv.second->type() != BIDIRECTIONAL) {
        continue;
      }
    } else {
      if (!QuicUtils::IsBidirectionalStreamId(id, version())) {
        continue;
      }
    }
    if (!QuicUtils::IsOutgoingStreamId(connection_->version(), id,
                                       perspective())) {
      continue;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Informing outgoing bidirectional stream "
                  << id << " of new stream flow control window " << new_window;
    if (!kv.second->MaybeConfigSendWindowOffset(new_window,
                                                was_zero_rtt_rejected_)) {
      return;
    }
  }
}

void QuicSession::OnNewStreamIncomingBidirectionalFlowControlWindow(
    QuicStreamOffset new_window) {
  QUICHE_DCHECK_EQ(connection_->version().handshake_protocol, PROTOCOL_TLS1_3);
  QUIC_DVLOG(1) << ENDPOINT
                << "OnNewStreamIncomingBidirectionalFlowControlWindow "
                << new_window;
  // Inform all existing incoming bidirectional streams about the new window.
  for (auto const& kv : stream_map_) {
    const QuicStreamId id = kv.first;
    if (!version().HasIetfQuicFrames()) {
      if (kv.second->type() != BIDIRECTIONAL) {
        continue;
      }
    } else {
      if (!QuicUtils::IsBidirectionalStreamId(id, version())) {
        continue;
      }
    }
    if (QuicUtils::IsOutgoingStreamId(connection_->version(), id,
                                      perspective())) {
      continue;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Informing incoming bidirectional stream "
                  << id << " of new stream flow control window " << new_window;
    if (!kv.second->MaybeConfigSendWindowOffset(new_window,
                                                was_zero_rtt_rejected_)) {
      return;
    }
  }
}

void QuicSession::OnNewSessionFlowControlWindow(QuicStreamOffset new_window) {
  QUIC_DVLOG(1) << ENDPOINT << "OnNewSessionFlowControlWindow " << new_window;

  if (was_zero_rtt_rejected_ && new_window < flow_controller_.bytes_sent()) {
    std::string error_details = absl::StrCat(
        "Server rejected 0-RTT. Aborting because the client received session "
        "flow control send window: ",
        new_window,
        ", which is below currently used: ", flow_controller_.bytes_sent());
    QUIC_LOG(ERROR) << error_details;
    connection_->CloseConnection(
        QUIC_ZERO_RTT_UNRETRANSMITTABLE, error_details,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (!connection_->version().AllowsLowFlowControlLimits() &&
      new_window < kMinimumFlowControlSendWindow) {
    std::string error_details = absl::StrCat(
        "Peer sent us an invalid session flow control send window: ",
        new_window, ", below minimum: ", kMinimumFlowControlSendWindow);
    QUIC_LOG_FIRST_N(ERROR, 1) << error_details;
    connection_->CloseConnection(
        QUIC_FLOW_CONTROL_INVALID_WINDOW, error_details,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (perspective_ == Perspective::IS_CLIENT &&
      new_window < flow_controller_.send_window_offset()) {
    // The client receives a lower limit than remembered, violating
    // https://tools.ietf.org/html/draft-ietf-quic-transport-27#section-7.3.1
    std::string error_details = absl::StrCat(
        was_zero_rtt_rejected_ ? "Server rejected 0-RTT, aborting because "
                               : "",
        "new session max data ", new_window,
        " decreases current limit: ", flow_controller_.send_window_offset());
    QUIC_LOG(ERROR) << error_details;
    connection_->CloseConnection(
        was_zero_rtt_rejected_ ? QUIC_ZERO_RTT_REJECTION_LIMIT_REDUCED
                               : QUIC_ZERO_RTT_RESUMPTION_LIMIT_REDUCED,
        error_details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  flow_controller_.UpdateSendWindowOffset(new_window);
}

bool QuicSession::OnNewDecryptionKeyAvailable(
    EncryptionLevel level, std::unique_ptr<QuicDecrypter> decrypter,
    bool set_alternative_decrypter, bool latch_once_used) {
  if (connection_->version().handshake_protocol == PROTOCOL_TLS1_3 &&
      !connection()->framer().HasEncrypterOfEncryptionLevel(
          QuicUtils::GetEncryptionLevelToSendAckofSpace(
              QuicUtils::GetPacketNumberSpace(level)))) {
    // This should never happen because connection should never decrypt a packet
    // while an ACK for it cannot be encrypted.
    return false;
  }
  if (connection()->version().KnowsWhichDecrypterToUse()) {
    connection()->InstallDecrypter(level, std::move(decrypter));
    return true;
  }
  if (set_alternative_decrypter) {
    connection()->SetAlternativeDecrypter(level, std::move(decrypter),
                                          latch_once_used);
    return true;
  }
  connection()->SetDecrypter(level, std::move(decrypter));
  return true;
}

void QuicSession::OnNewEncryptionKeyAvailable(
    EncryptionLevel level, std::unique_ptr<QuicEncrypter> encrypter) {
  connection()->SetEncrypter(level, std::move(encrypter));
  if (connection_->version().handshake_protocol != PROTOCOL_TLS1_3) {
    return;
  }

  bool reset_encryption_level = false;
  if (IsEncryptionEstablished() && level == ENCRYPTION_HANDSHAKE) {
    // ENCRYPTION_HANDSHAKE keys are only used for the handshake. If
    // ENCRYPTION_ZERO_RTT keys exist, it is possible for a client to send
    // stream data, which must not be sent at the ENCRYPTION_HANDSHAKE level.
    // Therefore, we avoid setting the default encryption level to
    // ENCRYPTION_HANDSHAKE.
    reset_encryption_level = true;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Set default encryption level to " << level;
  connection()->SetDefaultEncryptionLevel(level);
  if (reset_encryption_level) {
    connection()->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  }
  QUIC_BUG_IF(quic_bug_12435_7,
              IsEncryptionEstablished() &&
                  (connection()->encryption_level() == ENCRYPTION_INITIAL ||
                   connection()->encryption_level() == ENCRYPTION_HANDSHAKE))
      << "Encryption is established, but the encryption level " << level
      << " does not support sending stream data";
}

void QuicSession::SetDefaultEncryptionLevel(EncryptionLevel level) {
  QUICHE_DCHECK_EQ(PROTOCOL_QUIC_CRYPTO,
                   connection_->version().handshake_protocol);
  QUIC_DVLOG(1) << ENDPOINT << "Set default encryption level to " << level;
  connection()->SetDefaultEncryptionLevel(level);

  switch (level) {
    case ENCRYPTION_INITIAL:
      break;
    case ENCRYPTION_ZERO_RTT:
      if (perspective() == Perspective::IS_CLIENT) {
        // Retransmit old 0-RTT data (if any) with the new 0-RTT keys, since
        // they can't be decrypted by the server.
        connection_->MarkZeroRttPacketsForRetransmission(0);
        if (!connection_->framer().is_processing_packet()) {
          // TODO(fayang): consider removing this OnCanWrite call.
          // Given any streams blocked by encryption a chance to write.
          QUIC_CODE_COUNT(
              quic_session_on_can_write_set_default_encryption_level);
          OnCanWrite();
        }
      }
      break;
    case ENCRYPTION_HANDSHAKE:
      break;
    case ENCRYPTION_FORWARD_SECURE:
      QUIC_BUG_IF(quic_bug_12435_8, !config_.negotiated())
          << ENDPOINT << "Handshake confirmed without parameter negotiation.";
      connection()->mutable_stats().handshake_completion_time =
          connection()->clock()->ApproximateNow();
      break;
    default:
      QUIC_BUG(quic_bug_10866_7) << "Unknown encryption level: " << level;
  }
}

void QuicSession::OnTlsHandshakeComplete() {
  QUICHE_DCHECK_EQ(PROTOCOL_TLS1_3, connection_->version().handshake_protocol);
  QUIC_BUG_IF(quic_bug_12435_9,
              !GetCryptoStream()->crypto_negotiated_params().cipher_suite)
      << ENDPOINT << "Handshake completes without cipher suite negotiation.";
  QUIC_BUG_IF(quic_bug_12435_10, !config_.negotiated())
      << ENDPOINT << "Handshake completes without parameter negotiation.";
  connection()->mutable_stats().handshake_completion_time =
      connection()->clock()->ApproximateNow();
  if (connection()->version().UsesTls() &&
      perspective_ == Perspective::IS_SERVER) {
    // Server sends HANDSHAKE_DONE to signal confirmation of the handshake
    // to the client.
    control_frame_manager_.WriteOrBufferHandshakeDone();
    if (connection()->version().HasIetfQuicFrames()) {
      MaybeSendAddressToken();
    }
  }
}

bool QuicSession::MaybeSendAddressToken() {
  QUICHE_DCHECK(perspective_ == Perspective::IS_SERVER &&
                connection()->version().HasIetfQuicFrames());
  std::optional<CachedNetworkParameters> cached_network_params =
      GenerateCachedNetworkParameters();

  std::string address_token = GetCryptoStream()->GetAddressToken(
      cached_network_params.has_value() ? &*cached_network_params : nullptr);
  if (address_token.empty()) {
    return false;
  }
  const size_t buf_len = address_token.length() + 1;
  auto buffer = std::make_unique<char[]>(buf_len);
  QuicDataWriter writer(buf_len, buffer.get());
  // Add |kAddressTokenPrefix| for token sent in NEW_TOKEN frame.
  writer.WriteUInt8(kAddressTokenPrefix);
  writer.WriteBytes(address_token.data(), address_token.length());
  control_frame_manager_.WriteOrBufferNewToken(
      absl::string_view(buffer.get(), buf_len));
  if (cached_network_params.has_value()) {
    connection()->OnSendConnectionState(*cached_network_params);
  }
  return true;
}

void QuicSession::DiscardOldDecryptionKey(EncryptionLevel level) {
  if (!connection()->version().KnowsWhichDecrypterToUse()) {
    return;
  }
  connection()->RemoveDecrypter(level);
}

void QuicSession::DiscardOldEncryptionKey(EncryptionLevel level) {
  QUIC_DLOG(INFO) << ENDPOINT << "Discarding " << level << " keys";
  if (connection()->version().handshake_protocol == PROTOCOL_TLS1_3) {
    connection()->RemoveEncrypter(level);
  }
  switch (level) {
    case ENCRYPTION_INITIAL:
      NeuterUnencryptedData();
      break;
    case ENCRYPTION_HANDSHAKE:
      NeuterHandshakeData();
      break;
    case ENCRYPTION_ZERO_RTT:
      break;
    case ENCRYPTION_FORWARD_SECURE:
      QUIC_BUG(quic_bug_10866_8)
          << ENDPOINT << "Discarding 1-RTT keys is not allowed";
      break;
    default:
      QUIC_BUG(quic_bug_10866_9)
          << ENDPOINT
          << "Cannot discard keys for unknown encryption level: " << level;
  }
}

void QuicSession::NeuterHandshakeData() {
  GetMutableCryptoStream()->NeuterStreamDataOfEncryptionLevel(
      ENCRYPTION_HANDSHAKE);
  connection()->OnHandshakeComplete();
}

void QuicSession::OnZeroRttRejected(int reason) {
  was_zero_rtt_rejected_ = true;
  connection_->MarkZeroRttPacketsForRetransmission(reason);
  if (connection_->encryption_level() == ENCRYPTION_FORWARD_SECURE) {
    QUIC_BUG(quic_bug_10866_10)
        << "1-RTT keys already available when 0-RTT is rejected.";
    connection_->CloseConnection(
        QUIC_INTERNAL_ERROR,
        "1-RTT keys already available when 0-RTT is rejected.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }
}

bool QuicSession::FillTransportParameters(TransportParameters* params) {
  if (version().UsesTls()) {
    if (perspective() == Perspective::IS_SERVER) {
      config_.SetOriginalConnectionIdToSend(
          connection_->GetOriginalDestinationConnectionId());
      config_.SetInitialSourceConnectionIdToSend(connection_->connection_id());
    } else {
      config_.SetInitialSourceConnectionIdToSend(
          connection_->client_connection_id());
    }
  }
  return config_.FillTransportParameters(params);
}

QuicErrorCode QuicSession::ProcessTransportParameters(
    const TransportParameters& params, bool is_resumption,
    std::string* error_details) {
  return config_.ProcessTransportParameters(params, is_resumption,
                                            error_details);
}

void QuicSession::OnHandshakeCallbackDone() {
  if (!connection_->connected()) {
    return;
  }

  if (!connection()->is_processing_packet()) {
    connection()->MaybeProcessUndecryptablePackets();
  }
}

bool QuicSession::PacketFlusherAttached() const {
  QUICHE_DCHECK(connection_->connected());
  return connection()->packet_creator().PacketFlusherAttached();
}

void QuicSession::OnEncryptedClientHelloSent(
    absl::string_view client_hello) const {
  connection()->OnEncryptedClientHelloSent(client_hello);
}

void QuicSession::OnEncryptedClientHelloReceived(
    absl::string_view client_hello) const {
  connection()->OnEncryptedClientHelloReceived(client_hello);
}

void QuicSession::OnCryptoHandshakeMessageSent(
    const CryptoHandshakeMessage& /*message*/) {}

void QuicSession::OnCryptoHandshakeMessageReceived(
    const CryptoHandshakeMessage& /*message*/) {}

void QuicSession::RegisterStreamPriority(QuicStreamId id, bool is_static,
                                         const QuicStreamPriority& priority) {
  write_blocked_streams()->RegisterStream(id, is_static, priority);
}

void QuicSession::UnregisterStreamPriority(QuicStreamId id) {
  write_blocked_streams()->UnregisterStream(id);
}

void QuicSession::UpdateStreamPriority(QuicStreamId id,
                                       const QuicStreamPriority& new_priority) {
  write_blocked_streams()->UpdateStreamPriority(id, new_priority);
}

void QuicSession::ActivateStream(std::unique_ptr<QuicStream> stream) {
  const bool should_keep_alive = ShouldKeepConnectionAlive();
  QuicStreamId stream_id = stream->id();
  bool is_static = stream->is_static();
  QUIC_DVLOG(1) << ENDPOINT << "num_streams: " << stream_map_.size()
                << ". activating stream " << stream_id;
  QUICHE_DCHECK(!stream_map_.contains(stream_id));
  stream_map_[stream_id] = std::move(stream);
  if (is_static) {
    ++num_static_streams_;
    return;
  }
  if (version().HasIetfQuicFrames() && IsIncomingStream(stream_id) &&
      max_streams_accepted_per_loop_ != kMaxQuicStreamCount) {
    QUICHE_DCHECK(!ExceedsPerLoopStreamLimit());
    // Per-loop stream limit is emposed.
    ++new_incoming_streams_in_current_loop_;
    if (!stream_count_reset_alarm_->IsSet()) {
      stream_count_reset_alarm_->Set(connection()->clock()->ApproximateNow());
    }
  }
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Do not inform stream ID manager of static streams.
    stream_id_manager_.ActivateStream(
        /*is_incoming=*/IsIncomingStream(stream_id));
  }
  if (perspective() == Perspective::IS_CLIENT &&
      connection()->multi_port_stats() != nullptr && !should_keep_alive &&
      ShouldKeepConnectionAlive()) {
    connection()->MaybeProbeMultiPortPath();
  }
}

QuicStreamId QuicSession::GetNextOutgoingBidirectionalStreamId() {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.GetNextOutgoingBidirectionalStreamId();
  }
  return stream_id_manager_.GetNextOutgoingStreamId();
}

QuicStreamId QuicSession::GetNextOutgoingUnidirectionalStreamId() {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.GetNextOutgoingUnidirectionalStreamId();
  }
  return stream_id_manager_.GetNextOutgoingStreamId();
}

bool QuicSession::CanOpenNextOutgoingBidirectionalStream() {
  if (liveness_testing_in_progress_) {
    QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective());
    QUIC_CODE_COUNT(
        quic_client_fails_to_create_stream_liveness_testing_in_progress);
    return false;
  }
  if (!VersionHasIetfQuicFrames(transport_version())) {
    if (!stream_id_manager_.CanOpenNextOutgoingStream()) {
      return false;
    }
  } else {
    if (!ietf_streamid_manager_.CanOpenNextOutgoingBidirectionalStream()) {
      QUIC_CODE_COUNT(
          quic_fails_to_create_stream_close_too_many_streams_created);
      if (is_configured_) {
        // Send STREAM_BLOCKED after config negotiated.
        control_frame_manager_.WriteOrBufferStreamsBlocked(
            ietf_streamid_manager_.max_outgoing_bidirectional_streams(),
            /*unidirectional=*/false);
      }
      return false;
    }
  }
  if (perspective() == Perspective::IS_CLIENT &&
      connection_->MaybeTestLiveness()) {
    // Now is relatively close to the idle timeout having the risk that requests
    // could be discarded at the server.
    liveness_testing_in_progress_ = true;
    QUIC_CODE_COUNT(quic_client_fails_to_create_stream_close_to_idle_timeout);
    return false;
  }
  return true;
}

bool QuicSession::CanOpenNextOutgoingUnidirectionalStream() {
  if (!VersionHasIetfQuicFrames(transport_version())) {
    return stream_id_manager_.CanOpenNextOutgoingStream();
  }
  if (ietf_streamid_manager_.CanOpenNextOutgoingUnidirectionalStream()) {
    return true;
  }
  if (is_configured_) {
    // Send STREAM_BLOCKED after config negotiated.
    control_frame_manager_.WriteOrBufferStreamsBlocked(
        ietf_streamid_manager_.max_outgoing_unidirectional_streams(),
        /*unidirectional=*/true);
  }
  return false;
}

QuicStreamCount QuicSession::GetAdvertisedMaxIncomingBidirectionalStreams()
    const {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(transport_version()));
  return ietf_streamid_manager_.advertised_max_incoming_bidirectional_streams();
}

QuicStream* QuicSession::GetOrCreateStream(const QuicStreamId stream_id) {
  QUICHE_DCHECK(!pending_stream_map_.contains(stream_id));
  if (QuicUtils::IsCryptoStreamId(transport_version(), stream_id)) {
    return GetMutableCryptoStream();
  }

  StreamMap::iterator it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return it->second->IsZombie() ? nullptr : it->second.get();
  }

  if (IsClosedStream(stream_id)) {
    return nullptr;
  }

  if (!IsIncomingStream(stream_id)) {
    HandleFrameOnNonexistentOutgoingStream(stream_id);
    return nullptr;
  }

  // TODO(fkastenholz): If we are creating a new stream and we have sent a
  // goaway, we should ignore the stream creation. Need to add code to A) test
  // if goaway was sent ("if (transport_goaway_sent_)") and B) reject stream
  // creation ("return nullptr")

  if (!MaybeIncreaseLargestPeerStreamId(stream_id)) {
    return nullptr;
  }

  if (!VersionHasIetfQuicFrames(transport_version()) &&
      !stream_id_manager_.CanOpenIncomingStream()) {
    // Refuse to open the stream.
    ResetStream(stream_id, QUIC_REFUSED_STREAM);
    return nullptr;
  }

  return CreateIncomingStream(stream_id);
}

void QuicSession::StreamDraining(QuicStreamId stream_id, bool unidirectional) {
  QUICHE_DCHECK(stream_map_.contains(stream_id));
  QUIC_DVLOG(1) << ENDPOINT << "Stream " << stream_id << " is draining";
  if (VersionHasIetfQuicFrames(transport_version())) {
    ietf_streamid_manager_.OnStreamClosed(stream_id);
  } else {
    stream_id_manager_.OnStreamClosed(
        /*is_incoming=*/IsIncomingStream(stream_id));
  }
  ++num_draining_streams_;
  if (!IsIncomingStream(stream_id)) {
    ++num_outgoing_draining_streams_;
    if (!VersionHasIetfQuicFrames(transport_version())) {
      OnCanCreateNewOutgoingStream(unidirectional);
    }
  }
}

bool QuicSession::MaybeIncreaseLargestPeerStreamId(
    const QuicStreamId stream_id) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    std::string error_details;
    if (ietf_streamid_manager_.MaybeIncreaseLargestPeerStreamId(
            stream_id, &error_details)) {
      return true;
    }
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, error_details,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  if (!stream_id_manager_.MaybeIncreaseLargestPeerStreamId(stream_id)) {
    connection()->CloseConnection(
        QUIC_TOO_MANY_AVAILABLE_STREAMS,
        absl::StrCat(stream_id, " exceeds available streams ",
                     stream_id_manager_.MaxAvailableStreams()),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  return true;
}

bool QuicSession::ShouldYield(QuicStreamId stream_id) {
  if (stream_id == currently_writing_stream_id_) {
    return false;
  }
  return write_blocked_streams()->ShouldYield(stream_id);
}

PendingStream* QuicSession::GetOrCreatePendingStream(QuicStreamId stream_id) {
  auto it = pending_stream_map_.find(stream_id);
  if (it != pending_stream_map_.end()) {
    return it->second.get();
  }

  if (IsClosedStream(stream_id) ||
      !MaybeIncreaseLargestPeerStreamId(stream_id)) {
    return nullptr;
  }

  auto pending = std::make_unique<PendingStream>(stream_id, this);
  PendingStream* unowned_pending = pending.get();
  pending_stream_map_[stream_id] = std::move(pending);
  return unowned_pending;
}

void QuicSession::set_largest_peer_created_stream_id(
    QuicStreamId largest_peer_created_stream_id) {
  QUICHE_DCHECK(!VersionHasIetfQuicFrames(transport_version()));
  stream_id_manager_.set_largest_peer_created_stream_id(
      largest_peer_created_stream_id);
}

QuicStreamId QuicSession::GetLargestPeerCreatedStreamId(
    bool unidirectional) const {
  // This method is only used in IETF QUIC.
  QUICHE_DCHECK(VersionHasIetfQuicFrames(transport_version()));
  return ietf_streamid_manager_.GetLargestPeerCreatedStreamId(unidirectional);
}

void QuicSession::DeleteConnection() {
  if (connection_) {
    delete connection_;
    connection_ = nullptr;
  }
}

bool QuicSession::MaybeSetStreamPriority(QuicStreamId stream_id,
                                         const QuicStreamPriority& priority) {
  auto active_stream = stream_map_.find(stream_id);
  if (active_stream != stream_map_.end()) {
    active_stream->second->SetPriority(priority);
    return true;
  }

  return false;
}

bool QuicSession::IsClosedStream(QuicStreamId id) {
  QUICHE_DCHECK_NE(QuicUtils::GetInvalidStreamId(transport_version()), id);
  if (IsOpenStream(id)) {
    // Stream is active
    return false;
  }

  if (VersionHasIetfQuicFrames(transport_version())) {
    return !ietf_streamid_manager_.IsAvailableStream(id);
  }

  return !stream_id_manager_.IsAvailableStream(id);
}

bool QuicSession::IsOpenStream(QuicStreamId id) {
  QUICHE_DCHECK_NE(QuicUtils::GetInvalidStreamId(transport_version()), id);
  const StreamMap::iterator it = stream_map_.find(id);
  if (it != stream_map_.end()) {
    return !it->second->IsZombie();
  }
  if (pending_stream_map_.contains(id) ||
      QuicUtils::IsCryptoStreamId(transport_version(), id)) {
    // Stream is active
    return true;
  }
  return false;
}

bool QuicSession::IsStaticStream(QuicStreamId id) const {
  auto it = stream_map_.find(id);
  if (it == stream_map_.end()) {
    return false;
  }
  return it->second->is_static();
}

size_t QuicSession::GetNumActiveStreams() const {
  QUICHE_DCHECK_GE(
      static_cast<QuicStreamCount>(stream_map_.size()),
      num_static_streams_ + num_draining_streams_ + num_zombie_streams_);
  return stream_map_.size() - num_draining_streams_ - num_static_streams_ -
         num_zombie_streams_;
}

void QuicSession::MarkConnectionLevelWriteBlocked(QuicStreamId id) {
  if (GetOrCreateStream(id) == nullptr) {
    QUIC_BUG(quic_bug_10866_11)
        << "Marking unknown stream " << id << " blocked.";
    QUIC_LOG_FIRST_N(ERROR, 2) << QuicStackTrace();
  }

  QUIC_DVLOG(1) << ENDPOINT << "Adding stream " << id
                << " to write-blocked list";

  write_blocked_streams_->AddStream(id);
}

bool QuicSession::HasDataToWrite() const {
  return write_blocked_streams_->HasWriteBlockedSpecialStream() ||
         write_blocked_streams_->HasWriteBlockedDataStreams() ||
         connection_->HasQueuedData() ||
         !streams_with_pending_retransmission_.empty() ||
         control_frame_manager_.WillingToWrite();
}

void QuicSession::OnAckNeedsRetransmittableFrame() {
  flow_controller_.SendWindowUpdate();
}

void QuicSession::SendAckFrequency(const QuicAckFrequencyFrame& frame) {
  control_frame_manager_.WriteOrBufferAckFrequency(frame);
}

void QuicSession::SendNewConnectionId(const QuicNewConnectionIdFrame& frame) {
  control_frame_manager_.WriteOrBufferNewConnectionId(
      frame.connection_id, frame.sequence_number, frame.retire_prior_to,
      frame.stateless_reset_token);
}

void QuicSession::SendRetireConnectionId(uint64_t sequence_number) {
  if (GetQuicReloadableFlag(
          quic_no_write_control_frame_upon_connection_close2)) {
    QUIC_RELOADABLE_FLAG_COUNT(
        quic_no_write_control_frame_upon_connection_close2);
    if (!connection_->connected()) {
      return;
    }
  }
  control_frame_manager_.WriteOrBufferRetireConnectionId(sequence_number);
}

bool QuicSession::MaybeReserveConnectionId(
    const QuicConnectionId& server_connection_id) {
  if (visitor_) {
    return visitor_->TryAddNewConnectionId(
        connection_->GetOneActiveServerConnectionId(), server_connection_id);
  }
  return true;
}

void QuicSession::OnServerConnectionIdRetired(
    const QuicConnectionId& server_connection_id) {
  if (visitor_) {
    visitor_->OnConnectionIdRetired(server_connection_id);
  }
}

bool QuicSession::IsConnectionFlowControlBlocked() const {
  return flow_controller_.IsBlocked();
}

bool QuicSession::IsStreamFlowControlBlocked() {
  for (auto const& kv : stream_map_) {
    if (kv.second->IsFlowControlBlocked()) {
      return true;
    }
  }
  if (!QuicVersionUsesCryptoFrames(transport_version()) &&
      GetMutableCryptoStream()->IsFlowControlBlocked()) {
    return true;
  }
  return false;
}

size_t QuicSession::MaxAvailableBidirectionalStreams() const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.GetMaxAllowdIncomingBidirectionalStreams();
  }
  return stream_id_manager_.MaxAvailableStreams();
}

size_t QuicSession::MaxAvailableUnidirectionalStreams() const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.GetMaxAllowdIncomingUnidirectionalStreams();
  }
  return stream_id_manager_.MaxAvailableStreams();
}

bool QuicSession::IsIncomingStream(QuicStreamId id) const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return !QuicUtils::IsOutgoingStreamId(version(), id, perspective_);
  }
  return stream_id_manager_.IsIncomingStream(id);
}

void QuicSession::MaybeCloseZombieStream(QuicStreamId id) {
  auto it = stream_map_.find(id);
  if (it == stream_map_.end()) {
    return;
  }
  --num_zombie_streams_;
  closed_streams_.push_back(std::move(it->second));
  stream_map_.erase(it);

  if (!closed_streams_clean_up_alarm_->IsSet()) {
    closed_streams_clean_up_alarm_->Set(connection_->clock()->ApproximateNow());
  }
  // Do not retransmit data of a closed stream.
  streams_with_pending_retransmission_.erase(id);
  connection_->QuicBugIfHasPendingFrames(id);
}

QuicStream* QuicSession::GetStream(QuicStreamId id) const {
  auto active_stream = stream_map_.find(id);
  if (active_stream != stream_map_.end()) {
    return active_stream->second.get();
  }

  if (QuicUtils::IsCryptoStreamId(transport_version(), id)) {
    return const_cast<QuicCryptoStream*>(GetCryptoStream());
  }

  return nullptr;
}

QuicStream* QuicSession::GetActiveStream(QuicStreamId id) const {
  auto stream = stream_map_.find(id);
  if (stream != stream_map_.end() && !stream->second->is_static()) {
    return stream->second.get();
  }
  return nullptr;
}

bool QuicSession::OnFrameAcked(const QuicFrame& frame,
                               QuicTime::Delta ack_delay_time,
                               QuicTime receive_timestamp) {
  if (frame.type == MESSAGE_FRAME) {
    OnMessageAcked(frame.message_frame->message_id, receive_timestamp);
    return true;
  }
  if (frame.type == CRYPTO_FRAME) {
    return GetMutableCryptoStream()->OnCryptoFrameAcked(*frame.crypto_frame,
                                                        ack_delay_time);
  }
  if (frame.type != STREAM_FRAME) {
    bool acked = control_frame_manager_.OnControlFrameAcked(frame);
    if (acked && frame.type == MAX_STREAMS_FRAME) {
      // Since there is a 2 frame limit on the number of outstanding max_streams
      // frames, when an outstanding max_streams frame is ack'd that frees up
      // room to potntially send another.
      ietf_streamid_manager_.MaybeSendMaxStreamsFrame();
    }
    return acked;
  }
  bool new_stream_data_acked = false;
  QuicStream* stream = GetStream(frame.stream_frame.stream_id);
  // Stream can already be reset when sent frame gets acked.
  if (stream != nullptr) {
    QuicByteCount newly_acked_length = 0;
    new_stream_data_acked = stream->OnStreamFrameAcked(
        frame.stream_frame.offset, frame.stream_frame.data_length,
        frame.stream_frame.fin, ack_delay_time, receive_timestamp,
        &newly_acked_length);
    if (!stream->HasPendingRetransmission()) {
      streams_with_pending_retransmission_.erase(stream->id());
    }
  }
  return new_stream_data_acked;
}

void QuicSession::OnStreamFrameRetransmitted(const QuicStreamFrame& frame) {
  QuicStream* stream = GetStream(frame.stream_id);
  if (stream == nullptr) {
    QUIC_BUG(quic_bug_10866_12)
        << "Stream: " << frame.stream_id << " is closed when " << frame
        << " is retransmitted.";
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Attempt to retransmit frame of a closed stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  stream->OnStreamFrameRetransmitted(frame.offset, frame.data_length,
                                     frame.fin);
}

void QuicSession::OnFrameLost(const QuicFrame& frame) {
  if (frame.type == MESSAGE_FRAME) {
    ++total_datagrams_lost_;
    OnMessageLost(frame.message_frame->message_id);
    return;
  }
  if (frame.type == CRYPTO_FRAME) {
    GetMutableCryptoStream()->OnCryptoFrameLost(frame.crypto_frame);
    return;
  }
  if (frame.type != STREAM_FRAME) {
    control_frame_manager_.OnControlFrameLost(frame);
    return;
  }
  QuicStream* stream = GetStream(frame.stream_frame.stream_id);
  if (stream == nullptr) {
    return;
  }
  stream->OnStreamFrameLost(frame.stream_frame.offset,
                            frame.stream_frame.data_length,
                            frame.stream_frame.fin);
  if (stream->HasPendingRetransmission() &&
      !streams_with_pending_retransmission_.contains(
          frame.stream_frame.stream_id)) {
    streams_with_pending_retransmission_.insert(
        std::make_pair(frame.stream_frame.stream_id, true));
  }
}

bool QuicSession::RetransmitFrames(const QuicFrames& frames,
                                   TransmissionType type) {
  QuicConnection::ScopedPacketFlusher retransmission_flusher(connection_);
  for (const QuicFrame& frame : frames) {
    if (frame.type == MESSAGE_FRAME) {
      // Do not retransmit MESSAGE frames.
      continue;
    }
    if (frame.type == CRYPTO_FRAME) {
      if (!GetMutableCryptoStream()->RetransmitData(frame.crypto_frame, type)) {
        return false;
      }
      continue;
    }
    if (frame.type != STREAM_FRAME) {
      if (!control_frame_manager_.RetransmitControlFrame(frame, type)) {
        return false;
      }
      continue;
    }
    QuicStream* stream = GetStream(frame.stream_frame.stream_id);
    if (stream != nullptr &&
        !stream->RetransmitStreamData(frame.stream_frame.offset,
                                      frame.stream_frame.data_length,
                                      frame.stream_frame.fin, type)) {
      return false;
    }
  }
  return true;
}

bool QuicSession::IsFrameOutstanding(const QuicFrame& frame) const {
  if (frame.type == MESSAGE_FRAME) {
    return false;
  }
  if (frame.type == CRYPTO_FRAME) {
    return GetCryptoStream()->IsFrameOutstanding(
        frame.crypto_frame->level, frame.crypto_frame->offset,
        frame.crypto_frame->data_length);
  }
  if (frame.type != STREAM_FRAME) {
    return control_frame_manager_.IsControlFrameOutstanding(frame);
  }
  QuicStream* stream = GetStream(frame.stream_frame.stream_id);
  return stream != nullptr &&
         stream->IsStreamFrameOutstanding(frame.stream_frame.offset,
                                          frame.stream_frame.data_length,
                                          frame.stream_frame.fin);
}

bool QuicSession::HasUnackedCryptoData() const {
  const QuicCryptoStream* crypto_stream = GetCryptoStream();
  return crypto_stream->IsWaitingForAcks() || crypto_stream->HasBufferedData();
}

bool QuicSession::HasUnackedStreamData() const {
  for (const auto& it : stream_map_) {
    if (it.second->IsWaitingForAcks()) {
      return true;
    }
  }
  return false;
}

HandshakeState QuicSession::GetHandshakeState() const {
  return GetCryptoStream()->GetHandshakeState();
}

QuicByteCount QuicSession::GetFlowControlSendWindowSize(QuicStreamId id) {
  QUICHE_DCHECK(GetQuicRestartFlag(quic_opport_bundle_qpack_decoder_data5));
  auto it = stream_map_.find(id);
  if (it == stream_map_.end()) {
    // No flow control for invalid or inactive stream ids. Returning uint64max
    // allows QuicPacketCreator to write as much data as possible.
    return std::numeric_limits<QuicByteCount>::max();
  }
  return it->second->CalculateSendWindowSize();
}

WriteStreamDataResult QuicSession::WriteStreamData(QuicStreamId id,
                                                   QuicStreamOffset offset,
                                                   QuicByteCount data_length,
                                                   QuicDataWriter* writer) {
  QuicStream* stream = GetStream(id);
  if (stream == nullptr) {
    // This causes the connection to be closed because of failed to serialize
    // packet.
    QUIC_BUG(quic_bug_10866_13)
        << "Stream " << id << " does not exist when trying to write data."
        << " version:" << transport_version();
    return STREAM_MISSING;
  }
  if (stream->WriteStreamData(offset, data_length, writer)) {
    return WRITE_SUCCESS;
  }
  return WRITE_FAILED;
}

bool QuicSession::WriteCryptoData(EncryptionLevel level,
                                  QuicStreamOffset offset,
                                  QuicByteCount data_length,
                                  QuicDataWriter* writer) {
  return GetMutableCryptoStream()->WriteCryptoFrame(level, offset, data_length,
                                                    writer);
}

StatelessResetToken QuicSession::GetStatelessResetToken() const {
  return QuicUtils::GenerateStatelessResetToken(connection_->connection_id());
}

bool QuicSession::CanWriteStreamData() const {
  // Don't write stream data if there are queued data packets.
  if (connection_->HasQueuedPackets()) {
    return false;
  }
  // Immediately write handshake data.
  if (HasPendingHandshake()) {
    return true;
  }
  return connection_->CanWrite(HAS_RETRANSMITTABLE_DATA);
}

bool QuicSession::RetransmitLostData() {
  QuicConnection::ScopedPacketFlusher retransmission_flusher(connection_);
  // Retransmit crypto data first.
  bool uses_crypto_frames = QuicVersionUsesCryptoFrames(transport_version());
  if (QuicCryptoStream* const crypto_stream = GetMutableCryptoStream();
      uses_crypto_frames && crypto_stream->HasPendingCryptoRetransmission()) {
    crypto_stream->WritePendingCryptoRetransmission();
  }
  // Retransmit crypto data in stream 1 frames (version < 47).
  if (!uses_crypto_frames &&
      streams_with_pending_retransmission_.contains(
          QuicUtils::GetCryptoStreamId(transport_version()))) {
    // Retransmit crypto data first.
    QuicStream* const crypto_stream =
        GetStream(QuicUtils::GetCryptoStreamId(transport_version()));
    crypto_stream->OnCanWrite();
    QUICHE_DCHECK(CheckStreamWriteBlocked(crypto_stream));
    if (crypto_stream->HasPendingRetransmission()) {
      // Connection is write blocked.
      return false;
    } else {
      streams_with_pending_retransmission_.erase(
          QuicUtils::GetCryptoStreamId(transport_version()));
    }
  }
  if (control_frame_manager_.HasPendingRetransmission()) {
    control_frame_manager_.OnCanWrite();
    if (control_frame_manager_.HasPendingRetransmission()) {
      return false;
    }
  }
  while (!streams_with_pending_retransmission_.empty()) {
    if (!CanWriteStreamData()) {
      break;
    }
    // Retransmit lost data on headers and data streams.
    const QuicStreamId id = streams_with_pending_retransmission_.begin()->first;
    QuicStream* stream = GetStream(id);
    if (stream != nullptr) {
      stream->OnCanWrite();
      QUICHE_DCHECK(CheckStreamWriteBlocked(stream));
      if (stream->HasPendingRetransmission()) {
        // Connection is write blocked.
        break;
      } else if (!streams_with_pending_retransmission_.empty() &&
                 streams_with_pending_retransmission_.begin()->first == id) {
        // Retransmit lost data may cause connection close. If this stream
        // has not yet sent fin, a RST_STREAM will be sent and it will be
        // removed from streams_with_pending_retransmission_.
        streams_with_pending_retransmission_.pop_front();
      }
    } else {
      QUIC_BUG(quic_bug_10866_14)
          << "Try to retransmit data of a closed stream";
      streams_with_pending_retransmission_.pop_front();
    }
  }

  return streams_with_pending_retransmission_.empty();
}

void QuicSession::NeuterUnencryptedData() {
  QuicCryptoStream* crypto_stream = GetMutableCryptoStream();
  crypto_stream->NeuterUnencryptedStreamData();
  if (!crypto_stream->HasPendingRetransmission() &&
      !QuicVersionUsesCryptoFrames(transport_version())) {
    streams_with_pending_retransmission_.erase(
        QuicUtils::GetCryptoStreamId(transport_version()));
  }
  connection_->NeuterUnencryptedPackets();
}

void QuicSession::SetTransmissionType(TransmissionType type) {
  connection_->SetTransmissionType(type);
}

MessageResult QuicSession::SendMessage(
    absl::Span<quiche::QuicheMemSlice> message) {
  return SendMessage(message, /*flush=*/false);
}

MessageResult QuicSession::SendMessage(quiche::QuicheMemSlice message) {
  return SendMessage(absl::MakeSpan(&message, 1), /*flush=*/false);
}

MessageResult QuicSession::SendMessage(
    absl::Span<quiche::QuicheMemSlice> message, bool flush) {
  QUICHE_DCHECK(connection_->connected())
      << ENDPOINT << "Try to write messages when connection is closed.";
  if (!IsEncryptionEstablished()) {
    return {MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED, 0};
  }
  QuicConnection::ScopedEncryptionLevelContext context(
      connection(), GetEncryptionLevelToSendApplicationData());
  MessageStatus result =
      connection_->SendMessage(last_message_id_ + 1, message, flush);
  if (result == MESSAGE_STATUS_SUCCESS) {
    return {result, ++last_message_id_};
  }
  return {result, 0};
}

void QuicSession::OnMessageAcked(QuicMessageId message_id,
                                 QuicTime /*receive_timestamp*/) {
  QUIC_DVLOG(1) << ENDPOINT << "message " << message_id << " gets acked.";
}

void QuicSession::OnMessageLost(QuicMessageId message_id) {
  QUIC_DVLOG(1) << ENDPOINT << "message " << message_id
                << " is considered lost";
}

void QuicSession::CleanUpClosedStreams() { closed_streams_.clear(); }

QuicPacketLength QuicSession::GetCurrentLargestMessagePayload() const {
  return connection_->GetCurrentLargestMessagePayload();
}

QuicPacketLength QuicSession::GetGuaranteedLargestMessagePayload() const {
  return connection_->GetGuaranteedLargestMessagePayload();
}

QuicStreamId QuicSession::next_outgoing_bidirectional_stream_id() const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.next_outgoing_bidirectional_stream_id();
  }
  return stream_id_manager_.next_outgoing_stream_id();
}

QuicStreamId QuicSession::next_outgoing_unidirectional_stream_id() const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.next_outgoing_unidirectional_stream_id();
  }
  return stream_id_manager_.next_outgoing_stream_id();
}

bool QuicSession::OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) {
  const bool allow_new_streams =
      frame.unidirectional
          ? ietf_streamid_manager_.MaybeAllowNewOutgoingUnidirectionalStreams(
                frame.stream_count)
          : ietf_streamid_manager_.MaybeAllowNewOutgoingBidirectionalStreams(
                frame.stream_count);
  if (allow_new_streams) {
    OnCanCreateNewOutgoingStream(frame.unidirectional);
  }

  return true;
}

bool QuicSession::OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) {
  std::string error_details;
  if (ietf_streamid_manager_.OnStreamsBlockedFrame(frame, &error_details)) {
    return true;
  }
  connection_->CloseConnection(
      QUIC_STREAMS_BLOCKED_ERROR, error_details,
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  return false;
}

size_t QuicSession::max_open_incoming_bidirectional_streams() const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.GetMaxAllowdIncomingBidirectionalStreams();
  }
  return stream_id_manager_.max_open_incoming_streams();
}

size_t QuicSession::max_open_incoming_unidirectional_streams() const {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return ietf_streamid_manager_.GetMaxAllowdIncomingUnidirectionalStreams();
  }
  return stream_id_manager_.max_open_incoming_streams();
}

std::vector<absl::string_view>::const_iterator QuicSession::SelectAlpn(
    const std::vector<absl::string_view>& alpns) const {
  const std::string alpn = AlpnForVersion(connection()->version());
  return std::find(alpns.cbegin(), alpns.cend(), alpn);
}

void QuicSession::OnAlpnSelected(absl::string_view alpn) {
  QUIC_DLOG(INFO) << (perspective() == Perspective::IS_SERVER ? "Server: "
                                                              : "Client: ")
                  << "ALPN selected: " << alpn;
}

void QuicSession::NeuterCryptoDataOfEncryptionLevel(EncryptionLevel level) {
  GetMutableCryptoStream()->NeuterStreamDataOfEncryptionLevel(level);
}

void QuicSession::PerformActionOnActiveStreams(
    quiche::UnretainedCallback<bool(QuicStream*)> action) {
  std::vector<QuicStream*> active_streams;
  for (const auto& it : stream_map_) {
    if (!it.second->is_static() && !it.second->IsZombie()) {
      active_streams.push_back(it.second.get());
    }
  }

  for (QuicStream* stream : active_streams) {
    if (!action(stream)) {
      return;
    }
  }
}

void QuicSession::PerformActionOnActiveStreams(
    quiche::UnretainedCallback<bool(QuicStream*)> action) const {
  for (const auto& it : stream_map_) {
    if (!it.second->is_static() && !it.second->IsZombie() &&
        !action(it.second.get())) {
      return;
    }
  }
}

EncryptionLevel QuicSession::GetEncryptionLevelToSendApplicationData() const {
  return connection_->framer().GetEncryptionLevelToSendApplicationData();
}

void QuicSession::ProcessAllPendingStreams() {
  std::vector<PendingStream*> pending_streams;
  pending_streams.reserve(pending_stream_map_.size());
  for (auto it = pending_stream_map_.begin(); it != pending_stream_map_.end();
       ++it) {
    pending_streams.push_back(it->second.get());
  }
  for (auto* pending_stream : pending_streams) {
    if (!MaybeProcessPendingStream(pending_stream)) {
      // Defer any further pending stream processing to the next event loop.
      return;
    }
  }
}

void QuicSession::ValidatePath(
    std::unique_ptr<QuicPathValidationContext> context,
    std::unique_ptr<QuicPathValidator::ResultDelegate> result_delegate,
    PathValidationReason reason) {
  connection_->ValidatePath(std::move(context), std::move(result_delegate),
                            reason);
}

bool QuicSession::HasPendingPathValidation() const {
  return connection_->HasPendingPathValidation();
}

bool QuicSession::MigratePath(const QuicSocketAddress& self_address,
                              const QuicSocketAddress& peer_address,
                              QuicPacketWriter* writer, bool owns_writer) {
  return connection_->MigratePath(self_address, peer_address, writer,
                                  owns_writer);
}

bool QuicSession::ValidateToken(absl::string_view token) {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_SERVER);
  if (GetQuicFlag(quic_reject_retry_token_in_initial_packet)) {
    return false;
  }
  if (token.empty() || token[0] != kAddressTokenPrefix) {
    // Validate the prefix for token received in NEW_TOKEN frame.
    return false;
  }
  const bool valid = GetCryptoStream()->ValidateAddressToken(
      absl::string_view(token.data() + 1, token.length() - 1));
  if (valid) {
    const CachedNetworkParameters* cached_network_params =
        GetCryptoStream()->PreviousCachedNetworkParams();
    if (cached_network_params != nullptr &&
        cached_network_params->timestamp() > 0) {
      connection()->OnReceiveConnectionState(*cached_network_params);
    }
  }
  return valid;
}

void QuicSession::OnServerPreferredAddressAvailable(
    const QuicSocketAddress& server_preferred_address) {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  if (visitor_ != nullptr) {
    visitor_->OnServerPreferredAddressAvailable(server_preferred_address);
  }
}

QuicStream* QuicSession::ProcessPendingStream(PendingStream* pending) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK(connection()->connected());
  QuicStreamId stream_id = pending->id();
  QUIC_BUG_IF(bad pending stream, !IsIncomingStream(stream_id))
      << "Pending stream " << stream_id << " is not an incoming stream.";
  // TODO(b/305051334) check if this stream is incoming stream before making it
  // pending. If not, connection should be closed.
  StreamType stream_type = QuicUtils::GetStreamType(
      stream_id, perspective(), /*peer_initiated=*/true, version());
  switch (stream_type) {
    case BIDIRECTIONAL: {
      return ProcessBidirectionalPendingStream(pending);
    }
    case READ_UNIDIRECTIONAL: {
      return ProcessReadUnidirectionalPendingStream(pending);
    }
    case WRITE_UNIDIRECTIONAL:
      ABSL_FALLTHROUGH_INTENDED;
    case CRYPTO:
      QUICHE_BUG(unexpected pending stream)
          << "Unexpected pending stream " << stream_id << " with type "
          << stream_type;
      return nullptr;
  }
  return nullptr;  // Unreachable, unless the enum value is out-of-range
                   // (potentially undefined behavior)
}

bool QuicSession::ExceedsPerLoopStreamLimit() const {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  return new_incoming_streams_in_current_loop_ >=
         max_streams_accepted_per_loop_;
}

void QuicSession::OnStreamCountReset() {
  const bool exceeded_per_loop_stream_limit = ExceedsPerLoopStreamLimit();
  new_incoming_streams_in_current_loop_ = 0;
  if (exceeded_per_loop_stream_limit) {
    QUIC_CODE_COUNT_N(quic_pending_stream, 2, 3);
    // Convert as many leftover pending streams from last loop to active streams
    // as allowed.
    ProcessAllPendingStreams();
  }
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
