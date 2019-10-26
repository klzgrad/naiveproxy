// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/quic_headers_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_stack_trace.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/spdy/core/http2_frame_decoder_adapter.h"

using http2::Http2DecoderAdapter;
using spdy::HpackEntry;
using spdy::HpackHeaderTable;
using spdy::Http2WeightToSpdy3Priority;
using spdy::Spdy3PriorityToHttp2Weight;
using spdy::SpdyErrorCode;
using spdy::SpdyFramer;
using spdy::SpdyFramerDebugVisitorInterface;
using spdy::SpdyFramerVisitorInterface;
using spdy::SpdyFrameType;
using spdy::SpdyHeaderBlock;
using spdy::SpdyHeadersHandlerInterface;
using spdy::SpdyHeadersIR;
using spdy::SpdyKnownSettingsId;
using spdy::SpdyPingId;
using spdy::SpdyPriority;
using spdy::SpdyPriorityIR;
using spdy::SpdyPushPromiseIR;
using spdy::SpdySerializedFrame;
using spdy::SpdySettingsId;
using spdy::SpdySettingsIR;
using spdy::SpdyStreamId;

namespace quic {

namespace {

// TODO(b/124216424): remove this once HTTP/3 error codes are adopted.
const uint16_t kHttpUnknownStreamType = 0x0D;

class HeaderTableDebugVisitor : public HpackHeaderTable::DebugVisitorInterface {
 public:
  HeaderTableDebugVisitor(const QuicClock* clock,
                          std::unique_ptr<QuicHpackDebugVisitor> visitor)
      : clock_(clock), headers_stream_hpack_visitor_(std::move(visitor)) {}
  HeaderTableDebugVisitor(const HeaderTableDebugVisitor&) = delete;
  HeaderTableDebugVisitor& operator=(const HeaderTableDebugVisitor&) = delete;

  int64_t OnNewEntry(const HpackEntry& entry) override {
    QUIC_DVLOG(1) << entry.GetDebugString();
    return (clock_->ApproximateNow() - QuicTime::Zero()).ToMicroseconds();
  }

  void OnUseEntry(const HpackEntry& entry) override {
    const QuicTime::Delta elapsed(
        clock_->ApproximateNow() -
        QuicTime::Delta::FromMicroseconds(entry.time_added()) -
        QuicTime::Zero());
    QUIC_DVLOG(1) << entry.GetDebugString() << " " << elapsed.ToMilliseconds()
                  << " ms";
    headers_stream_hpack_visitor_->OnUseEntry(elapsed);
  }

 private:
  const QuicClock* clock_;
  std::unique_ptr<QuicHpackDebugVisitor> headers_stream_hpack_visitor_;
};

}  // namespace

// A SpdyFramerVisitor that passes HEADERS frames to the QuicSpdyStream, and
// closes the connection if any unexpected frames are received.
class QuicSpdySession::SpdyFramerVisitor
    : public SpdyFramerVisitorInterface,
      public SpdyFramerDebugVisitorInterface {
 public:
  explicit SpdyFramerVisitor(QuicSpdySession* session) : session_(session) {}
  SpdyFramerVisitor(const SpdyFramerVisitor&) = delete;
  SpdyFramerVisitor& operator=(const SpdyFramerVisitor&) = delete;

  SpdyHeadersHandlerInterface* OnHeaderFrameStart(
      SpdyStreamId /* stream_id */) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    return &header_list_;
  }

