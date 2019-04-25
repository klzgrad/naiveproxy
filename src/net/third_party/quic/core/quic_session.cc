// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_session.h"

#include <cstdint>
#include <utility>

#include "net/third_party/quic/core/quic_connection.h"
#include "net/third_party/quic/core/quic_flow_controller.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_map_util.h"
#include "net/third_party/quic/platform/api/quic_stack_trace.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"

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

QuicSession::QuicSession(QuicConnection* connection,
                         Visitor* owner,
                         const QuicConfig& config,
                         const ParsedQuicVersionVector& supported_versions)
    : connection_(connection),
      visitor_(owner),
      write_blocked_streams_(),
      config_(config),
      stream_id_manager_(this,
                         kDefaultMaxStreamsPerConnection,
                         config_.GetMaxIncomingDynamicStreamsToSend()),
      v99_streamid_manager_(this,
                            kDefaultMaxStreamsPerConnection,
                            config_.GetMaxIncomingDynamicStreamsToSend()),
      num_dynamic_incoming_streams_(0),
      num_draining_incoming_streams_(0),
      num_locally_closed_incoming_streams_highest_offset_(0),
      error_(QUIC_NO_ERROR),
      flow_controller_(
          this,
          QuicUtils::GetInvalidStreamId(connection->transport_version()),
          /*is_connection_flow_controller*/ true,
          kMinimumFlowControlSendWindow,
          config_.GetInitialSessionFlowControlWindowToSend(),
          kSessionReceiveWindowLimit,
          perspective() == Perspective::IS_SERVER,
          nullptr),
      currently_writing_stream_id_(0),
      largest_static_stream_id_(0),
      is_handshake_confirmed_(false),
      goaway_sent_(false),
      goaway_received_(false),
      control_frame_manager_(this),
      last_message_id_(0),
      closed_streams_clean_up_alarm_(nullptr),
      supported_versions_(supported_versions) {
  closed_streams_clean_up_alarm_ =
      QuicWrapUnique<QuicAlarm>(connection_->alarm_factory()->CreateAlarm(
          new ClosedStreamsCleanUpDelegate(this)));
}

void QuicSession::Initialize() {
  connection_->set_visitor(this);
  connection_->SetSessionNotifier(this);
  connection_->SetDataProducer(this);
  connection_->SetFromConfig(config_);

  DCHECK_EQ(QuicUtils::GetCryptoStreamId(connection_->transport_version()),
            GetMutableCryptoStream()->id());
  RegisterStaticStream(
      QuicUtils::GetCryptoStreamId(connection_->transport_version()),
      GetMutableCryptoStream());
}

QuicSession::~QuicSession() {
  QUIC_LOG_IF(WARNING, !zombie_streams_.empty()) << "Still have zombie streams";
}

void QuicSession::RegisterStaticStream(QuicStreamId id, QuicStream* stream) {
  static_stream_map_[id] = stream;

  QUIC_BUG_IF(id >
              largest_static_stream_id_ +
                  QuicUtils::StreamIdDelta(connection_->transport_version()))
      << ENDPOINT << "Static stream registered out of order: " << id
      << " vs: " << largest_static_stream_id_;
  largest_static_stream_id_ = std::max(id, largest_static_stream_id_);

  if (connection_->transport_version() == QUIC_VERSION_99) {
    v99_streamid_manager_.RegisterStaticStream(id);
  }
}

void QuicSession::OnStreamFrame(const QuicStreamFrame& frame) {
  // TODO(rch) deal with the error case of stream id 0.
  QuicStreamId stream_id = frame.stream_id;
  if (stream_id ==
      QuicUtils::GetInvalidStreamId(connection()->transport_version())) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Recevied data for an invalid stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (frame.fin && QuicContainsKey(static_stream_map_, stream_id)) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Attempt to close a static stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  StreamHandler handler = GetOrCreateStreamImpl(stream_id, frame.offset != 0);
  if (handler.is_pending) {
    handler.pending->OnStreamFrame(frame);
    return;
  }

  if (!handler.stream) {
    // The stream no longer exists, but we may still be interested in the
    // final stream byte offset sent by the peer. A frame with a FIN can give
    // us this offset.
    if (frame.fin) {
      QuicStreamOffset final_byte_offset = frame.offset + frame.data_length;
      OnFinalByteOffsetReceived(stream_id, final_byte_offset);
    }
    return;
  }
  handler.stream->OnStreamFrame(frame);
}

void QuicSession::OnCryptoFrame(const QuicCryptoFrame& frame) {
  GetMutableCryptoStream()->OnCryptoFrame(frame);
}

bool QuicSession::OnStopSendingFrame(const QuicStopSendingFrame& frame) {
  // We are not version 99. In theory, if not in version 99 then the framer
  // could not call OnStopSending... This is just a check that is good when
  // both a new protocol and a new implementation of that protocol are both
  // being developed.
  DCHECK_EQ(QUIC_VERSION_99, connection_->transport_version());

  QuicStreamId stream_id = frame.stream_id;
  // If Stream ID is invalid then close the connection.
  if (stream_id ==
      QuicUtils::GetInvalidStreamId(connection()->transport_version())) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING with invalid stream_id: "
                  << stream_id << " Closing connection";
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received STOP_SENDING for an invalid stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  // Ignore STOP_SENDING for static streams.
  // TODO(fkastenholz): IETF Quic does not have static streams and does not
  // make exceptions for them with respect to processing things like
  // STOP_SENDING.
  if (QuicContainsKey(static_stream_map_, stream_id)) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING for a static stream, id: "
                  << stream_id << " Closing connection";
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received STOP_SENDING for a static stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (visitor_) {
    visitor_->OnStopSendingReceived(frame);
  }

  // If stream is closed, ignore the frame
  if (IsClosedStream(stream_id)) {
    QUIC_DVLOG(1)
        << ENDPOINT
        << "Received STOP_SENDING for closed or non-existent stream, id: "
        << stream_id << " Ignoring.";
    return true;  // Continue processing the packet.
  }
  // If stream is non-existent, close the connection
  DynamicStreamMap::iterator it = dynamic_stream_map_.find(stream_id);
  if (it == dynamic_stream_map_.end()) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING for non-existent stream, id: "
                  << stream_id << " Closing connection";
    connection()->CloseConnection(
        IETF_QUIC_PROTOCOL_VIOLATION,
        "Received STOP_SENDING for a non-existent stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  // Get the QuicStream for this stream. Ignore the STOP_SENDING
  // if the QuicStream pointer is NULL
  // QUESTION: IS THIS THE RIGHT THING TO DO? (that is, this would happen IFF
  // there was an entry in the map, but the pointer is null. sounds more like a
  // deep programming error rather than a simple protocol problem).
  QuicStream* stream = it->second.get();
  if (stream == nullptr) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received STOP_SENDING for NULL QuicStream, stream_id: "
                  << stream_id << ". Ignoring.";
    return true;
  }
  stream->OnStopSending(frame.application_error_code);

  stream->set_stream_error(
      static_cast<QuicRstStreamErrorCode>(frame.application_error_code));
  SendRstStreamInner(
      stream->id(),
      static_cast<quic::QuicRstStreamErrorCode>(frame.application_error_code),
      stream->stream_bytes_written(),
      /*close_write_side_only=*/true);

  return true;
}

