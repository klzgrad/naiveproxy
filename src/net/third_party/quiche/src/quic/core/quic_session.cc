// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_session.h"

#include <cstdint>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quic/core/frames/quic_ack_frequency_frame.h"
#include "quic/core/quic_connection.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_flow_controller.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "quic/platform/api/quic_flag_utils.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_map_util.h"
#include "quic/platform/api/quic_ptr_util.h"
#include "quic/platform/api/quic_server_stats.h"
#include "quic/platform/api/quic_stack_trace.h"

using spdy::SpdyPriority;

namespace quic {

namespace {

class ClosedStreamsCleanUpDelegate : public QuicAlarm::Delegate {
 public:
  explicit ClosedStreamsCleanUpDelegate(QuicSession* session)
      : session_(session) {}
  ClosedStreamsCleanUpDelegate(const ClosedStreamsCleanUpDelegate&) = delete;
  ClosedStreamsCleanUpDelegate& operator=(const ClosedStreamsCleanUpDelegate&) =
      delete;

  void OnAlarm() override { session_->CleanUpClosedStreams(); }

 private:
  QuicSession* session_;
};

}  // namespace

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicSession::QuicSession(
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicStreamCount num_expected_unidirectional_static_streams)
    : QuicSession(connection,
                  owner,
                  config,
                  supported_versions,
                  num_expected_unidirectional_static_streams,
                  nullptr) {}

QuicSession::QuicSession(
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicStreamCount num_expected_unidirectional_static_streams,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer)
    : connection_(connection),
      perspective_(connection->perspective()),
      visitor_(owner),
      write_blocked_streams_(connection->transport_version()),
      config_(config),
      stream_id_manager_(perspective(),
                         connection->transport_version(),
                         kDefaultMaxStreamsPerConnection,
                         config_.GetMaxBidirectionalStreamsToSend()),
      ietf_streamid_manager_(perspective(),
                             connection->version(),
                             this,
                             0,
                             num_expected_unidirectional_static_streams,
                             config_.GetMaxBidirectionalStreamsToSend(),
                             config_.GetMaxUnidirectionalStreamsToSend() +
                                 num_expected_unidirectional_static_streams),
      num_draining_streams_(0),
      num_outgoing_draining_streams_(0),
      num_static_streams_(0),
      num_zombie_streams_(0),
      flow_controller_(
          this,
          QuicUtils::GetInvalidStreamId(connection->transport_version()),
          /*is_connection_flow_controller*/ true,
          connection->version().AllowsLowFlowControlLimits()
              ? 0
              : kMinimumFlowControlSendWindow,
          config_.GetInitialSessionFlowControlWindowToSend(),
          kSessionReceiveWindowLimit,
          perspective() == Perspective::IS_SERVER,
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
      liveness_testing_in_progress_(false) {
  closed_streams_clean_up_alarm_ =
      QuicWrapUnique<QuicAlarm>(connection_->alarm_factory()->CreateAlarm(
          new ClosedStreamsCleanUpDelegate(this)));
  if (perspective() == Perspective::IS_SERVER &&
      connection_->version().handshake_protocol == PROTOCOL_TLS1_3) {
    config_.SetStatelessResetTokenToSend(GetStatelessResetToken());
  }
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
  if (perspective_ == Perspective::IS_CLIENT &&
      config_.HasClientRequestedIndependentOption(kAFFE, perspective_) &&
      version().HasIetfQuicFrames()) {
    connection_->set_can_receive_ack_frequency_frame();
    config_.SetMinAckDelayMs(kDefaultMinAckDelayTimeMs);
  }

  // On the server side, version negotiation has been done by the dispatcher,
  // and the server session is created with the right version.
  if (perspective() == Perspective::IS_SERVER) {
    connection_->OnSuccessfulVersionNegotiation();
  }

  if (GetMutableCryptoStream()->KeyUpdateSupportedLocally()) {
    config_.SetKeyUpdateSupportedLocally();
  }

  if (QuicVersionUsesCryptoFrames(transport_version())) {
    return;
  }

  QUICHE_DCHECK_EQ(QuicUtils::GetCryptoStreamId(transport_version()),
                   GetMutableCryptoStream()->id());
}

QuicSession::~QuicSession() {}

void QuicSession::PendingStreamOnStreamFrame(const QuicStreamFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QuicStreamId stream_id = frame.stream_id;

  PendingStream* pending = GetOrCreatePendingStream(stream_id);

  if (!pending) {
    if (frame.fin) {
      QuicStreamOffset final_byte_offset = frame.offset + frame.data_length;
      OnFinalByteOffsetReceived(stream_id, final_byte_offset);
    }
    return;
  }

  pending->OnStreamFrame(frame);
  if (!connection()->connected()) {
    return;
  }
  if (ProcessPendingStream(pending)) {
    // The pending stream should now be in the scope of normal streams.
    QUICHE_DCHECK(IsClosedStream(stream_id) || IsOpenStream(stream_id))
        << "Stream " << stream_id << " not created";
    pending_stream_map_.erase(stream_id);
    return;
  }
  if (pending->sequencer()->IsClosed()) {
    ClosePendingStream(stream_id);
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

  if (UsesPendingStreams() &&
      QuicUtils::GetStreamType(stream_id, perspective(),
                               IsIncomingStream(stream_id),
                               version()) == READ_UNIDIRECTIONAL &&
      stream_map_.find(stream_id) == stream_map_.end()) {
    PendingStreamOnStreamFrame(frame);
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

  QuicStream* stream = GetOrCreateStream(stream_id);
  if (!stream) {
    // Errors are handled by GetOrCreateStream.
    return;
  }

  stream->OnStopSending(frame.error_code);
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
  // Pending stream is currently read only. We can safely close the stream.
  QUICHE_DCHECK_EQ(
      READ_UNIDIRECTIONAL,
      QuicUtils::GetStreamType(pending->id(), perspective(),
                               /*peer_initiated = */ true, version()));
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

  if (UsesPendingStreams() &&
      QuicUtils::GetStreamType(stream_id, perspective(),
                               IsIncomingStream(stream_id),
                               version()) == READ_UNIDIRECTIONAL &&
      stream_map_.find(stream_id) == stream_map_.end()) {
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
  QUIC_BUG_IF(version().UsesHttp3())
      << "gQUIC GOAWAY received on version " << version();

  transport_goaway_received_ = true;
}

void QuicSession::OnMessageReceived(absl::string_view message) {
  QUIC_DVLOG(1) << ENDPOINT << "Received message, length: " << message.length()
                << ", " << message;
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
  }

  GetMutableCryptoStream()->OnConnectionClosed(frame.quic_error_code, source);

  PerformActionOnActiveStreams([this, frame, source](QuicStream* stream) {
    QuicStreamId id = stream->id();
    stream->OnConnectionClosed(frame.quic_error_code, source);
    auto it = stream_map_.find(id);
    if (it != stream_map_.end()) {
      QUIC_BUG_IF(!it->second->IsZombie())
          << ENDPOINT << "Non-zombie stream " << id
          << " failed to close under OnConnectionClosed";
    }
    return true;
  });

  closed_streams_clean_up_alarm_->Cancel();

  if (visitor_) {
    visitor_->OnConnectionClosed(connection_->connection_id(),
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
  if (is_connectivity_probe && perspective() == Perspective::IS_SERVER) {
    // Server only sends back a connectivity probe after received a
    // connectivity probe from a new peer address.
    if (connection_->send_path_response()) {
      // SendConnectivityProbingResponsePacket() will be deprecated.
      // SendConnectivityProbingPacket() will be used to send both probing
      // request and response as both of them are padded PING.
      connection_->SendConnectivityProbingPacket(nullptr, peer_address);
    } else {
      connection_->SendConnectivityProbingResponsePacket(peer_address);
    }
  }
}

void QuicSession::OnPathDegrading() {}

void QuicSession::OnForwardProgressMadeAfterPathDegrading() {}

bool QuicSession::AllowSelfAddressChange() const {
  return false;
}

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
                  << frame.stream_id;
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
      !write_blocked_streams_.IsStreamBlocked(stream->id())) {
    QUIC_DLOG(ERROR) << ENDPOINT << "stream " << stream->id()
                     << " has buffered " << stream->BufferedDataBytes()
                     << " bytes, and is not flow control blocked, "
                        "but it is not in the write block list.";
    return false;
  }
  return true;
}

void QuicSession::OnCanWrite() {
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
                          ? write_blocked_streams_.NumBlockedSpecialStreams()
                          : write_blocked_streams_.NumBlockedStreams();
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
    if (crypto_stream->HasBufferedCryptoFrames()) {
      // Cannot finish writing buffered crypto frames, connection is write
      // blocked.
      return;
    }
  }
  if (control_frame_manager_.WillingToWrite()) {
    control_frame_manager_.OnCanWrite();
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
    if (!(write_blocked_streams_.HasWriteBlockedSpecialStream() ||
          write_blocked_streams_.HasWriteBlockedDataStreams())) {
      // Writing one stream removed another!? Something's broken.
      QUIC_BUG << "WriteBlockedStream is missing, num_writes: " << num_writes
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
    currently_writing_stream_id_ = write_blocked_streams_.PopFront();
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

bool QuicSession::SendProbingData() {
  if (connection()->sent_packet_manager().MaybeRetransmitOldestPacket(
          PROBING_RETRANSMISSION)) {
    return true;
  }
  return false;
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
    if (GetQuicReloadableFlag(quic_fix_willing_and_able_to_write2)) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_fix_willing_and_able_to_write2);
      if (!IsEncryptionEstablished()) {
        return false;
      }
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
    return write_blocked_streams_.HasWriteBlockedSpecialStream();
  }
  return write_blocked_streams_.HasWriteBlockedSpecialStream() ||
         write_blocked_streams_.HasWriteBlockedDataStreams();
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
  return QuicContainsKey(streams_with_pending_retransmission_,
                         QuicUtils::GetCryptoStreamId(transport_version())) ||
         write_blocked_streams_.IsStreamBlocked(
             QuicUtils::GetCryptoStreamId(transport_version()));
}

void QuicSession::ProcessUdpPacket(const QuicSocketAddress& self_address,
                                   const QuicSocketAddress& peer_address,
                                   const QuicReceivedPacket& packet) {
  connection_->ProcessUdpPacket(self_address, peer_address, packet);
}

QuicConsumedData QuicSession::WritevData(
    QuicStreamId id,
    size_t write_length,
    QuicStreamOffset offset,
    StreamSendingState state,
    TransmissionType type,
    absl::optional<EncryptionLevel> level) {
  QUICHE_DCHECK(connection_->connected())
      << ENDPOINT << "Try to write stream data when connection is closed.";
  QUICHE_DCHECK(!use_write_or_buffer_data_at_level_ || level.has_value());
  if (!IsEncryptionEstablished() &&
      !QuicUtils::IsCryptoStreamId(transport_version(), id)) {
    // Do not let streams write without encryption. The calling stream will end
    // up write blocked until OnCanWrite is next called.
    if (was_zero_rtt_rejected_ && !OneRttKeysAvailable()) {
      QUICHE_DCHECK(version().UsesTls() &&
                    perspective() == Perspective::IS_CLIENT);
      QUIC_BUG_IF(type == NOT_RETRANSMISSION)
          << ENDPOINT << "Try to send new data on stream " << id
          << "before 1-RTT keys are available while 0-RTT is rejected.";
    } else {
      QUIC_BUG << ENDPOINT << "Try to send data of stream " << id
               << " before encryption is established.";
    }
    return QuicConsumedData(0, false);
  }

  SetTransmissionType(type);
  const auto current_level = connection()->encryption_level();
  if (!use_encryption_level_context()) {
    if (level.has_value()) {
      connection()->SetDefaultEncryptionLevel(level.value());
    }
  }
  QuicConnection::ScopedEncryptionLevelContext context(
      use_encryption_level_context() ? connection() : nullptr,
      use_encryption_level_context() ? level.value() : NUM_ENCRYPTION_LEVELS);

  QuicConsumedData data =
      connection_->SendStreamData(id, write_length, offset, state);
  if (type == NOT_RETRANSMISSION) {
    // This is new stream data.
    write_blocked_streams_.UpdateBytesForStream(id, data.bytes_consumed);
  }

  // Restore the encryption level.
  if (!use_encryption_level_context()) {
    // Restore the encryption level.
    if (level.has_value()) {
      connection()->SetDefaultEncryptionLevel(current_level);
    }
  }

  return data;
}

size_t QuicSession::SendCryptoData(EncryptionLevel level,
                                   size_t write_length,
                                   QuicStreamOffset offset,
                                   TransmissionType type) {
  QUICHE_DCHECK(QuicVersionUsesCryptoFrames(transport_version()));
  if (!connection()->framer().HasEncrypterOfEncryptionLevel(level)) {
    const std::string error_details = absl::StrCat(
        "Try to send crypto data with missing keys of encryption level: ",
        EncryptionLevelToString(level));
    QUIC_BUG << ENDPOINT << error_details;
    connection()->CloseConnection(
        QUIC_MISSING_WRITE_KEYS, error_details,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return 0;
  }
  SetTransmissionType(type);
  const auto current_level = connection()->encryption_level();
  if (!use_encryption_level_context()) {
    connection_->SetDefaultEncryptionLevel(level);
  }
  QuicConnection::ScopedEncryptionLevelContext context(
      use_encryption_level_context() ? connection() : nullptr, level);
  const auto bytes_consumed =
      connection_->SendCryptoData(level, write_length, offset);
  if (!use_encryption_level_context()) {
    // Restores encryption level.
    connection_->SetDefaultEncryptionLevel(current_level);
  }
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
  QUICHE_DCHECK(connection()->connected())
      << ENDPOINT << "Try to write control frames when connection is closed.";
  if (connection_->encrypted_control_frames()) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_encrypted_control_frames);
    if (!IsEncryptionEstablished()) {
      QUIC_BUG << ENDPOINT << "Tried to send control frame " << frame
               << " before encryption is established.";
      return false;
    }
  }
  SetTransmissionType(type);
  QuicConnection::ScopedEncryptionLevelContext context(
      use_encryption_level_context() ? connection() : nullptr,
      use_encryption_level_context() ? GetEncryptionLevelToSendApplicationData()
                                     : NUM_ENCRYPTION_LEVELS);
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
  MaybeSendStopSendingFrame(id, error);
  MaybeSendRstStreamFrame(id, error, 0);
}

void QuicSession::MaybeSendRstStreamFrame(QuicStreamId id,
                                          QuicRstStreamErrorCode error,
                                          QuicStreamOffset bytes_written) {
  if (!connection()->connected()) {
    return;
  }
  if (!VersionHasIetfQuicFrames(transport_version()) ||
      QuicUtils::GetStreamType(id, perspective(), IsIncomingStream(id),
                               version()) != READ_UNIDIRECTIONAL) {
    control_frame_manager_.WriteOrBufferRstStream(id, error, bytes_written);
  }

  connection_->OnStreamReset(id, error);
}

void QuicSession::MaybeSendStopSendingFrame(QuicStreamId id,
                                            QuicRstStreamErrorCode error) {
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
  if (GetQuicReloadableFlag(quic_encrypted_goaway)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_encrypted_goaway, 1, 2);
    if (!IsEncryptionEstablished()) {
      QUIC_CODE_COUNT(quic_goaway_before_encryption_established);
      connection_->CloseConnection(
          error_code, reason,
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
  }
  if (transport_goaway_sent_) {
    return;
  }
  transport_goaway_sent_ = true;
  if (GetQuicReloadableFlag(quic_goaway_with_max_stream_id)) {
    QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_goaway_with_max_stream_id, 2, 2);
    control_frame_manager_.WriteOrBufferGoAway(
        error_code,
        QuicUtils::GetMaxClientInitiatedBidirectionalStreamId(
            transport_version()),
        reason);
  } else {
    control_frame_manager_.WriteOrBufferGoAway(
        error_code, stream_id_manager_.largest_peer_created_stream_id(),
        reason);
  }
}

void QuicSession::SendBlocked(QuicStreamId id) {
  control_frame_manager_.WriteOrBufferBlocked(id);
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

void QuicSession::SendMaxStreams(QuicStreamCount stream_count,
                                 bool unidirectional) {
  if (!is_configured_) {
    QUIC_BUG << "Try to send max streams before config negotiated.";
    return;
  }
  control_frame_manager_.WriteOrBufferMaxStreams(stream_count, unidirectional);
}

void QuicSession::InsertLocallyClosedStreamsHighestOffset(
    const QuicStreamId id,
    QuicStreamOffset offset) {
  locally_closed_streams_highest_offset_[id] = offset;
}

void QuicSession::OnStreamClosed(QuicStreamId stream_id) {
  QUIC_DVLOG(1) << ENDPOINT << "Closing stream: " << stream_id;
  StreamMap::iterator it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    QUIC_BUG << ENDPOINT << "Stream is already closed: " << stream_id;
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
    QUIC_BUG_IF(num_draining_streams_ == 0);
    --num_draining_streams_;
    if (!IsIncomingStream(stream_id)) {
      QUIC_BUG_IF(num_outgoing_draining_streams_ == 0);
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

void QuicSession::OnFinalByteOffsetReceived(
    QuicStreamId stream_id,
    QuicStreamOffset final_byte_offset) {
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
  if (GetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2) &&
      version().UsesTls() && is_configured_ &&
      connection_->encryption_level() != ENCRYPTION_FORWARD_SECURE) {
    QUIC_BUG
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

  is_configured_ = true;
  connection()->OnConfigNegotiated();

  // Ask flow controllers to try again since the config could have unblocked us.
  // Or if this session is configured on TLS enabled QUIC versions,
  // attempt to retransmit 0-RTT data if there's any.
  if (connection_->version().AllowsLowFlowControlLimits() ||
      (GetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2) &&
       version().UsesTls())) {
    OnCanWrite();
  }
}

absl::optional<std::string> QuicSession::OnAlpsData(
    const uint8_t* /*alps_data*/,
    size_t /*alps_length*/) {
  return absl::nullopt;
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
    EncryptionLevel level,
    std::unique_ptr<QuicDecrypter> decrypter,
    bool set_alternative_decrypter,
    bool latch_once_used) {
  if (connection_->version().handshake_protocol == PROTOCOL_TLS1_3 &&
      !connection()->framer().HasEncrypterOfEncryptionLevel(
          QuicUtils::GetEncryptionLevel(
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
    EncryptionLevel level,
    std::unique_ptr<QuicEncrypter> encrypter) {
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
  QUIC_BUG_IF(IsEncryptionEstablished() &&
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
        // Given any streams blocked by encryption a chance to write.
        OnCanWrite();
      }
      break;
    case ENCRYPTION_HANDSHAKE:
      break;
    case ENCRYPTION_FORWARD_SECURE:
      QUIC_BUG_IF(!config_.negotiated())
          << ENDPOINT << "Handshake confirmed without parameter negotiation.";
      connection()->mutable_stats().handshake_completion_time =
          connection()->clock()->ApproximateNow();
      break;
    default:
      QUIC_BUG << "Unknown encryption level: " << level;
  }
}

void QuicSession::OnTlsHandshakeComplete() {
  QUICHE_DCHECK_EQ(PROTOCOL_TLS1_3, connection_->version().handshake_protocol);
  QUIC_BUG_IF(!GetCryptoStream()->crypto_negotiated_params().cipher_suite)
      << ENDPOINT << "Handshake completes without cipher suite negotiation.";
  QUIC_BUG_IF(!config_.negotiated())
      << ENDPOINT << "Handshake completes without parameter negotiation.";
  connection()->mutable_stats().handshake_completion_time =
      connection()->clock()->ApproximateNow();
  if (connection()->version().UsesTls() &&
      perspective_ == Perspective::IS_SERVER) {
    // Server sends HANDSHAKE_DONE to signal confirmation of the handshake
    // to the client.
    control_frame_manager_.WriteOrBufferHandshakeDone();
    if (GetQuicReloadableFlag(quic_enable_token_based_address_validation) &&
        connection()->version().HasIetfQuicFrames()) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_enable_token_based_address_validation,
                                   1, 2);
      MaybeSendAddressToken();
    }
  }
}

void QuicSession::MaybeSendAddressToken() {
  QUICHE_DCHECK(perspective_ == Perspective::IS_SERVER &&
                connection()->version().HasIetfQuicFrames());
  std::string address_token = GetCryptoStream()->GetAddressToken();
  if (address_token.empty()) {
    return;
  }
  const size_t buf_len = address_token.length() + 1;
  auto buffer = std::make_unique<char[]>(buf_len);
  QuicDataWriter writer(buf_len, buffer.get());
  // Add prefix 0 for token sent in NEW_TOKEN frame.
  writer.WriteUInt8(0);
  writer.WriteBytes(address_token.data(), address_token.length());
  control_frame_manager_.WriteOrBufferNewToken(
      absl::string_view(buffer.get(), buf_len));
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
      QUIC_BUG << ENDPOINT << "Discarding 1-RTT keys is not allowed";
      break;
    default:
      QUIC_BUG << ENDPOINT
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
    QUIC_BUG << "1-RTT keys already available when 0-RTT is rejected.";
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
    const TransportParameters& params,
    bool is_resumption,
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

void QuicSession::OnCryptoHandshakeMessageSent(
    const CryptoHandshakeMessage& /*message*/) {}

void QuicSession::OnCryptoHandshakeMessageReceived(
    const CryptoHandshakeMessage& /*message*/) {}

void QuicSession::RegisterStreamPriority(
    QuicStreamId id,
    bool is_static,
    const spdy::SpdyStreamPrecedence& precedence) {
  write_blocked_streams()->RegisterStream(id, is_static, precedence);
}

void QuicSession::UnregisterStreamPriority(QuicStreamId id, bool is_static) {
  write_blocked_streams()->UnregisterStream(id, is_static);
}

void QuicSession::UpdateStreamPriority(
    QuicStreamId id,
    const spdy::SpdyStreamPrecedence& new_precedence) {
  write_blocked_streams()->UpdateStreamPriority(id, new_precedence);
}

QuicConfig* QuicSession::config() {
  return &config_;
}

void QuicSession::ActivateStream(std::unique_ptr<QuicStream> stream) {
  QuicStreamId stream_id = stream->id();
  bool is_static = stream->is_static();
  QUIC_DVLOG(1) << ENDPOINT << "num_streams: " << stream_map_.size()
                << ". activating stream " << stream_id;
  QUICHE_DCHECK(!QuicContainsKey(stream_map_, stream_id));
  stream_map_[stream_id] = std::move(stream);
  if (is_static) {
    ++num_static_streams_;
    return;
  }
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Do not inform stream ID manager of static streams.
    stream_id_manager_.ActivateStream(
        /*is_incoming=*/IsIncomingStream(stream_id));
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
    return false;
  }
  if (!VersionHasIetfQuicFrames(transport_version())) {
    if (!stream_id_manager_.CanOpenNextOutgoingStream()) {
      return false;
    }
  } else {
    if (!ietf_streamid_manager_.CanOpenNextOutgoingBidirectionalStream()) {
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
  QUICHE_DCHECK(!QuicContainsKey(pending_stream_map_, stream_id));
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
  QUICHE_DCHECK(QuicContainsKey(stream_map_, stream_id));
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
    OnCanCreateNewOutgoingStream(unidirectional);
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

bool QuicSession::MaybeSetStreamPriority(
    QuicStreamId stream_id,
    const spdy::SpdyStreamPrecedence& precedence) {
  auto active_stream = stream_map_.find(stream_id);
  if (active_stream != stream_map_.end()) {
    active_stream->second->SetPriority(precedence);
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
  if (QuicContainsKey(pending_stream_map_, id) ||
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
    QUIC_BUG << "Marking unknown stream " << id << " blocked.";
    QUIC_LOG_FIRST_N(ERROR, 2) << QuicStackTrace();
  }

  QUIC_DVLOG(1) << ENDPOINT << "Adding stream " << id
                << " to write-blocked list";

  write_blocked_streams_.AddStream(id);
}

bool QuicSession::HasDataToWrite() const {
  return write_blocked_streams_.HasWriteBlockedSpecialStream() ||
         write_blocked_streams_.HasWriteBlockedDataStreams() ||
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
    return control_frame_manager_.OnControlFrameAcked(frame);
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
    QUIC_BUG << "Stream: " << frame.stream_id << " is closed when " << frame
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
      !QuicContainsKey(streams_with_pending_retransmission_,
                       frame.stream_frame.stream_id)) {
    streams_with_pending_retransmission_.insert(
        std::make_pair(frame.stream_frame.stream_id, true));
  }
}

void QuicSession::RetransmitFrames(const QuicFrames& frames,
                                   TransmissionType type) {
  QuicConnection::ScopedPacketFlusher retransmission_flusher(connection_);
  for (const QuicFrame& frame : frames) {
    if (frame.type == MESSAGE_FRAME) {
      // Do not retransmit MESSAGE frames.
      continue;
    }
    if (frame.type == CRYPTO_FRAME) {
      GetMutableCryptoStream()->RetransmitData(frame.crypto_frame, type);
      continue;
    }
    if (frame.type != STREAM_FRAME) {
      if (!control_frame_manager_.RetransmitControlFrame(frame, type)) {
        break;
      }
      continue;
    }
    QuicStream* stream = GetStream(frame.stream_frame.stream_id);
    if (stream != nullptr &&
        !stream->RetransmitStreamData(frame.stream_frame.offset,
                                      frame.stream_frame.data_length,
                                      frame.stream_frame.fin, type)) {
      break;
    }
  }
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

WriteStreamDataResult QuicSession::WriteStreamData(QuicStreamId id,
                                                   QuicStreamOffset offset,
                                                   QuicByteCount data_length,
                                                   QuicDataWriter* writer) {
  QuicStream* stream = GetStream(id);
  if (stream == nullptr) {
    // This causes the connection to be closed because of failed to serialize
    // packet.
    QUIC_BUG << "Stream " << id << " does not exist when trying to write data."
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

QuicUint128 QuicSession::GetStatelessResetToken() const {
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
  QuicCryptoStream* crypto_stream = GetMutableCryptoStream();
  if (uses_crypto_frames && crypto_stream->HasPendingCryptoRetransmission()) {
    crypto_stream->WritePendingCryptoRetransmission();
  }
  // Retransmit crypto data in stream 1 frames (version < 47).
  if (!uses_crypto_frames &&
      QuicContainsKey(streams_with_pending_retransmission_,
                      QuicUtils::GetCryptoStreamId(transport_version()))) {
    // Retransmit crypto data first.
    QuicStream* crypto_stream =
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
      QUIC_BUG << "Try to retransmit data of a closed stream";
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

MessageResult QuicSession::SendMessage(QuicMemSliceSpan message) {
  return SendMessage(message, /*flush=*/false);
}

MessageResult QuicSession::SendMessage(QuicMemSliceSpan message, bool flush) {
  QUICHE_DCHECK(connection_->connected())
      << ENDPOINT << "Try to write messages when connection is closed.";
  if (!IsEncryptionEstablished()) {
    return {MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED, 0};
  }
  QuicConnection::ScopedEncryptionLevelContext context(
      use_encryption_level_context() ? connection() : nullptr,
      use_encryption_level_context() ? GetEncryptionLevelToSendApplicationData()
                                     : NUM_ENCRYPTION_LEVELS);
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

void QuicSession::CleanUpClosedStreams() {
  closed_streams_.clear();
}

QuicPacketLength QuicSession::GetCurrentLargestMessagePayload() const {
  return connection_->GetCurrentLargestMessagePayload();
}

QuicPacketLength QuicSession::GetGuaranteedLargestMessagePayload() const {
  return connection_->GetGuaranteedLargestMessagePayload();
}

void QuicSession::SendStopSending(QuicRstStreamErrorCode code,
                                  QuicStreamId stream_id) {
  control_frame_manager_.WriteOrBufferStopSending(code, stream_id);
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
    std::function<bool(QuicStream*)> action) {
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
    std::function<bool(QuicStream*)> action) const {
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

void QuicSession::ValidatePath(
    std::unique_ptr<QuicPathValidationContext> context,
    std::unique_ptr<QuicPathValidator::ResultDelegate> result_delegate) {
  connection_->ValidatePath(std::move(context), std::move(result_delegate));
}

bool QuicSession::HasPendingPathValidation() const {
  return connection_->HasPendingPathValidation();
}

void QuicSession::MigratePath(const QuicSocketAddress& self_address,
                              const QuicSocketAddress& peer_address,
                              QuicPacketWriter* writer,
                              bool owns_writer) {
  connection_->MigratePath(self_address, peer_address, writer, owns_writer);
}

bool QuicSession::ValidateToken(absl::string_view token) const {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_SERVER);
  if (token.empty() || token[0] != 0) {
    // Validate the prefix for token received in NEW_TOKEN frame.
    return false;
  }
  return GetCryptoStream()->ValidateAddressToken(
      absl::string_view(token.data() + 1, token.length() - 1));
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