  void OnHeaderFrameEnd(SpdyStreamId /* stream_id */) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    if (session_->IsConnected()) {
      session_->OnHeaderList(header_list_);
    }
    header_list_.Clear();
  }

  void OnStreamFrameData(SpdyStreamId /*stream_id*/,
                         const char* /*data*/,
                         size_t /*len*/) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    CloseConnection("SPDY DATA frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnStreamEnd(SpdyStreamId /*stream_id*/) override {
    // The framer invokes OnStreamEnd after processing a frame that had the fin
    // bit set.
  }

  void OnStreamPadding(SpdyStreamId /*stream_id*/, size_t /*len*/) override {
    CloseConnection("SPDY frame padding received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnError(Http2DecoderAdapter::SpdyFramerError error) override {
    QuicErrorCode code = QUIC_INVALID_HEADERS_STREAM_DATA;
    switch (error) {
      case Http2DecoderAdapter::SpdyFramerError::SPDY_DECOMPRESS_FAILURE:
        code = QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE;
        break;
      default:
        break;
    }
    CloseConnection(
        QuicStrCat("SPDY framing error: ",
                   Http2DecoderAdapter::SpdyFramerErrorToString(error)),
        code);
  }

  void OnDataFrameHeader(SpdyStreamId /*stream_id*/,
                         size_t /*length*/,
                         bool /*fin*/) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    CloseConnection("SPDY DATA frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnRstStream(SpdyStreamId /*stream_id*/,
                   SpdyErrorCode /*error_code*/) override {
    CloseConnection("SPDY RST_STREAM frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnSetting(SpdySettingsId id, uint32_t value) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    session_->OnSetting(id, value);
  }

  void OnSettingsEnd() override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
  }

  void OnPing(SpdyPingId /*unique_id*/, bool /*is_ack*/) override {
    CloseConnection("SPDY PING frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnGoAway(SpdyStreamId /*last_accepted_stream_id*/,
                SpdyErrorCode /*error_code*/) override {
    CloseConnection("SPDY GOAWAY frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnHeaders(SpdyStreamId stream_id,
                 bool has_priority,
                 int weight,
                 SpdyStreamId parent_stream_id,
                 bool exclusive,
                 bool fin,
                 bool /*end*/) override {
    if (!session_->IsConnected()) {
      return;
    }

    if (VersionUsesQpack(session_->connection()->transport_version())) {
      CloseConnection("HEADERS frame not allowed on headers stream.",
                      QUIC_INVALID_HEADERS_STREAM_DATA);
      return;
    }

    QUIC_BUG_IF(session_->destruction_indicator() != 123456789)
        << "QuicSpdyStream use after free. "
        << session_->destruction_indicator() << QuicStackTrace();

    if (session_->use_http2_priority_write_scheduler()) {
      session_->OnHeaders(
          stream_id, has_priority,
          spdy::SpdyStreamPrecedence(parent_stream_id, weight, exclusive), fin);
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_use_http2_priority_write_scheduler, 1,
                                   3);
      return;
    }

    SpdyPriority priority =
        has_priority ? Http2WeightToSpdy3Priority(weight) : 0;
    session_->OnHeaders(stream_id, has_priority,
                        spdy::SpdyStreamPrecedence(priority), fin);
  }

  void OnWindowUpdate(SpdyStreamId /*stream_id*/,
                      int /*delta_window_size*/) override {
    CloseConnection("SPDY WINDOW_UPDATE frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnPushPromise(SpdyStreamId stream_id,
                     SpdyStreamId promised_stream_id,
                     bool /*end*/) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    if (!session_->supports_push_promise()) {
      CloseConnection("PUSH_PROMISE not supported.",
                      QUIC_INVALID_HEADERS_STREAM_DATA);
      return;
    }
    if (!session_->IsConnected()) {
      return;
    }
    session_->OnPushPromise(stream_id, promised_stream_id);
  }

  void OnContinuation(SpdyStreamId /*stream_id*/, bool /*end*/) override {}

  void OnPriority(SpdyStreamId stream_id,
                  SpdyStreamId parent_id,
                  int weight,
                  bool exclusive) override {
    DCHECK(!VersionUsesQpack(session_->transport_version()));
    if (session_->connection()->transport_version() <= QUIC_VERSION_39) {
      CloseConnection("SPDY PRIORITY frame received.",
                      QUIC_INVALID_HEADERS_STREAM_DATA);
      return;
    }
    if (!session_->IsConnected()) {
      return;
    }
    if (session_->use_http2_priority_write_scheduler()) {
      session_->OnPriority(
          stream_id, spdy::SpdyStreamPrecedence(parent_id, weight, exclusive));
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_use_http2_priority_write_scheduler, 2,
                                   3);
      return;
    }
    SpdyPriority priority = Http2WeightToSpdy3Priority(weight);
    session_->OnPriority(stream_id, spdy::SpdyStreamPrecedence(priority));
  }

  bool OnUnknownFrame(SpdyStreamId /*stream_id*/,
                      uint8_t /*frame_type*/) override {
    CloseConnection("Unknown frame type received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
    return false;
  }

  // SpdyFramerDebugVisitorInterface implementation
  void OnSendCompressedFrame(SpdyStreamId /*stream_id*/,
                             SpdyFrameType /*type*/,
                             size_t payload_len,
                             size_t frame_len) override {
    if (payload_len == 0) {
      QUIC_BUG << "Zero payload length.";
      return;
    }
    int compression_pct = 100 - (100 * frame_len) / payload_len;
    QUIC_DVLOG(1) << "Net.QuicHpackCompressionPercentage: " << compression_pct;
  }

  void OnReceiveCompressedFrame(SpdyStreamId /*stream_id*/,
                                SpdyFrameType /*type*/,
                                size_t frame_len) override {
    if (session_->IsConnected()) {
      session_->OnCompressedFrameSize(frame_len);
    }
  }

  void set_max_header_list_size(size_t max_header_list_size) {
    header_list_.set_max_header_list_size(max_header_list_size);
  }

 private:
  void CloseConnection(const std::string& details, QuicErrorCode code) {
    if (session_->IsConnected()) {
      session_->CloseConnectionWithDetails(code, details);
    }
  }

 private:
  QuicSpdySession* session_;
  QuicHeaderList header_list_;
};

QuicHpackDebugVisitor::QuicHpackDebugVisitor() {}

QuicHpackDebugVisitor::~QuicHpackDebugVisitor() {}

QuicSpdySession::QuicSpdySession(
    QuicConnection* connection,
    QuicSession::Visitor* visitor,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions)
    : QuicSession(connection,
                  visitor,
                  config,
                  supported_versions,
                  /*num_expected_unidirectional_static_streams = */
                  VersionUsesQpack(connection->transport_version()) ? 3 : 0),
      send_control_stream_(nullptr),
      receive_control_stream_(nullptr),
      qpack_encoder_receive_stream_(nullptr),
      qpack_decoder_receive_stream_(nullptr),
      qpack_encoder_send_stream_(nullptr),
      qpack_decoder_send_stream_(nullptr),
      qpack_maximum_dynamic_table_capacity_(
          kDefaultQpackMaxDynamicTableCapacity),
      qpack_maximum_blocked_streams_(kDefaultMaximumBlockedStreams),
      max_inbound_header_list_size_(kDefaultMaxUncompressedHeaderSize),
      max_outbound_header_list_size_(kDefaultMaxUncompressedHeaderSize),
      server_push_enabled_(true),
      stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      promised_stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      fin_(false),
      frame_len_(0),
      supports_push_promise_(perspective() == Perspective::IS_CLIENT),
      spdy_framer_(SpdyFramer::ENABLE_COMPRESSION),
      spdy_framer_visitor_(new SpdyFramerVisitor(this)),
      max_allowed_push_id_(0),
      destruction_indicator_(123456789) {
  h2_deframer_.set_visitor(spdy_framer_visitor_.get());
  h2_deframer_.set_debug_visitor(spdy_framer_visitor_.get());
  spdy_framer_.set_debug_visitor(spdy_framer_visitor_.get());
}

QuicSpdySession::~QuicSpdySession() {
  QUIC_BUG_IF(destruction_indicator_ != 123456789)
      << "QuicSpdyStream use after free. " << destruction_indicator_
      << QuicStackTrace();
  // Set the streams' session pointers in closed and dynamic stream lists
  // to null to avoid subsequent use of this session.
  for (auto& stream : *closed_streams()) {
    static_cast<QuicSpdyStream*>(stream.get())->ClearSession();
  }
  for (auto const& kv : zombie_streams()) {
    static_cast<QuicSpdyStream*>(kv.second.get())->ClearSession();
  }
  for (auto const& kv : stream_map()) {
    if (!kv.second->is_static()) {
      static_cast<QuicSpdyStream*>(kv.second.get())->ClearSession();
    }
  }
  destruction_indicator_ = 987654321;
}

void QuicSpdySession::Initialize() {
  QuicSession::Initialize();

  if (!VersionUsesQpack(connection()->transport_version())) {
    if (perspective() == Perspective::IS_SERVER) {
      set_largest_peer_created_stream_id(
          QuicUtils::GetHeadersStreamId(connection()->transport_version()));
    } else {
      QuicStreamId headers_stream_id = GetNextOutgoingBidirectionalStreamId();
      DCHECK_EQ(headers_stream_id, QuicUtils::GetHeadersStreamId(
                                       connection()->transport_version()));
    }
    auto headers_stream = QuicMakeUnique<QuicHeadersStream>((this));
    DCHECK_EQ(QuicUtils::GetHeadersStreamId(connection()->transport_version()),
              headers_stream->id());

    headers_stream_ = headers_stream.get();
    RegisterStaticStream(std::move(headers_stream));
  } else {
    ConfigureMaxIncomingDynamicStreamsToSend(
        config()->GetMaxIncomingUnidirectionalStreamsToSend());
    qpack_encoder_ = QuicMakeUnique<QpackEncoder>(this);
    qpack_decoder_ =
        QuicMakeUnique<QpackDecoder>(qpack_maximum_dynamic_table_capacity_,
                                     qpack_maximum_blocked_streams_, this);
    MaybeInitializeHttp3UnidirectionalStreams();
    // TODO(b/112770235): Send SETTINGS_QPACK_BLOCKED_STREAMS with value
    // qpack_maximum_blocked_streams_.
  }

  spdy_framer_visitor_->set_max_header_list_size(max_inbound_header_list_size_);

  // Limit HPACK buffering to 2x header list size limit.
  h2_deframer_.GetHpackDecoder()->set_max_decode_buffer_size_bytes(
      2 * max_inbound_header_list_size_);
}

void QuicSpdySession::OnDecoderStreamError(QuicStringPiece /*error_message*/) {
  DCHECK(VersionUsesQpack(connection()->transport_version()));

  // TODO(112770235): Signal connection error on decoder stream errors.
  QUIC_NOTREACHED();
}

void QuicSpdySession::OnEncoderStreamError(QuicStringPiece /*error_message*/) {
  DCHECK(VersionUsesQpack(connection()->transport_version()));

  // TODO(112770235): Signal connection error on encoder stream errors.
  QUIC_NOTREACHED();
}

void QuicSpdySession::OnStreamHeadersPriority(
    QuicStreamId stream_id,
    const spdy::SpdyStreamPrecedence& precedence) {
  QuicSpdyStream* stream = GetSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive headers after a stream has been reset.
    return;
  }
  stream->OnStreamHeadersPriority(precedence);
}