void QuicSession::OnRstStream(const QuicRstStreamFrame& frame) {
  QuicStreamId stream_id = frame.stream_id;
  if (stream_id ==
      QuicUtils::GetInvalidStreamId(connection()->transport_version())) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Recevied data for an invalid stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (QuicContainsKey(static_stream_map_, stream_id)) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Attempt to reset a static stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (visitor_) {
    visitor_->OnRstStreamReceived(frame);
  }

  // may_buffer is true here to allow subclasses to buffer streams until the
  // first byte of payload arrives which would allow sessions to delay
  // creation of the stream until the type is known.
  StreamHandler handler = GetOrCreateStreamImpl(stream_id, /*may_buffer=*/true);
  if (handler.is_pending) {
    handler.pending->OnRstStreamFrame(frame);
    ClosePendingStream(stream_id);
    return;
  }
  if (!handler.stream) {
    HandleRstOnValidNonexistentStream(frame);
    return;  // Errors are handled by GetOrCreateStream.
  }
  handler.stream->OnStreamReset(frame);
}

void QuicSession::OnGoAway(const QuicGoAwayFrame& frame) {
  goaway_received_ = true;
}

void QuicSession::OnMessageReceived(QuicStringPiece message) {
  QUIC_DVLOG(1) << ENDPOINT << "Received message, length: " << message.length()
                << ", " << message;
}

void QuicSession::OnConnectionClosed(QuicErrorCode error,
                                     const QuicString& error_details,
                                     ConnectionCloseSource source) {
  DCHECK(!connection_->connected());
  if (error_ == QUIC_NO_ERROR) {
    error_ = error;
  }

  while (!dynamic_stream_map_.empty()) {
    DynamicStreamMap::iterator it = dynamic_stream_map_.begin();
    QuicStreamId id = it->first;
    it->second->OnConnectionClosed(error, source);
    // The stream should call CloseStream as part of OnConnectionClosed.
    if (dynamic_stream_map_.find(id) != dynamic_stream_map_.end()) {
      QUIC_BUG << ENDPOINT << "Stream failed to close under OnConnectionClosed";
      CloseStream(id);
    }
  }

  // Cleanup zombie stream map on connection close.
  while (!zombie_streams_.empty()) {
    ZombieStreamMap::iterator it = zombie_streams_.begin();
    closed_streams_.push_back(std::move(it->second));
    zombie_streams_.erase(it);
  }

  closed_streams_clean_up_alarm_->Cancel();

  if (visitor_) {
    visitor_->OnConnectionClosed(connection_->connection_id(), error,
                                 error_details, source);
  }
}

void QuicSession::OnWriteBlocked() {
  if (GetQuicReloadableFlag(
          quic_connection_do_not_add_to_write_blocked_list_if_disconnected) &&
      !connection_->connected()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(
        quic_connection_do_not_add_to_write_blocked_list_if_disconnected, 1, 2);
    return;
  }
  if (visitor_) {
    visitor_->OnWriteBlocked(connection_);
  }
}

void QuicSession::OnSuccessfulVersionNegotiation(
    const ParsedQuicVersion& version) {
  GetMutableCryptoStream()->OnSuccessfulVersionNegotiation(version);
}

void QuicSession::OnConnectivityProbeReceived(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address) {
  if (perspective() == Perspective::IS_SERVER) {
    // Server only sends back a connectivity probe after received a
    // connectivity probe from a new peer address.
    connection_->SendConnectivityProbingResponsePacket(peer_address);
  }
}

void QuicSession::OnPathDegrading() {}

bool QuicSession::AllowSelfAddressChange() const {
  return false;
}

void QuicSession::OnForwardProgressConfirmed() {}

