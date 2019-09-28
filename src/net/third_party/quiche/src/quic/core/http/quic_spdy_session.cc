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
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/spdy/core/http2_frame_decoder_adapter.h"

using http2::Http2DecoderAdapter;
using spdy::HpackEntry;
using spdy::HpackHeaderTable;
using spdy::Http2WeightToSpdy3Priority;
using spdy::SETTINGS_ENABLE_PUSH;
using spdy::SETTINGS_HEADER_TABLE_SIZE;
using spdy::SETTINGS_MAX_HEADER_LIST_SIZE;
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

// TODO(renjietang): remove this once HTTP/3 error codes are adopted.
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
    return &header_list_;
  }

  void OnHeaderFrameEnd(SpdyStreamId /* stream_id */) override {
    if (session_->IsConnected()) {
      session_->OnHeaderList(header_list_);
    }
    header_list_.Clear();
  }

  void OnStreamFrameData(SpdyStreamId /*stream_id*/,
                         const char* /*data*/,
                         size_t /*len*/) override {
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
    CloseConnection("SPDY DATA frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnRstStream(SpdyStreamId /*stream_id*/,
                   SpdyErrorCode /*error_code*/) override {
    CloseConnection("SPDY RST_STREAM frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnSetting(SpdySettingsId id, uint32_t value) override {
    switch (id) {
      case SETTINGS_HEADER_TABLE_SIZE:
        session_->UpdateHeaderEncoderTableSize(value);
        break;
      case SETTINGS_ENABLE_PUSH:
        if (session_->perspective() == Perspective::IS_SERVER) {
          // See rfc7540, Section 6.5.2.
          if (value > 1) {
            CloseConnection(
                QuicStrCat("Invalid value for SETTINGS_ENABLE_PUSH: ", value),
                QUIC_INVALID_HEADERS_STREAM_DATA);
            return;
          }
          session_->UpdateEnableServerPush(value > 0);
          break;
        } else {
          CloseConnection(
              QuicStrCat("Unsupported field of HTTP/2 SETTINGS frame: ", id),
              QUIC_INVALID_HEADERS_STREAM_DATA);
        }
        break;
      // TODO(fayang): Need to support SETTINGS_MAX_HEADER_LIST_SIZE when
      // clients are actually sending it.
      case SETTINGS_MAX_HEADER_LIST_SIZE:
        break;
      default:
        CloseConnection(
            QuicStrCat("Unsupported field of HTTP/2 SETTINGS frame: ", id),
            QUIC_INVALID_HEADERS_STREAM_DATA);
    }
  }

  void OnSettingsEnd() override {}

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
                 SpdyStreamId /*parent_stream_id*/,
                 bool /*exclusive*/,
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

    // TODO(mpw): avoid down-conversion and plumb SpdyStreamPrecedence through
    // QuicHeadersStream.
    SpdyPriority priority =
        has_priority ? Http2WeightToSpdy3Priority(weight) : 0;
    session_->OnHeaders(stream_id, has_priority, priority, fin);
  }

  void OnWindowUpdate(SpdyStreamId /*stream_id*/,
                      int /*delta_window_size*/) override {
    CloseConnection("SPDY WINDOW_UPDATE frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnPushPromise(SpdyStreamId stream_id,
                     SpdyStreamId promised_stream_id,
                     bool /*end*/) override {
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
                  SpdyStreamId /*parent_id*/,
                  int weight,
                  bool /*exclusive*/) override {
    if (session_->connection()->transport_version() <= QUIC_VERSION_39) {
      CloseConnection("SPDY PRIORITY frame received.",
                      QUIC_INVALID_HEADERS_STREAM_DATA);
      return;
    }
    if (!session_->IsConnected()) {
      return;
    }
    // TODO (wangyix): implement real HTTP/2 weights and dependencies instead of
    // converting to SpdyPriority.
    SpdyPriority priority = Http2WeightToSpdy3Priority(weight);
    session_->OnPriority(stream_id, priority);
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
    : QuicSession(connection, visitor, config, supported_versions),
      max_inbound_header_list_size_(kDefaultMaxUncompressedHeaderSize),
      max_outbound_header_list_size_(kDefaultMaxUncompressedHeaderSize),
      server_push_enabled_(true),
      stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      promised_stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      fin_(false),
      frame_len_(0),
      uncompressed_frame_len_(0),
      supports_push_promise_(perspective() == Perspective::IS_CLIENT),
      spdy_framer_(SpdyFramer::ENABLE_COMPRESSION),
      spdy_framer_visitor_(new SpdyFramerVisitor(this)) {
  h2_deframer_.set_visitor(spdy_framer_visitor_.get());
  h2_deframer_.set_debug_visitor(spdy_framer_visitor_.get());
  spdy_framer_.set_debug_visitor(spdy_framer_visitor_.get());
}

QuicSpdySession::~QuicSpdySession() {
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
}

void QuicSpdySession::Initialize() {
  QuicSession::Initialize();

  if (!connection()->version().DoesNotHaveHeadersStream()) {
    if (perspective() == Perspective::IS_SERVER) {
      set_largest_peer_created_stream_id(
          QuicUtils::GetHeadersStreamId(connection()->transport_version()));
    } else {
      QuicStreamId headers_stream_id = GetNextOutgoingBidirectionalStreamId();
      DCHECK_EQ(headers_stream_id, QuicUtils::GetHeadersStreamId(
                                       connection()->transport_version()));
    }
  }

  if (VersionUsesQpack(connection()->transport_version())) {
    qpack_encoder_ =
        QuicMakeUnique<QpackEncoder>(this, &encoder_stream_sender_delegate_);
    qpack_decoder_ =
        QuicMakeUnique<QpackDecoder>(this, &decoder_stream_sender_delegate_);
    // TODO(b/112770235): Send SETTINGS_QPACK_MAX_TABLE_CAPACITY with value
    // kDefaultQpackMaxDynamicTableCapacity.
    qpack_decoder_->SetMaximumDynamicTableCapacity(
        kDefaultQpackMaxDynamicTableCapacity);
  }

  auto headers_stream = QuicMakeUnique<QuicHeadersStream>((this));
  DCHECK_EQ(QuicUtils::GetHeadersStreamId(connection()->transport_version()),
            headers_stream->id());

  headers_stream_ = headers_stream.get();
  RegisterStaticStream(std::move(headers_stream),
                       /*stream_already_counted = */ false);

  if (VersionHasStreamType(connection()->transport_version())) {
    auto send_control = QuicMakeUnique<QuicSendControlStream>(
        GetNextOutgoingUnidirectionalStreamId(), this,
        max_inbound_header_list_size_);
    send_control_stream_ = send_control.get();
    RegisterStaticStream(std::move(send_control),
                         /*stream_already_counted = */ false);
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

void QuicSpdySession::OnStreamHeadersPriority(QuicStreamId stream_id,
                                              SpdyPriority priority) {
  QuicSpdyStream* stream = GetSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive headers after a stream has been reset.
    return;
  }
  stream->OnStreamHeadersPriority(priority);
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

void QuicSpdySession::OnPriorityFrame(QuicStreamId stream_id,
                                      SpdyPriority priority) {
  QuicSpdyStream* stream = GetSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive a PRIORITY frame after a stream has been
    // reset.
    return;
  }
  stream->OnPriorityFrame(priority);
}

size_t QuicSpdySession::ProcessHeaderData(const struct iovec& iov) {
  return h2_deframer_.ProcessInput(static_cast<char*>(iov.iov_base),
                                   iov.iov_len);
}

size_t QuicSpdySession::WriteHeadersOnHeadersStream(
    QuicStreamId id,
    SpdyHeaderBlock headers,
    bool fin,
    SpdyPriority priority,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  DCHECK(!VersionUsesQpack(connection()->transport_version()));

  return WriteHeadersOnHeadersStreamImpl(
      id, std::move(headers), fin,
      /* parent_stream_id = */ 0, Spdy3PriorityToHttp2Weight(priority),
      /* exclusive = */ false, std::move(ack_listener));
}

size_t QuicSpdySession::WritePriority(QuicStreamId id,
                                      QuicStreamId parent_stream_id,
                                      int weight,
                                      bool exclusive) {
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

  send_control_stream_->WritePriority(priority);
}

void QuicSpdySession::WritePushPromise(QuicStreamId original_stream_id,
                                       QuicStreamId promised_stream_id,
                                       SpdyHeaderBlock headers) {
  if (perspective() == Perspective::IS_CLIENT) {
    QUIC_BUG << "Client shouldn't send PUSH_PROMISE";
    return;
  }

  SpdyPushPromiseIR push_promise(original_stream_id, promised_stream_id,
                                 std::move(headers));
  // PUSH_PROMISE must not be the last frame sent out, at least followed by
  // response headers.
  push_promise.set_fin(false);

  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(push_promise));
  headers_stream()->WriteOrBufferData(
      QuicStringPiece(frame.data(), frame.size()), false, nullptr);
}

void QuicSpdySession::SendMaxHeaderListSize(size_t value) {
  if (VersionHasStreamType(connection()->transport_version())) {
    send_control_stream_->SendSettingsFrame();
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
  if (event == HANDSHAKE_CONFIRMED && config()->SupportMaxHeaderListSize()) {
    SendMaxHeaderListSize(max_inbound_header_list_size_);
  }
}

// True if there are open HTTP requests.
bool QuicSpdySession::ShouldKeepConnectionAlive() const {
  // Change to check if there are open HTTP requests.
  // When IETF QUIC control and QPACK streams are used, those will need to be
  // subtracted from this count to ensure only request streams are counted.
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

bool QuicSpdySession::ShouldReleaseHeadersStreamSequencerBuffer() {
  return false;
}

void QuicSpdySession::OnHeaders(SpdyStreamId stream_id,
                                bool has_priority,
                                SpdyPriority priority,
                                bool fin) {
  if (has_priority) {
    if (perspective() == Perspective::IS_CLIENT) {
      CloseConnectionWithDetails(QUIC_INVALID_HEADERS_STREAM_DATA,
                                 "Server must not send priorities.");
      return;
    }
    OnStreamHeadersPriority(stream_id, priority);
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
                                 SpdyPriority priority) {
  if (perspective() == Perspective::IS_CLIENT) {
    CloseConnectionWithDetails(QUIC_INVALID_HEADERS_STREAM_DATA,
                               "Server must not send PRIORITY frames.");
    return;
  }
  OnPriorityFrame(stream_id, priority);
}

void QuicSpdySession::OnHeaderList(const QuicHeaderList& header_list) {
  QUIC_DVLOG(1) << "Received header list for stream " << stream_id_ << ": "
                << header_list.DebugString();
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
  uncompressed_frame_len_ = 0;
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

void QuicSpdySession::UpdateHeaderEncoderTableSize(uint32_t value) {
  spdy_framer_.UpdateHeaderEncoderTableSize(value);
}

void QuicSpdySession::UpdateEnableServerPush(bool value) {
  set_server_push_enabled(value);
}

void QuicSpdySession::CloseConnectionWithDetails(QuicErrorCode error,
                                                 const std::string& details) {
  connection()->CloseConnection(
      error, details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

bool QuicSpdySession::HasActiveRequestStreams() const {
  // In the case where session is destructed by calling
  // stream_map().clear(), we will have incorrect accounting here.
  // TODO(renjietang): Modify destructors and make this a DCHECK.
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
    return false;
  }
  pending->MarkConsumed(stream_type_length);

  switch (stream_type) {
    case kControlStream: {  // HTTP/3 control stream.
      auto receive_stream = QuicMakeUnique<QuicReceiveControlStream>(pending);
      receive_control_stream_ = receive_stream.get();
      RegisterStaticStream(std::move(receive_stream),
                           /*stream_already_counted = */ true);
      receive_control_stream_->SetUnblocked();
      return true;
    }
    case kServerPushStream: {  // Push Stream.
      QuicSpdyStream* stream = CreateIncomingStream(pending);
      stream->SetUnblocked();
      return true;
    }
    case kQpackEncoderStream:  // QPACK encoder stream.
      // TODO(bnc): Create QPACK encoder stream.
      break;
    case kQpackDecoderStream:  // QPACK decoder stream.
      // TODO(bnc): Create QPACK decoder stream.
      break;
    default:
      SendStopSending(kHttpUnknownStreamType, pending->id());
  }
  return false;
}

}  // namespace quic