void QuicSpdySession::OnStreamHeaderList(QuicStreamId stream_id,
                                         bool fin,
                                         size_t frame_len,
                                         const QuicHeaderList& header_list) {
  if (IsStaticStream(stream_id)) {
    connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "stream is static",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  QuicSpdyStream* stream = GetSpdyDataStream(stream_id);
  if (stream == nullptr) {
    // The stream no longer exists, but trailing headers may contain the final
    // byte offset necessary for flow control and open stream accounting.
    size_t final_byte_offset = 0;
    for (const auto& header : header_list) {
      const std::string& header_key = header.first;
      const std::string& header_value = header.second;
      if (header_key == kFinalOffsetHeaderKey) {
        if (!QuicTextUtils::StringToSizeT(header_value, &final_byte_offset)) {
          connection()->CloseConnection(
              QUIC_INVALID_HEADERS_STREAM_DATA,
              "Trailers are malformed (no final offset)",
              ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
          return;
        }
        QUIC_DVLOG(1) << "Received final byte offset in trailers for stream "
                      << stream_id << ", which no longer exists.";
        OnFinalByteOffsetReceived(stream_id, final_byte_offset);
      }
    }

    // It's quite possible to receive headers after a stream has been reset.
    return;
  }
  stream->OnStreamHeaderList(fin, frame_len, header_list);
}

void QuicSpdySession::OnPriorityFrame(
    QuicStreamId stream_id,
    const spdy::SpdyStreamPrecedence& precedence) {
  QuicSpdyStream* stream = GetSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive a PRIORITY frame after a stream has been
    // reset.
    return;
  }
  stream->OnPriorityFrame(precedence);
}

size_t QuicSpdySession::ProcessHeaderData(const struct iovec& iov) {
  QUIC_BUG_IF(destruction_indicator_ != 123456789)
      << "QuicSpdyStream use after free. " << destruction_indicator_
      << QuicStackTrace();
  return h2_deframer_.ProcessInput(static_cast<char*>(iov.iov_base),
                                   iov.iov_len);
}

size_t QuicSpdySession::WriteHeadersOnHeadersStream(
    QuicStreamId id,
    SpdyHeaderBlock headers,
    bool fin,
    const spdy::SpdyStreamPrecedence& precedence,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  DCHECK(!VersionUsesQpack(connection()->transport_version()));

  return WriteHeadersOnHeadersStreamImpl(
      id, std::move(headers), fin,
      /* parent_stream_id = */ 0,
      Spdy3PriorityToHttp2Weight(precedence.spdy3_priority()),
      /* exclusive = */ false, std::move(ack_listener));
}

size_t QuicSpdySession::WritePriority(QuicStreamId id,
                                      QuicStreamId parent_stream_id,
                                      int weight,
                                      bool exclusive) {
  DCHECK(!VersionUsesQpack(connection()->transport_version()));
  if (connection()->transport_version() <= QUIC_VERSION_39) {
    return 0;
  }
  SpdyPriorityIR priority_frame(id, parent_stream_id, weight, exclusive);
  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(priority_frame));
  headers_stream()->WriteOrBufferData(
      QuicStringPiece(frame.data(), frame.size()), false, nullptr);
  return frame.size();
}