void QuicSession::OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) {
  // Stream may be closed by the time we receive a WINDOW_UPDATE, so we can't
  // assume that it still exists.
  QuicStreamId stream_id = frame.stream_id;
  if (stream_id ==
      QuicUtils::GetInvalidStreamId(connection_->transport_version())) {
    // This is a window update that applies to the connection, rather than an
    // individual stream.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received connection level flow control window "
                       "update with byte offset: "
                    << frame.byte_offset;
    flow_controller_.UpdateSendWindowOffset(frame.byte_offset);
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
    QUIC_DVLOG(1) << "Suspected busy loop on stream id " << stream->id()
                  << " stream_bytes_written " << stream->stream_bytes_written()
                  << " fin " << stream->fin_sent() << " count "
                  << stream->busy_counter();
    // Wait a few iterations before firing, the exact count is
    // arbitrary, more than a few to cover a few test-only false
    // positives.
    if (stream->busy_counter() > 20) {
      QUIC_LOG(ERROR) << "Detected busy loop on stream id " << stream->id()
                      << " stream_bytes_written "
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
      !stream->flow_controller()->IsBlocked() &&
      !write_blocked_streams_.IsStreamBlocked(stream->id())) {
    QUIC_DLOG(ERROR) << "stream " << stream->id() << " has buffered "
                     << stream->BufferedDataBytes()
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
  if (session_decides_what_to_write()) {
    SetTransmissionType(NOT_RETRANSMISSION);
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
  if (num_writes == 0 && !control_frame_manager_.WillingToWrite()) {
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(
      connection_, QuicConnection::SEND_ACK_IF_QUEUED);
  if (control_frame_manager_.WillingToWrite()) {
    control_frame_manager_.OnCanWrite();
  }
  for (size_t i = 0; i < num_writes; ++i) {
    if (!(write_blocked_streams_.HasWriteBlockedSpecialStream() ||
          write_blocked_streams_.HasWriteBlockedDataStreams())) {
      // Writing one stream removed another!? Something's broken.
      QUIC_BUG << "WriteBlockedStream is missing";
      connection_->CloseConnection(QUIC_INTERNAL_ERROR,
                                   "WriteBlockedStream is missing",
                                   ConnectionCloseBehavior::SILENT_CLOSE);
      return;
    }
    if (!connection_->CanWriteStreamData()) {
      return;
    }
    currently_writing_stream_id_ = write_blocked_streams_.PopFront();
    QuicStream* stream = GetOrCreateStream(currently_writing_stream_id_);
    if (stream != nullptr && !stream->flow_controller()->IsBlocked()) {
      // If the stream can't write all bytes it'll re-add itself to the blocked
      // list.
      uint64_t previous_bytes_written = stream->stream_bytes_written();
      bool previous_fin_sent = stream->fin_sent();
      QUIC_DVLOG(1) << "stream " << stream->id() << " bytes_written "
                    << previous_bytes_written << " fin " << previous_fin_sent;
      stream->OnCanWrite();
      DCHECK(CheckStreamWriteBlocked(stream));
      DCHECK(CheckStreamNotBusyLooping(stream, previous_bytes_written,
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
  return control_frame_manager_.WillingToWrite() ||
         !streams_with_pending_retransmission_.empty() ||
         write_blocked_streams_.HasWriteBlockedSpecialStream() ||
         (!flow_controller_.IsBlocked() &&
          write_blocked_streams_.HasWriteBlockedDataStreams());
}

bool QuicSession::HasPendingHandshake() const {
  return QuicContainsKey(
             streams_with_pending_retransmission_,
             QuicUtils::GetCryptoStreamId(connection_->transport_version())) ||
         write_blocked_streams_.IsStreamBlocked(
             QuicUtils::GetCryptoStreamId(connection_->transport_version()));
}

uint64_t QuicSession::GetNumOpenDynamicStreams() const {
  return dynamic_stream_map_.size() - draining_streams_.size() +
         locally_closed_streams_highest_offset_.size();
}

void QuicSession::ProcessUdpPacket(const QuicSocketAddress& self_address,
                                   const QuicSocketAddress& peer_address,
                                   const QuicReceivedPacket& packet) {
  connection_->ProcessUdpPacket(self_address, peer_address, packet);
}

QuicConsumedData QuicSession::WritevData(QuicStream* stream,
                                         QuicStreamId id,
                                         size_t write_length,
                                         QuicStreamOffset offset,
                                         StreamSendingState state) {
  // This check is an attempt to deal with potential memory corruption
  // in which |id| ends up set to 1 (the crypto stream id). If this happen
  // it might end up resulting in unencrypted stream data being sent.
  // While this is impossible to avoid given sufficient corruption, this
  // seems like a reasonable mitigation.
  if (id == QuicUtils::GetCryptoStreamId(connection_->transport_version()) &&
      stream != GetMutableCryptoStream()) {
    QUIC_BUG << "Stream id mismatch";
    connection_->CloseConnection(
        QUIC_INTERNAL_ERROR,
        "Non-crypto stream attempted to write data as crypto stream.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return QuicConsumedData(0, false);
  }
  if (!IsEncryptionEstablished() &&
      id != QuicUtils::GetCryptoStreamId(connection_->transport_version())) {
    // Do not let streams write without encryption. The calling stream will end
    // up write blocked until OnCanWrite is next called.
    return QuicConsumedData(0, false);
  }

  QuicConsumedData data =
      connection_->SendStreamData(id, write_length, offset, state);
  if (offset >= stream->stream_bytes_written()) {
    // This is new stream data.
    write_blocked_streams_.UpdateBytesForStream(id, data.bytes_consumed);
  }
  return data;
}

bool QuicSession::WriteControlFrame(const QuicFrame& frame) {
  return connection_->SendControlFrame(frame);
}

void QuicSession::SendRstStream(QuicStreamId id,
                                QuicRstStreamErrorCode error,
                                QuicStreamOffset bytes_written) {
  SendRstStreamInner(id, error, bytes_written, /*close_write_side_only=*/false);
}

void QuicSession::SendRstStreamInner(QuicStreamId id,
                                     QuicRstStreamErrorCode error,
                                     QuicStreamOffset bytes_written,
                                     bool close_write_side_only) {
  if (connection()->connected()) {
    // Only send if still connected.
    if (close_write_side_only) {
      DCHECK_EQ(QUIC_VERSION_99, connection_->transport_version());
      // Send a RST_STREAM frame.
      control_frame_manager_.WriteOrBufferRstStream(id, error, bytes_written);
    } else {
      // Send a RST_STREAM frame plus, if version 99, an IETF
      // QUIC STOP_SENDING frame. Both sre sent to emulate
      // the two-way close that Google QUIC's RST_STREAM does.
      if (connection_->transport_version() == QUIC_VERSION_99) {
        QuicConnection::ScopedPacketFlusher flusher(
            connection(), QuicConnection::SEND_ACK_IF_QUEUED);
        control_frame_manager_.WriteOrBufferRstStream(id, error, bytes_written);
        control_frame_manager_.WriteOrBufferStopSending(error, id);
      } else {
        control_frame_manager_.WriteOrBufferRstStream(id, error, bytes_written);
      }
    }
    connection_->OnStreamReset(id, error);
  }
  if (error != QUIC_STREAM_NO_ERROR && QuicContainsKey(zombie_streams_, id)) {
    OnStreamDoneWaitingForAcks(id);
    return;
  }

  if (!close_write_side_only) {
    CloseStreamInner(id, true);
    return;
  }
  DCHECK_EQ(QUIC_VERSION_99, connection_->transport_version());

  DynamicStreamMap::iterator it = dynamic_stream_map_.find(id);
  if (it != dynamic_stream_map_.end()) {
    QuicStream* stream = it->second.get();
    if (stream) {
      stream->set_rst_sent(true);
      stream->CloseWriteSide();
    }
  }
}

void QuicSession::SendGoAway(QuicErrorCode error_code,
                             const QuicString& reason) {
  // GOAWAY frame is not supported in v99.
  DCHECK_NE(QUIC_VERSION_99, connection_->transport_version());
  if (goaway_sent_) {
    return;
  }
  goaway_sent_ = true;
  control_frame_manager_.WriteOrBufferGoAway(
      error_code, stream_id_manager_.largest_peer_created_stream_id(), reason);
}

void QuicSession::SendBlocked(QuicStreamId id) {
  control_frame_manager_.WriteOrBufferBlocked(id);
}

void QuicSession::SendWindowUpdate(QuicStreamId id,
                                   QuicStreamOffset byte_offset) {
  control_frame_manager_.WriteOrBufferWindowUpdate(id, byte_offset);
}

void QuicSession::SendMaxStreamId(QuicStreamId max_allowed_incoming_id) {
  control_frame_manager_.WriteOrBufferMaxStreamId(max_allowed_incoming_id);
}

void QuicSession::SendStreamIdBlocked(QuicStreamId max_allowed_outgoing_id) {
  control_frame_manager_.WriteOrBufferStreamIdBlocked(max_allowed_outgoing_id);
}

void QuicSession::CloseStream(QuicStreamId stream_id) {
  CloseStreamInner(stream_id, false);
}

void QuicSession::InsertLocallyClosedStreamsHighestOffset(
    const QuicStreamId id,
    QuicStreamOffset offset) {
  locally_closed_streams_highest_offset_[id] = offset;
  if (IsIncomingStream(id)) {
    ++num_locally_closed_incoming_streams_highest_offset_;
  }
}

void QuicSession::CloseStreamInner(QuicStreamId stream_id, bool locally_reset) {
  QUIC_DVLOG(1) << ENDPOINT << "Closing stream " << stream_id;

  DynamicStreamMap::iterator it = dynamic_stream_map_.find(stream_id);
  if (it == dynamic_stream_map_.end()) {
    // When CloseStreamInner has been called recursively (via
    // QuicStream::OnClose), the stream will already have been deleted
    // from stream_map_, so return immediately.
    QUIC_DVLOG(1) << ENDPOINT << "Stream is already closed: " << stream_id;
    return;
  }
  QuicStream* stream = it->second.get();

  // Tell the stream that a RST has been sent.
  if (locally_reset) {
    stream->set_rst_sent(true);
  }

  if (stream->IsWaitingForAcks()) {
    zombie_streams_[stream->id()] = std::move(it->second);
  } else {
    closed_streams_.push_back(std::move(it->second));
    // Do not retransmit data of a closed stream.
    streams_with_pending_retransmission_.erase(stream_id);
    if (!closed_streams_clean_up_alarm_->IsSet()) {
      closed_streams_clean_up_alarm_->Set(
          connection_->clock()->ApproximateNow());
    }
  }

  // If we haven't received a FIN or RST for this stream, we need to keep track
  // of the how many bytes the stream's flow controller believes it has
  // received, for accurate connection level flow control accounting.
  const bool had_fin_or_rst = stream->HasFinalReceivedByteOffset();
  if (!had_fin_or_rst) {
    InsertLocallyClosedStreamsHighestOffset(
        stream_id, stream->flow_controller()->highest_received_byte_offset());
  }
  dynamic_stream_map_.erase(it);
  if (IsIncomingStream(stream_id)) {
    --num_dynamic_incoming_streams_;
  }

  const bool stream_was_draining =
      draining_streams_.find(stream_id) != draining_streams_.end();
  if (stream_was_draining) {
    if (IsIncomingStream(stream_id)) {
      --num_draining_incoming_streams_;
    }
    draining_streams_.erase(stream_id);
  } else if (connection_->transport_version() == QUIC_VERSION_99) {
    // Stream was not draining, but we did have a fin or rst, so we can now
    // free the stream ID if version 99.
    if (had_fin_or_rst) {
      v99_streamid_manager_.OnStreamClosed(stream_id);
    }
  }

  stream->OnClose();

  if (!stream_was_draining && !IsIncomingStream(stream_id) && had_fin_or_rst &&
      connection_->transport_version() != QUIC_VERSION_99) {
    // Streams that first became draining already called OnCanCreate...
    // This covers the case where the stream went directly to being closed.
    OnCanCreateNewOutgoingStream();
  }
}

void QuicSession::ClosePendingStream(QuicStreamId stream_id) {
  QUIC_DVLOG(1) << ENDPOINT << "Closing stream " << stream_id;

  if (pending_stream_map_.find(stream_id) == pending_stream_map_.end()) {
    QUIC_BUG << ENDPOINT << "Stream is already closed: " << stream_id;
    return;
  }

  SendRstStream(stream_id, QUIC_RST_ACKNOWLEDGEMENT, 0);

  // The pending stream may have been deleted and removed during SendRstStream.
  // Remove the stream from pending stream map iff it is still in the map.
  if (pending_stream_map_.find(stream_id) != pending_stream_map_.end()) {
    pending_stream_map_.erase(stream_id);
  }

  --num_dynamic_incoming_streams_;

  if (connection_->transport_version() == QUIC_VERSION_99) {
    v99_streamid_manager_.OnStreamClosed(stream_id);
  }

  OnCanCreateNewOutgoingStream();
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
  if (IsIncomingStream(stream_id)) {
    --num_locally_closed_incoming_streams_highest_offset_;
    if (connection_->transport_version() == QUIC_VERSION_99) {
      v99_streamid_manager_.OnStreamClosed(stream_id);
    }
  } else if (connection_->transport_version() != QUIC_VERSION_99) {
    OnCanCreateNewOutgoingStream();
  }
}

bool QuicSession::IsEncryptionEstablished() const {
  // Once the handshake is confirmed, it never becomes un-confirmed.
  if (is_handshake_confirmed_) {
    return true;
  }
  return GetCryptoStream()->encryption_established();
}

bool QuicSession::IsCryptoHandshakeConfirmed() const {
  return GetCryptoStream()->handshake_confirmed();
}

void QuicSession::OnConfigNegotiated() {
  connection_->SetFromConfig(config_);

  uint32_t max_streams = 0;
  if (config_.HasReceivedMaxIncomingDynamicStreams()) {
    max_streams = config_.ReceivedMaxIncomingDynamicStreams();
  }
  QUIC_DVLOG(1) << "Setting max_open_outgoing_streams_ to " << max_streams;
  if (connection_->transport_version() == QUIC_VERSION_99) {
    v99_streamid_manager_.SetMaxOpenOutgoingStreams(max_streams);
  } else {
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

  // A small number of additional incoming streams beyond the limit should be
  // allowed. This helps avoid early connection termination when FIN/RSTs for
  // old streams are lost or arrive out of order.
  // Use a minimum number of additional streams, or a percentage increase,
  // whichever is larger.
  uint32_t max_incoming_streams_to_send =
      config_.GetMaxIncomingDynamicStreamsToSend();
  if (connection_->transport_version() == QUIC_VERSION_99) {
    v99_streamid_manager_.SetMaxOpenIncomingStreams(
        max_incoming_streams_to_send);
  } else {
    uint32_t max_incoming_streams =
        std::max(max_incoming_streams_to_send + kMaxStreamsMinimumIncrement,
                 static_cast<uint32_t>(max_incoming_streams_to_send *
                                       kMaxStreamsMultiplier));
    stream_id_manager_.set_max_open_incoming_streams(max_incoming_streams);
  }

  if (config_.HasReceivedInitialStreamFlowControlWindowBytes()) {
    // Streams which were created before the SHLO was received (0-RTT
    // requests) are now informed of the peer's initial flow control window.
    OnNewStreamFlowControlWindow(
        config_.ReceivedInitialStreamFlowControlWindowBytes());
  }
  if (config_.HasReceivedInitialSessionFlowControlWindowBytes()) {
    OnNewSessionFlowControlWindow(
        config_.ReceivedInitialSessionFlowControlWindowBytes());
  }
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
  for (auto const& kv : static_stream_map_) {
    kv.second->flow_controller()->UpdateReceiveWindowSize(stream_window);
  }
  for (auto const& kv : dynamic_stream_map_) {
    kv.second->flow_controller()->UpdateReceiveWindowSize(stream_window);
  }
}

void QuicSession::HandleFrameOnNonexistentOutgoingStream(
    QuicStreamId stream_id) {
  DCHECK(!IsClosedStream(stream_id));
  // Received a frame for a locally-created stream that is not currently
  // active. This is an error.
  connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Data for nonexistent stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSession::HandleRstOnValidNonexistentStream(
    const QuicRstStreamFrame& frame) {
  // If the stream is neither originally in active streams nor created in
  // GetOrCreateDynamicStream(), it could be a closed stream in which case its
  // final received byte offset need to be updated.
  if (IsClosedStream(frame.stream_id)) {
    // The RST frame contains the final byte offset for the stream: we can now
    // update the connection level flow controller if needed.
    OnFinalByteOffsetReceived(frame.stream_id, frame.byte_offset);
  }
}

void QuicSession::OnNewStreamFlowControlWindow(QuicStreamOffset new_window) {
  if (new_window < kMinimumFlowControlSendWindow) {
    QUIC_LOG_FIRST_N(ERROR, 1)
        << "Peer sent us an invalid stream flow control send window: "
        << new_window << ", below default: " << kMinimumFlowControlSendWindow;
    if (connection_->connected()) {
      connection_->CloseConnection(
          QUIC_FLOW_CONTROL_INVALID_WINDOW, "New stream window too low",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    }
    return;
  }

  // Inform all existing streams about the new window.
  for (auto const& kv : static_stream_map_) {
    kv.second->UpdateSendWindowOffset(new_window);
  }
  for (auto const& kv : dynamic_stream_map_) {
    kv.second->UpdateSendWindowOffset(new_window);
  }
}

void QuicSession::OnNewSessionFlowControlWindow(QuicStreamOffset new_window) {
  if (new_window < kMinimumFlowControlSendWindow) {
    QUIC_LOG_FIRST_N(ERROR, 1)
        << "Peer sent us an invalid session flow control send window: "
        << new_window << ", below default: " << kMinimumFlowControlSendWindow;
    if (connection_->connected()) {
      connection_->CloseConnection(
          QUIC_FLOW_CONTROL_INVALID_WINDOW, "New connection window too low",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    }
    return;
  }

  flow_controller_.UpdateSendWindowOffset(new_window);
}

void QuicSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  switch (event) {
    // TODO(satyamshekhar): Move the logic of setting the encrypter/decrypter
    // to QuicSession since it is the glue.
    case ENCRYPTION_FIRST_ESTABLISHED:
      // Given any streams blocked by encryption a chance to write.
      OnCanWrite();
      break;

    case ENCRYPTION_REESTABLISHED:
      // Retransmit originally packets that were sent, since they can't be
      // decrypted by the peer.
      connection_->RetransmitUnackedPackets(ALL_INITIAL_RETRANSMISSION);
      // Given any streams blocked by encryption a chance to write.
      OnCanWrite();
      break;

    case HANDSHAKE_CONFIRMED:
      QUIC_BUG_IF(!config_.negotiated())
          << ENDPOINT << "Handshake confirmed without parameter negotiation.";
      // Discard originally encrypted packets, since they can't be decrypted by
      // the peer.
      NeuterUnencryptedData();
      is_handshake_confirmed_ = true;
      break;

    default:
      QUIC_LOG(ERROR) << ENDPOINT << "Got unknown handshake event: " << event;
  }
}

void QuicSession::OnCryptoHandshakeMessageSent(
    const CryptoHandshakeMessage& /*message*/) {}

void QuicSession::OnCryptoHandshakeMessageReceived(
    const CryptoHandshakeMessage& /*message*/) {}

void QuicSession::RegisterStreamPriority(QuicStreamId id,
                                         bool is_static,
                                         SpdyPriority priority) {
  write_blocked_streams()->RegisterStream(id, is_static, priority);
}

void QuicSession::UnregisterStreamPriority(QuicStreamId id, bool is_static) {
  write_blocked_streams()->UnregisterStream(id, is_static);
}

void QuicSession::UpdateStreamPriority(QuicStreamId id,
                                       SpdyPriority new_priority) {
  write_blocked_streams()->UpdateStreamPriority(id, new_priority);
}

QuicConfig* QuicSession::config() {
  return &config_;
}

void QuicSession::ActivateStream(std::unique_ptr<QuicStream> stream) {
  QuicStreamId stream_id = stream->id();
  QUIC_DVLOG(1) << ENDPOINT << "num_streams: " << dynamic_stream_map_.size()
                << ". activating " << stream_id;
  DCHECK(!QuicContainsKey(dynamic_stream_map_, stream_id));
  DCHECK(!QuicContainsKey(static_stream_map_, stream_id));
  dynamic_stream_map_[stream_id] = std::move(stream);
  if (IsIncomingStream(stream_id)) {
    ++num_dynamic_incoming_streams_;
  }
}

QuicStreamId QuicSession::GetNextOutgoingBidirectionalStreamId() {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.GetNextOutgoingBidirectionalStreamId();
  }
  return stream_id_manager_.GetNextOutgoingStreamId();
}

QuicStreamId QuicSession::GetNextOutgoingUnidirectionalStreamId() {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.GetNextOutgoingUnidirectionalStreamId();
  }
  return stream_id_manager_.GetNextOutgoingStreamId();
}

bool QuicSession::CanOpenNextOutgoingBidirectionalStream() {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.CanOpenNextOutgoingBidirectionalStream();
  }
  return stream_id_manager_.CanOpenNextOutgoingStream(
      GetNumOpenOutgoingStreams());
}

bool QuicSession::CanOpenNextOutgoingUnidirectionalStream() {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.CanOpenNextOutgoingUnidirectionalStream();
  }
  return stream_id_manager_.CanOpenNextOutgoingStream(
      GetNumOpenOutgoingStreams());
}

QuicStream* QuicSession::GetOrCreateStream(const QuicStreamId stream_id) {
  StreamHandler handler =
      GetOrCreateStreamImpl(stream_id, /*may_buffer=*/false);
  DCHECK(!handler.is_pending);
  return handler.stream;
}

QuicSession::StreamHandler QuicSession::GetOrCreateStreamImpl(
    QuicStreamId stream_id,
    bool may_buffer) {
  StaticStreamMap::iterator it = static_stream_map_.find(stream_id);
  if (it != static_stream_map_.end()) {
    return StreamHandler(it->second);
  }
  return GetOrCreateDynamicStreamImpl(stream_id, may_buffer);
}

void QuicSession::StreamDraining(QuicStreamId stream_id) {
  DCHECK(QuicContainsKey(dynamic_stream_map_, stream_id));
  if (!QuicContainsKey(draining_streams_, stream_id)) {
    draining_streams_.insert(stream_id);
    if (IsIncomingStream(stream_id)) {
      ++num_draining_incoming_streams_;
    }
    if (connection_->transport_version() == QUIC_VERSION_99) {
      v99_streamid_manager_.OnStreamClosed(stream_id);
    }
  }
  if (!IsIncomingStream(stream_id)) {
    // Inform application that a stream is available.
    OnCanCreateNewOutgoingStream();
  }
}

bool QuicSession::MaybeIncreaseLargestPeerStreamId(
    const QuicStreamId stream_id) {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.MaybeIncreaseLargestPeerStreamId(stream_id);
  }
  return stream_id_manager_.MaybeIncreaseLargestPeerStreamId(stream_id);
}

bool QuicSession::ShouldYield(QuicStreamId stream_id) {
  if (stream_id == currently_writing_stream_id_) {
    return false;
  }
  return write_blocked_streams()->ShouldYield(stream_id);
}

QuicStream* QuicSession::GetOrCreateDynamicStream(
    const QuicStreamId stream_id) {
  StreamHandler handler =
      GetOrCreateDynamicStreamImpl(stream_id, /*may_buffer=*/false);
  DCHECK(!handler.is_pending);
  return handler.stream;
}

QuicSession::StreamHandler QuicSession::GetOrCreateDynamicStreamImpl(
    QuicStreamId stream_id,
    bool may_buffer) {
  DCHECK(!QuicContainsKey(static_stream_map_, stream_id))
      << "Attempt to call GetOrCreateDynamicStream for a static stream";

  DynamicStreamMap::iterator it = dynamic_stream_map_.find(stream_id);
  if (it != dynamic_stream_map_.end()) {
    return StreamHandler(it->second.get());
  }

  if (IsClosedStream(stream_id)) {
    return StreamHandler();
  }

  if (!IsIncomingStream(stream_id)) {
    HandleFrameOnNonexistentOutgoingStream(stream_id);
    return StreamHandler();
  }

  auto pending_it = pending_stream_map_.find(stream_id);
  if (pending_it != pending_stream_map_.end()) {
    DCHECK_EQ(QUIC_VERSION_99, connection_->transport_version());
    if (may_buffer) {
      return StreamHandler(pending_it->second.get());
    }
    // The stream limit accounting has already been taken care of
    // when the PendingStream was created, so there is no need to
    // do so here. Now we can create the actual stream from the
    // PendingStream.
    StreamHandler handler(CreateIncomingStream(std::move(*pending_it->second)));
    pending_stream_map_.erase(pending_it);
    return handler;
  }

  // TODO(fkastenholz): If we are creating a new stream and we have
  // sent a goaway, we should ignore the stream creation. Need to
  // add code to A) test if goaway was sent ("if (goaway_sent_)") and
  // B) reject stream creation ("return nullptr")

  if (!MaybeIncreaseLargestPeerStreamId(stream_id)) {
    return StreamHandler();
  }

  if (connection_->transport_version() != QUIC_VERSION_99) {
    // TODO(fayang): Let LegacyQuicStreamIdManager count open streams and make
    // CanOpenIncomingStream interface cosistent with that of v99.
    if (!stream_id_manager_.CanOpenIncomingStream(
            GetNumOpenIncomingStreams())) {
      // Refuse to open the stream.
      SendRstStream(stream_id, QUIC_REFUSED_STREAM, 0);
      return StreamHandler();
    }
  }

  if (connection_->transport_version() == QUIC_VERSION_99 && may_buffer &&
      ShouldBufferIncomingStream(stream_id)) {
    ++num_dynamic_incoming_streams_;
    // Since STREAM frames may arrive out of order, delay creating the
    // stream object until the first byte arrives. Buffer the frames and
    // handle flow control accounting in the PendingStream.
    auto pending = QuicMakeUnique<PendingStream>(stream_id, this);
    StreamHandler handler(pending.get());
    pending_stream_map_[stream_id] = std::move(pending);
    return handler;
  }

  return StreamHandler(CreateIncomingStream(stream_id));
}

void QuicSession::set_largest_peer_created_stream_id(
    QuicStreamId largest_peer_created_stream_id) {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    v99_streamid_manager_.SetLargestPeerCreatedStreamId(
        largest_peer_created_stream_id);
    return;
  }
  stream_id_manager_.set_largest_peer_created_stream_id(
      largest_peer_created_stream_id);
}

bool QuicSession::IsClosedStream(QuicStreamId id) {
  DCHECK_NE(QuicUtils::GetInvalidStreamId(connection_->transport_version()),
            id);
  if (IsOpenStream(id)) {
    // Stream is active
    return false;
  }

  if (connection_->transport_version() == QUIC_VERSION_99) {
    return !v99_streamid_manager_.IsAvailableStream(id);
  }

  return !stream_id_manager_.IsAvailableStream(id);
}

bool QuicSession::IsOpenStream(QuicStreamId id) {
  DCHECK_NE(QuicUtils::GetInvalidStreamId(connection_->transport_version()),
            id);
  if (QuicContainsKey(static_stream_map_, id) ||
      QuicContainsKey(dynamic_stream_map_, id) ||
      QuicContainsKey(pending_stream_map_, id)) {
    // Stream is active
    return true;
  }
  return false;
}

size_t QuicSession::GetNumOpenIncomingStreams() const {
  return num_dynamic_incoming_streams_ - num_draining_incoming_streams_ +
         num_locally_closed_incoming_streams_highest_offset_;
}

size_t QuicSession::GetNumOpenOutgoingStreams() const {
  DCHECK_GE(GetNumDynamicOutgoingStreams() +
                GetNumLocallyClosedOutgoingStreamsHighestOffset(),
            GetNumDrainingOutgoingStreams());
  return GetNumDynamicOutgoingStreams() +
         GetNumLocallyClosedOutgoingStreamsHighestOffset() -
         GetNumDrainingOutgoingStreams();
}

size_t QuicSession::GetNumActiveStreams() const {
  return dynamic_stream_map_.size() - draining_streams_.size();
}

size_t QuicSession::GetNumDrainingStreams() const {
  return draining_streams_.size();
}

void QuicSession::MarkConnectionLevelWriteBlocked(QuicStreamId id) {
  if (GetOrCreateStream(id) == nullptr) {
    QUIC_BUG << "Marking unknown stream " << id << " blocked.";
    QUIC_LOG_FIRST_N(ERROR, 2) << QuicStackTrace();
  }

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

void QuicSession::SendPing() {
  control_frame_manager_.WritePing();
}

size_t QuicSession::GetNumDynamicOutgoingStreams() const {
  DCHECK_GE(dynamic_stream_map_.size() + pending_stream_map_.size(),
            num_dynamic_incoming_streams_);
  return dynamic_stream_map_.size() + pending_stream_map_.size() -
         num_dynamic_incoming_streams_;
}

size_t QuicSession::GetNumDrainingOutgoingStreams() const {
  DCHECK_GE(draining_streams_.size(), num_draining_incoming_streams_);
  return draining_streams_.size() - num_draining_incoming_streams_;
}

size_t QuicSession::GetNumLocallyClosedOutgoingStreamsHighestOffset() const {
  DCHECK_GE(locally_closed_streams_highest_offset_.size(),
            num_locally_closed_incoming_streams_highest_offset_);
  return locally_closed_streams_highest_offset_.size() -
         num_locally_closed_incoming_streams_highest_offset_;
}

bool QuicSession::IsConnectionFlowControlBlocked() const {
  return flow_controller_.IsBlocked();
}

bool QuicSession::IsStreamFlowControlBlocked() {
  for (auto const& kv : static_stream_map_) {
    if (kv.second->flow_controller()->IsBlocked()) {
      return true;
    }
  }
  for (auto const& kv : dynamic_stream_map_) {
    if (kv.second->flow_controller()->IsBlocked()) {
      return true;
    }
  }
  return false;
}

size_t QuicSession::MaxAvailableBidirectionalStreams() const {
  if (connection()->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.GetMaxAllowdIncomingBidirectionalStreams();
  }
  return stream_id_manager_.MaxAvailableStreams();
}

size_t QuicSession::MaxAvailableUnidirectionalStreams() const {
  if (connection()->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.GetMaxAllowdIncomingUnidirectionalStreams();
  }
  return stream_id_manager_.MaxAvailableStreams();
}

bool QuicSession::IsIncomingStream(QuicStreamId id) const {
  if (connection()->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.IsIncomingStream(id);
  }
  return stream_id_manager_.IsIncomingStream(id);
}

void QuicSession::OnStreamDoneWaitingForAcks(QuicStreamId id) {
  auto it = zombie_streams_.find(id);
  if (it == zombie_streams_.end()) {
    return;
  }

  closed_streams_.push_back(std::move(it->second));
  if (!closed_streams_clean_up_alarm_->IsSet()) {
    closed_streams_clean_up_alarm_->Set(connection_->clock()->ApproximateNow());
  }
  zombie_streams_.erase(it);
  // Do not retransmit data of a closed stream.
  streams_with_pending_retransmission_.erase(id);
}

QuicStream* QuicSession::GetStream(QuicStreamId id) const {
  if (id <= largest_static_stream_id_) {
    auto static_stream = static_stream_map_.find(id);
    if (static_stream != static_stream_map_.end()) {
      return static_stream->second;
    }
  }

  auto active_stream = dynamic_stream_map_.find(id);
  if (active_stream != dynamic_stream_map_.end()) {
    return active_stream->second.get();
  }
  auto zombie_stream = zombie_streams_.find(id);
  if (zombie_stream != zombie_streams_.end()) {
    return zombie_stream->second.get();
  }
  return nullptr;
}

bool QuicSession::OnFrameAcked(const QuicFrame& frame,
                               QuicTime::Delta ack_delay_time) {
  if (frame.type == MESSAGE_FRAME) {
    OnMessageAcked(frame.message_frame->message_id);
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
        frame.stream_frame.fin, ack_delay_time, &newly_acked_length);
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
  QuicConnection::ScopedPacketFlusher retransmission_flusher(
      connection_, QuicConnection::NO_ACK);
  SetTransmissionType(type);
  for (const QuicFrame& frame : frames) {
    if (frame.type == MESSAGE_FRAME) {
      // Do not retransmit MESSAGE frames.
      continue;
    }
    if (frame.type == CRYPTO_FRAME) {
      GetMutableCryptoStream()->RetransmitData(frame.crypto_frame);
      continue;
    }
    if (frame.type != STREAM_FRAME) {
      if (!control_frame_manager_.RetransmitControlFrame(frame)) {
        break;
      }
      continue;
    }
    QuicStream* stream = GetStream(frame.stream_frame.stream_id);
    if (stream != nullptr &&
        !stream->RetransmitStreamData(frame.stream_frame.offset,
                                      frame.stream_frame.data_length,
                                      frame.stream_frame.fin)) {
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
  if (crypto_stream->IsWaitingForAcks()) {
    return true;
  }
  if (GetQuicReloadableFlag(quic_fix_has_pending_crypto_data) &&
      crypto_stream->HasBufferedData()) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_fix_has_pending_crypto_data);
    return true;
  }
  return false;
}

WriteStreamDataResult QuicSession::WriteStreamData(QuicStreamId id,
                                                   QuicStreamOffset offset,
                                                   QuicByteCount data_length,
                                                   QuicDataWriter* writer) {
  QuicStream* stream = GetStream(id);
  if (stream == nullptr) {
    // This causes the connection to be closed because of failed to serialize
    // packet.
    QUIC_BUG << "Stream " << id << " does not exist when trying to write data.";
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

bool QuicSession::RetransmitLostData() {
  QuicConnection::ScopedPacketFlusher retransmission_flusher(
      connection_, QuicConnection::SEND_ACK_IF_QUEUED);
  // Retransmit crypto data first.
  bool uses_crypto_frames = connection_->transport_version() >= QUIC_VERSION_47;
  QuicCryptoStream* crypto_stream = GetMutableCryptoStream();
  if (uses_crypto_frames && crypto_stream->HasPendingCryptoRetransmission()) {
    SetTransmissionType(HANDSHAKE_RETRANSMISSION);
    crypto_stream->WritePendingCryptoRetransmission();
  }
  // Retransmit crypto data in stream 1 frames (version < 47).
  if (!uses_crypto_frames &&
      QuicContainsKey(
          streams_with_pending_retransmission_,
          QuicUtils::GetCryptoStreamId(connection_->transport_version()))) {
    SetTransmissionType(HANDSHAKE_RETRANSMISSION);
    // Retransmit crypto data first.
    QuicStream* crypto_stream = GetStream(
        QuicUtils::GetCryptoStreamId(connection_->transport_version()));
    crypto_stream->OnCanWrite();
    DCHECK(CheckStreamWriteBlocked(crypto_stream));
    if (crypto_stream->HasPendingRetransmission()) {
      // Connection is write blocked.
      return false;
    } else {
      streams_with_pending_retransmission_.erase(
          QuicUtils::GetCryptoStreamId(connection_->transport_version()));
    }
  }
  if (control_frame_manager_.HasPendingRetransmission()) {
    SetTransmissionType(LOSS_RETRANSMISSION);
    control_frame_manager_.OnCanWrite();
    if (control_frame_manager_.HasPendingRetransmission()) {
      return false;
    }
  }
  while (!streams_with_pending_retransmission_.empty()) {
    if (!connection_->CanWriteStreamData()) {
      break;
    }
    // Retransmit lost data on headers and data streams.
    const QuicStreamId id = streams_with_pending_retransmission_.begin()->first;
    QuicStream* stream = GetStream(id);
    if (stream != nullptr) {
      SetTransmissionType(LOSS_RETRANSMISSION);
      stream->OnCanWrite();
      DCHECK(CheckStreamWriteBlocked(stream));
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
  if (connection_->session_decides_what_to_write()) {
    QuicCryptoStream* crypto_stream = GetMutableCryptoStream();
    crypto_stream->NeuterUnencryptedStreamData();
    if (!crypto_stream->HasPendingRetransmission()) {
      streams_with_pending_retransmission_.erase(
          QuicUtils::GetCryptoStreamId(connection_->transport_version()));
    }
  }
  connection_->NeuterUnencryptedPackets();
}

void QuicSession::SetTransmissionType(TransmissionType type) {
  connection_->SetTransmissionType(type);
}

MessageResult QuicSession::SendMessage(QuicMemSliceSpan message) {
  if (!IsEncryptionEstablished()) {
    return {MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED, 0};
  }
  MessageStatus result =
      connection_->SendMessage(last_message_id_ + 1, message);
  if (result == MESSAGE_STATUS_SUCCESS) {
    return {result, ++last_message_id_};
  }
  return {result, 0};
}

void QuicSession::OnMessageAcked(QuicMessageId message_id) {
  QUIC_DVLOG(1) << ENDPOINT << "message " << message_id << " gets acked.";
}

void QuicSession::OnMessageLost(QuicMessageId message_id) {
  QUIC_DVLOG(1) << ENDPOINT << "message " << message_id
                << " is considered lost";
}

void QuicSession::CleanUpClosedStreams() {
  closed_streams_.clear();
}

bool QuicSession::session_decides_what_to_write() const {
  return connection_->session_decides_what_to_write();
}

QuicPacketLength QuicSession::GetLargestMessagePayload() const {
  return connection_->GetLargestMessagePayload();
}

void QuicSession::SendStopSending(uint16_t code, QuicStreamId stream_id) {
  control_frame_manager_.WriteOrBufferStopSending(code, stream_id);
}

void QuicSession::OnCanCreateNewOutgoingStream() {}

QuicStreamId QuicSession::next_outgoing_bidirectional_stream_id() const {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.next_outgoing_bidirectional_stream_id();
  }
  return stream_id_manager_.next_outgoing_stream_id();
}

QuicStreamId QuicSession::next_outgoing_unidirectional_stream_id() const {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.next_outgoing_unidirectional_stream_id();
  }
  return stream_id_manager_.next_outgoing_stream_id();
}

bool QuicSession::OnMaxStreamIdFrame(const QuicMaxStreamIdFrame& frame) {
  return v99_streamid_manager_.OnMaxStreamIdFrame(frame);
}

bool QuicSession::OnStreamIdBlockedFrame(
    const QuicStreamIdBlockedFrame& frame) {
  return v99_streamid_manager_.OnStreamIdBlockedFrame(frame);
}

size_t QuicSession::max_open_incoming_bidirectional_streams() const {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.GetMaxAllowdIncomingBidirectionalStreams();
  }
  return stream_id_manager_.max_open_incoming_streams();
}

size_t QuicSession::max_open_incoming_unidirectional_streams() const {
  if (connection_->transport_version() == QUIC_VERSION_99) {
    return v99_streamid_manager_.GetMaxAllowdIncomingUnidirectionalStreams();
  }
  return stream_id_manager_.max_open_incoming_streams();
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