void QuicSpdySession::WriteH3Priority(const PriorityFrame& priority) {
  DCHECK(VersionHasStreamType(connection()->transport_version()));
  DCHECK(perspective() == Perspective::IS_CLIENT)
      << "Server must not send priority";

  QuicConnection::ScopedPacketFlusher flusher(connection());
  SendMaxHeaderListSize(max_inbound_header_list_size_);
  send_control_stream_->WritePriority(priority);
}

void QuicSpdySession::WritePushPromise(QuicStreamId original_stream_id,
                                       QuicStreamId promised_stream_id,
                                       SpdyHeaderBlock headers) {
  if (perspective() == Perspective::IS_CLIENT) {
    QUIC_BUG << "Client shouldn't send PUSH_PROMISE";
    return;
  }

  if (VersionHasIetfQuicFrames(connection()->transport_version()) &&
      promised_stream_id > max_allowed_push_id()) {
    QUIC_BUG
        << "Server shouldn't send push id higher than client's MAX_PUSH_ID.";
    return;
  }

  if (!VersionHasStreamType(connection()->transport_version())) {
    SpdyPushPromiseIR push_promise(original_stream_id, promised_stream_id,
                                   std::move(headers));
    // PUSH_PROMISE must not be the last frame sent out, at least followed by
    // response headers.
    push_promise.set_fin(false);

    SpdySerializedFrame frame(spdy_framer_.SerializeFrame(push_promise));
    headers_stream()->WriteOrBufferData(
        QuicStringPiece(frame.data(), frame.size()), false, nullptr);
    return;
  }

  // Encode header list.
  std::string encoded_headers =
      qpack_encoder_->EncodeHeaderList(original_stream_id, headers);
  PushPromiseFrame frame;
  frame.push_id = promised_stream_id;
  frame.headers = encoded_headers;
  QuicSpdyStream* stream = GetSpdyDataStream(original_stream_id);
  stream->WritePushPromise(frame);
}

void QuicSpdySession::SendMaxHeaderListSize(size_t value) {
  if (VersionHasStreamType(connection()->transport_version())) {
    QuicConnection::ScopedPacketFlusher flusher(connection());
    send_control_stream_->MaybeSendSettingsFrame();
    // TODO(renjietang): Remove this once stream id manager can take dynamically
    // created HTTP/3 unidirectional streams.
    qpack_encoder_send_stream_->MaybeSendStreamType();
    qpack_decoder_send_stream_->MaybeSendStreamType();
    return;
  }
  if (GetQuicReloadableFlag(quic_do_not_send_settings)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_do_not_send_settings);
    return;
  }

  SpdySettingsIR settings_frame;
  settings_frame.AddSetting(SETTINGS_MAX_HEADER_LIST_SIZE, value);

  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(settings_frame));
  headers_stream()->WriteOrBufferData(
      QuicStringPiece(frame.data(), frame.size()), false, nullptr);
}

QpackEncoder* QuicSpdySession::qpack_encoder() {
  DCHECK(VersionUsesQpack(connection()->transport_version()));

  return qpack_encoder_.get();
}

QpackDecoder* QuicSpdySession::qpack_decoder() {
  DCHECK(VersionUsesQpack(connection()->transport_version()));

  return qpack_decoder_.get();
}

QuicSpdyStream* QuicSpdySession::GetSpdyDataStream(
    const QuicStreamId stream_id) {
  QuicStream* stream = nullptr;
  if (GetQuicReloadableFlag(quic_inline_getorcreatedynamicstream) &&
      GetQuicReloadableFlag(quic_handle_staticness_for_spdy_stream)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_inline_getorcreatedynamicstream);
    stream = GetOrCreateStream(stream_id);
  } else {
    stream = GetOrCreateDynamicStream(stream_id);
  }
  if (GetQuicReloadableFlag(quic_handle_staticness_for_spdy_stream) && stream &&
      stream->is_static()) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_handle_staticness_for_spdy_stream);
    QUIC_BUG << "GetSpdyDataStream returns static stream";
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "stream is static",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return nullptr;
  }
  DCHECK(!stream || !stream->is_static());
  return static_cast<QuicSpdyStream*>(stream);
}

void QuicSpdySession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  QuicSession::OnCryptoHandshakeEvent(event);
  if (VersionUsesQpack(transport_version()) && event == HANDSHAKE_CONFIRMED) {
    SendMaxHeaderListSize(max_inbound_header_list_size_);
    return;
  }
  if (event == HANDSHAKE_CONFIRMED && config()->SupportMaxHeaderListSize()) {
    SendMaxHeaderListSize(max_inbound_header_list_size_);
  }
}

// True if there are open HTTP requests.
bool QuicSpdySession::ShouldKeepConnectionAlive() const {
  if (GetQuicReloadableFlag(quic_aggressive_connection_aliveness)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_aggressive_connection_aliveness);
    return GetNumActiveStreams() > 0;
  }
  return GetNumOpenDynamicStreams() > 0;
}

bool QuicSpdySession::UsesPendingStreams() const {
  // QuicSpdySession supports PendingStreams, therefore this method should
  // eventually just return true.  However, pending streams can only be used if
  // unidirectional stream type is supported.
  return VersionHasStreamType(connection()->transport_version());
}

size_t QuicSpdySession::WriteHeadersOnHeadersStreamImpl(
    QuicStreamId id,
    spdy::SpdyHeaderBlock headers,
    bool fin,
    QuicStreamId parent_stream_id,
    int weight,
    bool exclusive,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  DCHECK(!VersionUsesQpack(connection()->transport_version()));

  SpdyHeadersIR headers_frame(id, std::move(headers));
  headers_frame.set_fin(fin);
  if (perspective() == Perspective::IS_CLIENT) {
    headers_frame.set_has_priority(true);
    headers_frame.set_parent_stream_id(parent_stream_id);
    headers_frame.set_weight(weight);
    headers_frame.set_exclusive(exclusive);
  }
  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(headers_frame));
  headers_stream()->WriteOrBufferData(
      QuicStringPiece(frame.data(), frame.size()), false,
      std::move(ack_listener));
  return frame.size();
}

void QuicSpdySession::OnPromiseHeaderList(
    QuicStreamId /*stream_id*/,
    QuicStreamId /*promised_stream_id*/,
    size_t /*frame_len*/,
    const QuicHeaderList& /*header_list*/) {
  std::string error =
      "OnPromiseHeaderList should be overridden in client code.";
  QUIC_BUG << error;
  connection()->CloseConnection(QUIC_INTERNAL_ERROR, error,
                                ConnectionCloseBehavior::SILENT_CLOSE);
}

void QuicSpdySession::OnSetting(uint64_t id, uint64_t value) {
  if (VersionHasStreamType(connection()->transport_version())) {
    // SETTINGS frame received on the control stream.
    switch (id) {
      case SETTINGS_QPACK_MAX_TABLE_CAPACITY:
        QUIC_DVLOG(1)
            << "SETTINGS_QPACK_MAX_TABLE_CAPACITY received with value "
            << value;
        // Communicate |value| to encoder, because it is used for encoding
        // Required Insert Count.
        qpack_encoder_->SetMaximumDynamicTableCapacity(value);
        // However, limit the dynamic table capacity to
        // |qpack_maximum_dynamic_table_capacity_|.
        qpack_encoder_->SetDynamicTableCapacity(
            std::min(value, qpack_maximum_dynamic_table_capacity_));
        break;
      case SETTINGS_MAX_HEADER_LIST_SIZE:
        QUIC_DVLOG(1) << "SETTINGS_MAX_HEADER_LIST_SIZE received with value "
                      << value;
        max_outbound_header_list_size_ = value;
        break;
      case SETTINGS_QPACK_BLOCKED_STREAMS:
        QUIC_DVLOG(1) << "SETTINGS_QPACK_BLOCKED_STREAMS received with value "
                      << value;
        qpack_encoder_->SetMaximumBlockedStreams(value);
        break;
      case SETTINGS_NUM_PLACEHOLDERS:
        QUIC_DVLOG(1) << "SETTINGS_NUM_PLACEHOLDERS received with value "
                      << value;
        // TODO: Support placeholder setting.
        break;
      default:
        QUIC_DVLOG(1) << "Unknown setting identifier " << id
                      << " received with value " << value;
        // Ignore unknown settings.
        break;
    }
    return;
  }

  // SETTINGS frame received on the headers stream.
  switch (id) {
    case spdy::SETTINGS_HEADER_TABLE_SIZE:
      QUIC_DVLOG(1) << "SETTINGS_HEADER_TABLE_SIZE received with value "
                    << value;
      spdy_framer_.UpdateHeaderEncoderTableSize(value);
      break;
    case spdy::SETTINGS_ENABLE_PUSH:
      if (perspective() == Perspective::IS_SERVER) {
        // See rfc7540, Section 6.5.2.
        if (value > 1) {
          QUIC_DLOG(ERROR) << "Invalid value " << value
                           << " received for SETTINGS_ENABLE_PUSH.";
          if (IsConnected()) {
            CloseConnectionWithDetails(
                QUIC_INVALID_HEADERS_STREAM_DATA,
                QuicStrCat("Invalid value for SETTINGS_ENABLE_PUSH: ", value));
          }
          return;
        }
        QUIC_DVLOG(1) << "SETTINGS_ENABLE_PUSH received with value " << value;
        server_push_enabled_ = value;
        break;
      } else {
        QUIC_DLOG(ERROR)
            << "Invalid SETTINGS_ENABLE_PUSH received by client with value "
            << value;
        if (IsConnected()) {
          CloseConnectionWithDetails(
              QUIC_INVALID_HEADERS_STREAM_DATA,
              QuicStrCat("Unsupported field of HTTP/2 SETTINGS frame: ", id));
        }
      }
      break;
    // TODO(fayang): Need to support SETTINGS_MAX_HEADER_LIST_SIZE when
    // clients are actually sending it.
    case spdy::SETTINGS_MAX_HEADER_LIST_SIZE:
      QUIC_DVLOG(1) << "SETTINGS_MAX_HEADER_LIST_SIZE received with value "
                    << value;
      break;
    default:
      QUIC_DLOG(ERROR) << "Unknown setting identifier " << id
                       << " received with value " << value;
      if (IsConnected()) {
        CloseConnectionWithDetails(
            QUIC_INVALID_HEADERS_STREAM_DATA,
            QuicStrCat("Unsupported field of HTTP/2 SETTINGS frame: ", id));
      }
  }
}

bool QuicSpdySession::ShouldReleaseHeadersStreamSequencerBuffer() {
  return false;
}

void QuicSpdySession::OnHeaders(SpdyStreamId stream_id,
                                bool has_priority,
                                const spdy::SpdyStreamPrecedence& precedence,
                                bool fin) {
  if (has_priority) {
    if (perspective() == Perspective::IS_CLIENT) {
      CloseConnectionWithDetails(QUIC_INVALID_HEADERS_STREAM_DATA,
                                 "Server must not send priorities.");
      return;
    }
    OnStreamHeadersPriority(stream_id, precedence);
  } else {
    if (perspective() == Perspective::IS_SERVER) {
      CloseConnectionWithDetails(QUIC_INVALID_HEADERS_STREAM_DATA,
                                 "Client must send priorities.");
      return;
    }
  }
  DCHECK_EQ(QuicUtils::GetInvalidStreamId(connection()->transport_version()),
            stream_id_);
  DCHECK_EQ(QuicUtils::GetInvalidStreamId(connection()->transport_version()),
            promised_stream_id_);
  stream_id_ = stream_id;
  fin_ = fin;
}

void QuicSpdySession::OnPushPromise(SpdyStreamId stream_id,
                                    SpdyStreamId promised_stream_id) {
  DCHECK_EQ(QuicUtils::GetInvalidStreamId(connection()->transport_version()),
            stream_id_);
  DCHECK_EQ(QuicUtils::GetInvalidStreamId(connection()->transport_version()),
            promised_stream_id_);
  stream_id_ = stream_id;
  promised_stream_id_ = promised_stream_id;
}

// TODO (wangyix): Why is SpdyStreamId used instead of QuicStreamId?
// This occurs in many places in this file.
void QuicSpdySession::OnPriority(SpdyStreamId stream_id,
                                 const spdy::SpdyStreamPrecedence& precedence) {
  if (perspective() == Perspective::IS_CLIENT) {
    CloseConnectionWithDetails(QUIC_INVALID_HEADERS_STREAM_DATA,
                               "Server must not send PRIORITY frames.");
    return;
  }
  OnPriorityFrame(stream_id, precedence);
}

void QuicSpdySession::OnHeaderList(const QuicHeaderList& header_list) {
  QUIC_DVLOG(1) << "Received header list for stream " << stream_id_ << ": "
                << header_list.DebugString();
  // This code path is only executed for push promise in IETF QUIC.
  if (VersionUsesQpack(connection()->transport_version())) {
    DCHECK(promised_stream_id_ !=
           QuicUtils::GetInvalidStreamId(connection()->transport_version()));
  }
  if (promised_stream_id_ ==
      QuicUtils::GetInvalidStreamId(connection()->transport_version())) {
    OnStreamHeaderList(stream_id_, fin_, frame_len_, header_list);
  } else {
    OnPromiseHeaderList(stream_id_, promised_stream_id_, frame_len_,
                        header_list);
  }
  // Reset state for the next frame.
  promised_stream_id_ =
      QuicUtils::GetInvalidStreamId(connection()->transport_version());
  stream_id_ = QuicUtils::GetInvalidStreamId(connection()->transport_version());
  fin_ = false;
  frame_len_ = 0;
}

void QuicSpdySession::OnCompressedFrameSize(size_t frame_len) {
  frame_len_ += frame_len;
}

void QuicSpdySession::SetHpackEncoderDebugVisitor(
    std::unique_ptr<QuicHpackDebugVisitor> visitor) {
  spdy_framer_.SetEncoderHeaderTableDebugVisitor(
      std::unique_ptr<HeaderTableDebugVisitor>(new HeaderTableDebugVisitor(
          connection()->helper()->GetClock(), std::move(visitor))));
}

void QuicSpdySession::SetHpackDecoderDebugVisitor(
    std::unique_ptr<QuicHpackDebugVisitor> visitor) {
  h2_deframer_.SetDecoderHeaderTableDebugVisitor(
      QuicMakeUnique<HeaderTableDebugVisitor>(
          connection()->helper()->GetClock(), std::move(visitor)));
}

void QuicSpdySession::CloseConnectionWithDetails(QuicErrorCode error,
                                                 const std::string& details) {
  connection()->CloseConnection(
      error, details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

bool QuicSpdySession::HasActiveRequestStreams() const {
  if (GetQuicReloadableFlag(quic_active_streams_never_negative)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_active_streams_never_negative);
    DCHECK(static_cast<size_t>(stream_map().size()) >=
           num_incoming_static_streams() + num_outgoing_static_streams());
    return stream_map().size() - num_incoming_static_streams() -
               num_outgoing_static_streams() >
           0;
  }
  if (static_cast<size_t>(stream_map().size()) >
      num_incoming_static_streams() + num_outgoing_static_streams()) {
    return stream_map().size() - num_incoming_static_streams() -
               num_outgoing_static_streams() >
           0;
  }
  return false;
}

bool QuicSpdySession::ProcessPendingStream(PendingStream* pending) {
  DCHECK(VersionHasStreamType(connection()->transport_version()));
  DCHECK(connection()->connected());
  struct iovec iov;
  if (!pending->sequencer()->GetReadableRegion(&iov)) {
    // The first byte hasn't been received yet.
    return false;
  }

  QuicDataReader reader(static_cast<char*>(iov.iov_base), iov.iov_len);
  uint8_t stream_type_length = reader.PeekVarInt62Length();
  uint64_t stream_type = 0;
  if (!reader.ReadVarInt62(&stream_type)) {
    if (pending->sequencer()->NumBytesBuffered() ==
        pending->sequencer()->close_offset()) {
      // Stream received FIN but there are not enough bytes for stream type.
      // Mark all bytes consumed in order to close stream.
      pending->MarkConsumed(pending->sequencer()->close_offset());
    }
    return false;
  }
  pending->MarkConsumed(stream_type_length);

  switch (stream_type) {
    case kControlStream: {  // HTTP/3 control stream.
      if (receive_control_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("Control");
        return false;
      }
      auto receive_stream = QuicMakeUnique<QuicReceiveControlStream>(pending);
      receive_control_stream_ = receive_stream.get();
      RegisterStaticStream(std::move(receive_stream));
      receive_control_stream_->SetUnblocked();
      QUIC_DVLOG(1) << "Receive Control stream is created";
      return true;
    }
    case kServerPushStream: {  // Push Stream.
      QuicSpdyStream* stream = CreateIncomingStream(pending);
      stream->SetUnblocked();
      return true;
    }
    case kQpackEncoderStream: {  // QPACK encoder stream.
      if (qpack_encoder_receive_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("QPACK encoder");
        return false;
      }
      auto encoder_receive = QuicMakeUnique<QpackReceiveStream>(
          pending, qpack_decoder_->encoder_stream_receiver());
      qpack_encoder_receive_stream_ = encoder_receive.get();
      RegisterStaticStream(std::move(encoder_receive));
      qpack_encoder_receive_stream_->SetUnblocked();
      QUIC_DVLOG(1) << "Receive QPACK Encoder stream is created";
      return true;
    }
    case kQpackDecoderStream: {  // QPACK decoder stream.
      if (qpack_decoder_receive_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("QPACK decoder");
        return false;
      }
      auto decoder_receive = QuicMakeUnique<QpackReceiveStream>(
          pending, qpack_encoder_->decoder_stream_receiver());
      qpack_decoder_receive_stream_ = decoder_receive.get();
      RegisterStaticStream(std::move(decoder_receive));
      qpack_decoder_receive_stream_->SetUnblocked();
      QUIC_DVLOG(1) << "Receive Qpack Decoder stream is created";
      return true;
    }
    default:
      SendStopSending(kHttpUnknownStreamType, pending->id());
      pending->StopReading();
  }
  return false;
}

void QuicSpdySession::MaybeInitializeHttp3UnidirectionalStreams() {
  DCHECK(VersionHasStreamType(connection()->transport_version()));
  if (!send_control_stream_ && CanOpenNextOutgoingUnidirectionalStream()) {
    auto send_control = QuicMakeUnique<QuicSendControlStream>(
        GetNextOutgoingUnidirectionalStreamId(), this,
        qpack_maximum_dynamic_table_capacity_, max_inbound_header_list_size_);
    send_control_stream_ = send_control.get();
    RegisterStaticStream(std::move(send_control));
  }

  if (!qpack_decoder_send_stream_ &&
      CanOpenNextOutgoingUnidirectionalStream()) {
    auto decoder_send = QuicMakeUnique<QpackSendStream>(
        GetNextOutgoingUnidirectionalStreamId(), this, kQpackDecoderStream);
    qpack_decoder_send_stream_ = decoder_send.get();
    RegisterStaticStream(std::move(decoder_send));
    qpack_decoder_->set_qpack_stream_sender_delegate(
        qpack_decoder_send_stream_);
  }

  if (!qpack_encoder_send_stream_ &&
      CanOpenNextOutgoingUnidirectionalStream()) {
    auto encoder_send = QuicMakeUnique<QpackSendStream>(
        GetNextOutgoingUnidirectionalStreamId(), this, kQpackEncoderStream);
    qpack_encoder_send_stream_ = encoder_send.get();
    RegisterStaticStream(std::move(encoder_send));
    qpack_encoder_->set_qpack_stream_sender_delegate(
        qpack_encoder_send_stream_);
  }
}

void QuicSpdySession::OnCanCreateNewOutgoingStream(bool unidirectional) {
  if (unidirectional &&
      VersionHasStreamType(connection()->transport_version())) {
    MaybeInitializeHttp3UnidirectionalStreams();
  }
}

void QuicSpdySession::set_max_allowed_push_id(
    QuicStreamId max_allowed_push_id) {
  if (VersionHasIetfQuicFrames(connection()->transport_version()) &&
      perspective() == Perspective::IS_SERVER &&
      max_allowed_push_id > max_allowed_push_id_) {
    OnCanCreateNewOutgoingStream(true);
  }

  max_allowed_push_id_ = max_allowed_push_id;

  if (VersionHasIetfQuicFrames(connection()->transport_version()) &&
      perspective() == Perspective::IS_CLIENT && IsHandshakeConfirmed()) {
    SendMaxPushId(max_allowed_push_id);
  }
}

void QuicSpdySession::SendMaxPushId(QuicStreamId max_allowed_push_id) {
  DCHECK(VersionHasStreamType(connection()->transport_version()));
  send_control_stream_->SendMaxPushIdFrame(max_allowed_push_id);
}

void QuicSpdySession::CloseConnectionOnDuplicateHttp3UnidirectionalStreams(
    QuicStringPiece type) {
  QUIC_PEER_BUG << QuicStrCat("Received a duplicate ", type,
                              " stream: Closing connection.");
  // TODO(b/124216424): Change to HTTP_STREAM_CREATION_ERROR.
  CloseConnectionWithDetails(QUIC_INVALID_STREAM_ID,
                             QuicStrCat(type, " stream is received twice."));
}

}  // namespace quic
