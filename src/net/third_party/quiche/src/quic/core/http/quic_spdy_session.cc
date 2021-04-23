// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/http/quic_spdy_session.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quic/core/http/http_constants.h"
#include "quic/core/http/http_decoder.h"
#include "quic/core/http/http_frames.h"
#include "quic/core/http/quic_headers_stream.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "quic/platform/api/quic_exported_stats.h"
#include "quic/platform/api/quic_flag_utils.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_stack_trace.h"
#include "common/platform/api/quiche_text_utils.h"
#include "spdy/core/http2_frame_decoder_adapter.h"

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
using spdy::SpdyStreamId;

namespace quic {

namespace {

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

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

// Class to forward ACCEPT_CH frame to QuicSpdySession,
// and ignore every other frame.
class AlpsFrameDecoder : public HttpDecoder::Visitor {
 public:
  explicit AlpsFrameDecoder(QuicSpdySession* session) : session_(session) {}
  ~AlpsFrameDecoder() override = default;

  // HttpDecoder::Visitor implementation.
  void OnError(HttpDecoder* /*decoder*/) override {}
  bool OnCancelPushFrame(const CancelPushFrame& /*frame*/) override {
    error_detail_ = "CANCEL_PUSH frame forbidden";
    return false;
  }
  bool OnMaxPushIdFrame(const MaxPushIdFrame& /*frame*/) override {
    error_detail_ = "MAX_PUSH_ID frame forbidden";
    return false;
  }
  bool OnGoAwayFrame(const GoAwayFrame& /*frame*/) override {
    error_detail_ = "GOAWAY frame forbidden";
    return false;
  }
  bool OnSettingsFrameStart(QuicByteCount /*header_length*/) override {
    return true;
  }
  bool OnSettingsFrame(const SettingsFrame& /*frame*/) override { return true; }
  bool OnDataFrameStart(QuicByteCount /*header_length*/, QuicByteCount
                        /*payload_length*/) override {
    error_detail_ = "DATA frame forbidden";
    return false;
  }
  bool OnDataFramePayload(absl::string_view /*payload*/) override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnDataFrameEnd() override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnHeadersFrameStart(QuicByteCount /*header_length*/,
                           QuicByteCount /*payload_length*/) override {
    error_detail_ = "HEADERS frame forbidden";
    return false;
  }
  bool OnHeadersFramePayload(absl::string_view /*payload*/) override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnHeadersFrameEnd() override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnPushPromiseFrameStart(QuicByteCount /*header_length*/) override {
    error_detail_ = "PUSH_PROMISE frame forbidden";
    return false;
  }
  bool OnPushPromiseFramePushId(
      PushId /*push_id*/,
      QuicByteCount
      /*push_id_length*/,
      QuicByteCount /*header_block_length*/) override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnPushPromiseFramePayload(absl::string_view /*payload*/) override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnPushPromiseFrameEnd() override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnPriorityUpdateFrameStart(QuicByteCount /*header_length*/) override {
    error_detail_ = "PRIORITY_UPDATE frame forbidden";
    return false;
  }
  bool OnPriorityUpdateFrame(const PriorityUpdateFrame& /*frame*/) override {
    QUICHE_NOTREACHED();
    return false;
  }
  bool OnAcceptChFrameStart(QuicByteCount /*header_length*/) override {
    return true;
  }
  bool OnAcceptChFrame(const AcceptChFrame& frame) override {
    session_->OnAcceptChFrameReceivedViaAlps(frame);
    return true;
  }
  bool OnUnknownFrameStart(uint64_t /*frame_type*/,
                           QuicByteCount
                           /*header_length*/,
                           QuicByteCount /*payload_length*/) override {
    return true;
  }
  bool OnUnknownFramePayload(absl::string_view /*payload*/) override {
    return true;
  }
  bool OnUnknownFrameEnd() override { return true; }

  const absl::optional<std::string>& error_detail() const {
    return error_detail_;
  }

 private:
  QuicSpdySession* const session_;
  absl::optional<std::string> error_detail_;
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
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
    return &header_list_;
  }

  void OnHeaderFrameEnd(SpdyStreamId /* stream_id */) override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));

    LogHeaderCompressionRatioHistogram(
        /* using_qpack = */ false,
        /* is_sent = */ false, header_list_.compressed_header_bytes(),
        header_list_.uncompressed_header_bytes());

    if (session_->IsConnected()) {
      session_->OnHeaderList(header_list_);
    }
    header_list_.Clear();
  }

  void OnStreamFrameData(SpdyStreamId /*stream_id*/,
                         const char* /*data*/,
                         size_t /*len*/) override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
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

  void OnError(Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error) override {
    QuicErrorCode code;
    switch (error) {
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_INDEX_VARINT_ERROR:
        code = QUIC_HPACK_INDEX_VARINT_ERROR;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_NAME_LENGTH_VARINT_ERROR:
        code = QUIC_HPACK_NAME_LENGTH_VARINT_ERROR;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_VALUE_LENGTH_VARINT_ERROR:
        code = QUIC_HPACK_VALUE_LENGTH_VARINT_ERROR;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_NAME_TOO_LONG:
        code = QUIC_HPACK_NAME_TOO_LONG;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_VALUE_TOO_LONG:
        code = QUIC_HPACK_VALUE_TOO_LONG;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_NAME_HUFFMAN_ERROR:
        code = QUIC_HPACK_NAME_HUFFMAN_ERROR;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_VALUE_HUFFMAN_ERROR:
        code = QUIC_HPACK_VALUE_HUFFMAN_ERROR;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE:
        code = QUIC_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_INVALID_INDEX:
        code = QUIC_HPACK_INVALID_INDEX;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_INVALID_NAME_INDEX:
        code = QUIC_HPACK_INVALID_NAME_INDEX;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED:
        code = QUIC_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_INITIAL_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK:
        code = QUIC_HPACK_INITIAL_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING:
        code = QUIC_HPACK_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_TRUNCATED_BLOCK:
        code = QUIC_HPACK_TRUNCATED_BLOCK;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_HPACK_FRAGMENT_TOO_LONG:
        code = QUIC_HPACK_FRAGMENT_TOO_LONG;
        break;
      case Http2DecoderAdapter::SpdyFramerError::
          SPDY_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT:
        code = QUIC_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT;
        break;
      case Http2DecoderAdapter::SpdyFramerError::SPDY_DECOMPRESS_FAILURE:
        code = QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE;
        break;
      default:
        code = QUIC_INVALID_HEADERS_STREAM_DATA;
    }
    CloseConnection(
        absl::StrCat("SPDY framing error: ", detailed_error,
                     Http2DecoderAdapter::SpdyFramerErrorToString(error)),
        code);
  }

  void OnDataFrameHeader(SpdyStreamId /*stream_id*/,
                         size_t /*length*/,
                         bool /*fin*/) override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
    CloseConnection("SPDY DATA frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnRstStream(SpdyStreamId /*stream_id*/,
                   SpdyErrorCode /*error_code*/) override {
    CloseConnection("SPDY RST_STREAM frame received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
  }

  void OnSetting(SpdySettingsId id, uint32_t value) override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
    session_->OnSetting(id, value);
  }

  void OnSettingsEnd() override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
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
                 SpdyStreamId /* parent_stream_id */,
                 bool /* exclusive */,
                 bool fin,
                 bool /*end*/) override {
    if (!session_->IsConnected()) {
      return;
    }

    if (VersionUsesHttp3(session_->transport_version())) {
      CloseConnection("HEADERS frame not allowed on headers stream.",
                      QUIC_INVALID_HEADERS_STREAM_DATA);
      return;
    }

    QUIC_BUG_IF(session_->destruction_indicator() != 123456789)
        << "QuicSpdyStream use after free. "
        << session_->destruction_indicator() << QuicStackTrace();

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
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
    if (session_->perspective() != Perspective::IS_CLIENT) {
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
                  SpdyStreamId /* parent_id */,
                  int weight,
                  bool /* exclusive */) override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
    if (!session_->IsConnected()) {
      return;
    }
    SpdyPriority priority = Http2WeightToSpdy3Priority(weight);
    session_->OnPriority(stream_id, spdy::SpdyStreamPrecedence(priority));
  }

  void OnPriorityUpdate(SpdyStreamId /*prioritized_stream_id*/,
                        absl::string_view /*priority_field_value*/) override {
    // TODO(b/171470299): Parse and call
    // QuicSpdySession::OnPriorityUpdateForRequestStream().
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

  QuicSpdySession* session_;
  QuicHeaderList header_list_;
};

QuicHpackDebugVisitor::QuicHpackDebugVisitor() {}

QuicHpackDebugVisitor::~QuicHpackDebugVisitor() {}

Http3DebugVisitor::Http3DebugVisitor() {}

Http3DebugVisitor::~Http3DebugVisitor() {}

// Expected unidirectional static streams Requirement can be found at
// https://tools.ietf.org/html/draft-ietf-quic-http-22#section-6.2.
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
                  VersionUsesHttp3(connection->transport_version())
                      ? kHttp3StaticUnidirectionalStreamCount
                      : 0),
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
      max_outbound_header_list_size_(std::numeric_limits<size_t>::max()),
      stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      promised_stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      frame_len_(0),
      fin_(false),
      spdy_framer_(SpdyFramer::ENABLE_COMPRESSION),
      spdy_framer_visitor_(new SpdyFramerVisitor(this)),
      debug_visitor_(nullptr),
      destruction_indicator_(123456789),
      server_push_enabled_(true),
      ietf_server_push_enabled_(
          GetQuicFlag(FLAGS_quic_enable_http3_server_push)),
      http3_max_push_id_sent_(false),
      goaway_with_max_stream_id_(
          GetQuicReloadableFlag(quic_goaway_with_max_stream_id)) {
  if (goaway_with_max_stream_id_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_goaway_with_max_stream_id, 1, 2);
  }
  h2_deframer_.set_visitor(spdy_framer_visitor_.get());
  h2_deframer_.set_debug_visitor(spdy_framer_visitor_.get());
  spdy_framer_.set_debug_visitor(spdy_framer_visitor_.get());
}

QuicSpdySession::~QuicSpdySession() {
  QUIC_BUG_IF(destruction_indicator_ != 123456789)
      << "QuicSpdyStream use after free. " << destruction_indicator_
      << QuicStackTrace();
  destruction_indicator_ = 987654321;
}

void QuicSpdySession::Initialize() {
  QuicSession::Initialize();

  FillSettingsFrame();
  if (!VersionUsesHttp3(transport_version())) {
    if (perspective() == Perspective::IS_SERVER) {
      set_largest_peer_created_stream_id(
          QuicUtils::GetHeadersStreamId(transport_version()));
    } else {
      QuicStreamId headers_stream_id = GetNextOutgoingBidirectionalStreamId();
      QUICHE_DCHECK_EQ(headers_stream_id,
                       QuicUtils::GetHeadersStreamId(transport_version()));
    }
    auto headers_stream = std::make_unique<QuicHeadersStream>((this));
    QUICHE_DCHECK_EQ(QuicUtils::GetHeadersStreamId(transport_version()),
                     headers_stream->id());

    headers_stream_ = headers_stream.get();
    ActivateStream(std::move(headers_stream));
  } else {
    qpack_encoder_ = std::make_unique<QpackEncoder>(this);
    qpack_decoder_ =
        std::make_unique<QpackDecoder>(qpack_maximum_dynamic_table_capacity_,
                                       qpack_maximum_blocked_streams_, this);
    MaybeInitializeHttp3UnidirectionalStreams();
  }

  spdy_framer_visitor_->set_max_header_list_size(max_inbound_header_list_size_);

  // Limit HPACK buffering to 2x header list size limit.
  h2_deframer_.GetHpackDecoder()->set_max_decode_buffer_size_bytes(
      2 * max_inbound_header_list_size_);
}

void QuicSpdySession::FillSettingsFrame() {
  settings_.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] =
      qpack_maximum_dynamic_table_capacity_;
  settings_.values[SETTINGS_QPACK_BLOCKED_STREAMS] =
      qpack_maximum_blocked_streams_;
  settings_.values[SETTINGS_MAX_FIELD_SECTION_SIZE] =
      max_inbound_header_list_size_;
}

void QuicSpdySession::OnDecoderStreamError(QuicErrorCode error_code,
                                           absl::string_view error_message) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  CloseConnectionWithDetails(
      error_code, absl::StrCat("Decoder stream error: ", error_message));
}

void QuicSpdySession::OnEncoderStreamError(QuicErrorCode error_code,
                                           absl::string_view error_message) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  CloseConnectionWithDetails(
      error_code, absl::StrCat("Encoder stream error: ", error_message));
}

void QuicSpdySession::OnStreamHeadersPriority(
    QuicStreamId stream_id,
    const spdy::SpdyStreamPrecedence& precedence) {
  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
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
  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
  if (stream == nullptr) {
    // The stream no longer exists, but trailing headers may contain the final
    // byte offset necessary for flow control and open stream accounting.
    size_t final_byte_offset = 0;
    for (const auto& header : header_list) {
      const std::string& header_key = header.first;
      const std::string& header_value = header.second;
      if (header_key == kFinalOffsetHeaderKey) {
        if (!absl::SimpleAtoi(header_value, &final_byte_offset)) {
          connection()->CloseConnection(
              QUIC_INVALID_HEADERS_STREAM_DATA,
              "Trailers are malformed (no final offset)",
              ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
          return;
        }
        QUIC_DVLOG(1) << ENDPOINT
                      << "Received final byte offset in trailers for stream "
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
  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive a PRIORITY frame after a stream has been
    // reset.
    return;
  }
  stream->OnPriorityFrame(precedence);
}

bool QuicSpdySession::OnPriorityUpdateForRequestStream(QuicStreamId stream_id,
                                                       int urgency) {
  if (perspective() == Perspective::IS_CLIENT ||
      !QuicUtils::IsBidirectionalStreamId(stream_id, version()) ||
      !QuicUtils::IsClientInitiatedStreamId(transport_version(), stream_id)) {
    return true;
  }

  QuicStreamCount advertised_max_incoming_bidirectional_streams =
      GetAdvertisedMaxIncomingBidirectionalStreams();
  if (advertised_max_incoming_bidirectional_streams == 0 ||
      stream_id > QuicUtils::GetFirstBidirectionalStreamId(
                      transport_version(), Perspective::IS_CLIENT) +
                      QuicUtils::StreamIdDelta(transport_version()) *
                          (advertised_max_incoming_bidirectional_streams - 1)) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        "PRIORITY_UPDATE frame received for invalid stream.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (MaybeSetStreamPriority(stream_id, spdy::SpdyStreamPrecedence(urgency))) {
    return true;
  }

  if (IsClosedStream(stream_id)) {
    return true;
  }

  buffered_stream_priorities_[stream_id] = urgency;

  if (buffered_stream_priorities_.size() >
      10 * max_open_incoming_bidirectional_streams()) {
    // This should never happen, because |buffered_stream_priorities_| should
    // only contain entries for streams that are allowed to be open by the peer
    // but have not been opened yet.
    std::string error_message =
        absl::StrCat("Too many stream priority values buffered: ",
                     buffered_stream_priorities_.size(),
                     ", which should not exceed the incoming stream limit of ",
                     max_open_incoming_bidirectional_streams());
    QUIC_BUG << error_message;
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, error_message,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  return true;
}

bool QuicSpdySession::OnPriorityUpdateForPushStream(QuicStreamId /*push_id*/,
                                                    int /*urgency*/) {
  // TODO(b/147306124): Implement PRIORITY_UPDATE frames for pushed streams.
  return true;
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
  QUICHE_DCHECK(!VersionUsesHttp3(transport_version()));

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
  QUICHE_DCHECK(!VersionUsesHttp3(transport_version()));
  SpdyPriorityIR priority_frame(id, parent_stream_id, weight, exclusive);
  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(priority_frame));
  headers_stream()->WriteOrBufferData(
      absl::string_view(frame.data(), frame.size()), false, nullptr);
  return frame.size();
}

void QuicSpdySession::WriteHttp3PriorityUpdate(
    const PriorityUpdateFrame& priority_update) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  send_control_stream_->WritePriorityUpdate(priority_update);
}

void QuicSpdySession::OnHttp3GoAway(uint64_t id) {
  QUIC_BUG_IF(!version().UsesHttp3())
      << "HTTP/3 GOAWAY received on version " << version();

  if (last_received_http3_goaway_id_.has_value() &&
      id > last_received_http3_goaway_id_.value()) {
    CloseConnectionWithDetails(
        QUIC_HTTP_GOAWAY_ID_LARGER_THAN_PREVIOUS,
        absl::StrCat("GOAWAY received with ID ", id,
                     " greater than previously received ID ",
                     last_received_http3_goaway_id_.value()));
    return;
  }
  last_received_http3_goaway_id_ = id;

  if (perspective() == Perspective::IS_SERVER) {
    // TODO(b/151749109): Cancel server pushes with push ID larger than |id|.
    return;
  }

  // QuicStreamId is uint32_t.  Casting to this narrower type is well-defined
  // and preserves the lower 32 bits.  Both IsBidirectionalStreamId() and
  // IsIncomingStream() give correct results, because their return value is
  // determined by the least significant two bits.
  QuicStreamId stream_id = static_cast<QuicStreamId>(id);
  if (!QuicUtils::IsBidirectionalStreamId(stream_id, version()) ||
      IsIncomingStream(stream_id)) {
    CloseConnectionWithDetails(QUIC_HTTP_GOAWAY_INVALID_STREAM_ID,
                               "GOAWAY with invalid stream ID");
    return;
  }

  // TODO(b/161252736): Cancel client requests with ID larger than |id|.
  // If |id| is larger than numeric_limits<QuicStreamId>::max(), then use
  // max() instead of downcast value.
}

bool QuicSpdySession::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& frame) {
  if (!QuicSession::OnStreamsBlockedFrame(frame)) {
    return false;
  }

  // The peer asked for stream space more than this implementation has. Send
  // goaway.
  if (perspective() == Perspective::IS_SERVER &&
      frame.stream_count >= QuicUtils::GetMaxStreamCount()) {
    QUICHE_DCHECK_EQ(frame.stream_count, QuicUtils::GetMaxStreamCount());
    SendHttp3GoAway(QUIC_PEER_GOING_AWAY, "stream count too large");
  }
  return true;
}

void QuicSpdySession::SendHttp3GoAway(QuicErrorCode error_code,
                                      const std::string& reason) {
  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  if (GetQuicReloadableFlag(quic_encrypted_goaway)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_encrypted_goaway, 2, 2);
    if (!IsEncryptionEstablished()) {
      QUIC_CODE_COUNT(quic_h3_goaway_before_encryption_established);
      connection()->CloseConnection(
          error_code, reason,
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
  }
  QuicStreamId stream_id;

  if (goaway_with_max_stream_id_) {
    stream_id = QuicUtils::GetMaxClientInitiatedBidirectionalStreamId(
        transport_version());
    if (last_sent_http3_goaway_id_.has_value()) {
      if (last_sent_http3_goaway_id_.value() == stream_id) {
        // Do not send GOAWAY twice.
        return;
      }
      if (last_sent_http3_goaway_id_.value() < stream_id) {
        // A previous GOAWAY frame was sent with smaller stream ID.  This is not
        // possible, because the only time a GOAWAY frame with non-maximal
        // stream ID is sent is right before closing connection.
        QUIC_BUG << "GOAWAY frame with smaller ID already sent.";
        return;
      }
    }
  } else {
    stream_id = GetLargestPeerCreatedStreamId(/*unidirectional = */ false);

    if (stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
      // No client-initiated bidirectional streams received yet.
      // Send 0 to let client know that all requests can be retried.
      stream_id = 0;
    } else {
      // Tell client that streams starting with the next after the largest
      // received one can be retried.
      stream_id += QuicUtils::StreamIdDelta(transport_version());
    }
    if (last_sent_http3_goaway_id_.has_value() &&
        last_sent_http3_goaway_id_.value() <= stream_id) {
      // MUST not send GOAWAY with identifier larger than previously sent.
      // Do not bother sending one with same identifier as before, since
      // GOAWAY frames on the control stream are guaranteed to be processed in
      // order.
      return;
    }
  }

  send_control_stream_->SendGoAway(stream_id);
  last_sent_http3_goaway_id_ = stream_id;
}

void QuicSpdySession::SendHttp3Shutdown() {
  if (goaway_with_max_stream_id_) {
    SendHttp3GoAway(QUIC_PEER_GOING_AWAY, "Server shutdown");
    return;
  }

  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QuicStreamCount advertised_max_incoming_bidirectional_streams =
      GetAdvertisedMaxIncomingBidirectionalStreams();
  const QuicStreamId stream_id =
      QuicUtils::GetFirstBidirectionalStreamId(transport_version(),
                                               Perspective::IS_CLIENT) +
      QuicUtils::StreamIdDelta(transport_version()) *
          advertised_max_incoming_bidirectional_streams;
  if (last_sent_http3_goaway_id_.has_value() &&
      last_sent_http3_goaway_id_.value() < stream_id) {
    send_control_stream_->SendGoAway(last_sent_http3_goaway_id_.value());
    return;
  }
  send_control_stream_->SendGoAway(stream_id);
  last_sent_http3_goaway_id_ = stream_id;
}

void QuicSpdySession::WritePushPromise(QuicStreamId original_stream_id,
                                       QuicStreamId promised_stream_id,
                                       SpdyHeaderBlock headers) {
  if (perspective() == Perspective::IS_CLIENT) {
    QUIC_BUG << "Client shouldn't send PUSH_PROMISE";
    return;
  }

  if (!VersionUsesHttp3(transport_version())) {
    SpdyPushPromiseIR push_promise(original_stream_id, promised_stream_id,
                                   std::move(headers));
    // PUSH_PROMISE must not be the last frame sent out, at least followed by
    // response headers.
    push_promise.set_fin(false);

    SpdySerializedFrame frame(spdy_framer_.SerializeFrame(push_promise));
    headers_stream()->WriteOrBufferData(
        absl::string_view(frame.data(), frame.size()), false, nullptr);
    return;
  }

  if (!max_push_id_.has_value() || promised_stream_id > max_push_id_.value()) {
    QUIC_BUG
        << "Server shouldn't send push id higher than client's MAX_PUSH_ID.";
    return;
  }

  // Encode header list.
  std::string encoded_headers =
      qpack_encoder_->EncodeHeaderList(original_stream_id, headers, nullptr);

  if (debug_visitor_) {
    debug_visitor_->OnPushPromiseFrameSent(original_stream_id,
                                           promised_stream_id, headers);
  }

  PushPromiseFrame frame;
  frame.push_id = promised_stream_id;
  frame.headers = encoded_headers;
  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(original_stream_id);
  stream->WritePushPromise(frame);
}

bool QuicSpdySession::server_push_enabled() const {
  return VersionUsesHttp3(transport_version())
             ? ietf_server_push_enabled_ && max_push_id_.has_value()
             : server_push_enabled_;
}

void QuicSpdySession::SendInitialData() {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  QuicConnection::ScopedPacketFlusher flusher(connection());
  send_control_stream_->MaybeSendSettingsFrame();
  if (perspective() == Perspective::IS_CLIENT && max_push_id_.has_value() &&
      !http3_max_push_id_sent_) {
    SendMaxPushId();
  }
}

QpackEncoder* QuicSpdySession::qpack_encoder() {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  return qpack_encoder_.get();
}

QpackDecoder* QuicSpdySession::qpack_decoder() {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  return qpack_decoder_.get();
}

void QuicSpdySession::OnStreamCreated(QuicSpdyStream* stream) {
  auto it = buffered_stream_priorities_.find(stream->id());
  if (it == buffered_stream_priorities_.end()) {
    return;
  }

  stream->SetPriority(spdy::SpdyStreamPrecedence(it->second));
  buffered_stream_priorities_.erase(it);
}

QuicSpdyStream* QuicSpdySession::GetOrCreateSpdyDataStream(
    const QuicStreamId stream_id) {
  QuicStream* stream = GetOrCreateStream(stream_id);
  if (stream && stream->is_static()) {
    QUIC_BUG << "GetOrCreateSpdyDataStream returns static stream " << stream_id
             << " in version " << transport_version() << "\n"
             << QuicStackTrace();
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        absl::StrCat("stream ", stream_id, " is static"),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return nullptr;
  }
  return static_cast<QuicSpdyStream*>(stream);
}

void QuicSpdySession::OnNewEncryptionKeyAvailable(
    EncryptionLevel level,
    std::unique_ptr<QuicEncrypter> encrypter) {
  QuicSession::OnNewEncryptionKeyAvailable(level, std::move(encrypter));
  if (IsEncryptionEstablished()) {
    // Send H3 SETTINGs once encryption is established.
    SendInitialData();
  }
}

// True if there are open HTTP requests.
bool QuicSpdySession::ShouldKeepConnectionAlive() const {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()) ||
                0u == pending_streams_size());
  return GetNumActiveStreams() + pending_streams_size() > 0;
}

bool QuicSpdySession::UsesPendingStreams() const {
  // QuicSpdySession supports PendingStreams, therefore this method should
  // eventually just return true.  However, pending streams can only be used if
  // unidirectional stream type is supported.
  return VersionUsesHttp3(transport_version());
}

size_t QuicSpdySession::WriteHeadersOnHeadersStreamImpl(
    QuicStreamId id,
    spdy::SpdyHeaderBlock headers,
    bool fin,
    QuicStreamId parent_stream_id,
    int weight,
    bool exclusive,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  QUICHE_DCHECK(!VersionUsesHttp3(transport_version()));

  const QuicByteCount uncompressed_size = headers.TotalBytesUsed();
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
      absl::string_view(frame.data(), frame.size()), false,
      std::move(ack_listener));

  // Calculate compressed header block size without framing overhead.
  QuicByteCount compressed_size = frame.size();
  compressed_size -= spdy::kFrameHeaderSize;
  if (perspective() == Perspective::IS_CLIENT) {
    // Exclusive bit and Stream Dependency are four bytes, weight is one more.
    compressed_size -= 5;
  }

  LogHeaderCompressionRatioHistogram(
      /* using_qpack = */ false,
      /* is_sent = */ true, compressed_size, uncompressed_size);

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

bool QuicSpdySession::ResumeApplicationState(ApplicationState* cached_state) {
  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_CLIENT);
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  SettingsFrame out;
  if (!HttpDecoder::DecodeSettings(
          reinterpret_cast<char*>(cached_state->data()), cached_state->size(),
          &out)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSettingsFrameResumed(out);
  }
  for (const auto& setting : out.values) {
    OnSetting(setting.first, setting.second);
  }
  return true;
}

absl::optional<std::string> QuicSpdySession::OnAlpsData(
    const uint8_t* alps_data,
    size_t alps_length) {
  AlpsFrameDecoder alps_frame_decoder(this);
  HttpDecoder decoder(&alps_frame_decoder);
  decoder.ProcessInput(reinterpret_cast<const char*>(alps_data), alps_length);
  if (alps_frame_decoder.error_detail()) {
    return alps_frame_decoder.error_detail();
  }

  if (decoder.error() != QUIC_NO_ERROR) {
    return decoder.error_detail();
  }

  if (!decoder.AtFrameBoundary()) {
    return "incomplete HTTP/3 frame";
  }

  return absl::nullopt;
}

void QuicSpdySession::OnAcceptChFrameReceivedViaAlps(
    const AcceptChFrame& frame) {
  if (debug_visitor_) {
    debug_visitor_->OnAcceptChFrameReceivedViaAlps(frame);
  }
}

bool QuicSpdySession::OnSettingsFrame(const SettingsFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSettingsFrameReceived(frame);
  }
  for (const auto& setting : frame.values) {
    if (!OnSetting(setting.first, setting.second)) {
      return false;
    }
  }
  return true;
}

bool QuicSpdySession::OnSetting(uint64_t id, uint64_t value) {
  if (VersionUsesHttp3(transport_version())) {
    // SETTINGS frame received on the control stream.
    switch (id) {
      case SETTINGS_QPACK_MAX_TABLE_CAPACITY: {
        QUIC_DVLOG(1)
            << ENDPOINT
            << "SETTINGS_QPACK_MAX_TABLE_CAPACITY received with value "
            << value;
        // Communicate |value| to encoder, because it is used for encoding
        // Required Insert Count.
        bool success = qpack_encoder_->SetMaximumDynamicTableCapacity(value);
        if (GetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2) && !success) {
          CloseConnectionWithDetails(
              was_zero_rtt_rejected()
                  ? QUIC_HTTP_ZERO_RTT_REJECTION_SETTINGS_MISMATCH
                  : QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
              absl::StrCat(was_zero_rtt_rejected()
                               ? "Server rejected 0-RTT, aborting because "
                               : "",
                           "Server sent an SETTINGS_QPACK_MAX_TABLE_CAPACITY: ",
                           value, "while current value is: ",
                           qpack_encoder_->MaximumDynamicTableCapacity()));
          return false;
        }
        // However, limit the dynamic table capacity to
        // |qpack_maximum_dynamic_table_capacity_|.
        qpack_encoder_->SetDynamicTableCapacity(
            std::min(value, qpack_maximum_dynamic_table_capacity_));
        break;
      }
      case SETTINGS_MAX_FIELD_SECTION_SIZE:
        QUIC_DVLOG(1) << ENDPOINT
                      << "SETTINGS_MAX_FIELD_SECTION_SIZE received with value "
                      << value;
        if (GetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2) &&
            max_outbound_header_list_size_ !=
                std::numeric_limits<size_t>::max() &&
            max_outbound_header_list_size_ > value) {
          CloseConnectionWithDetails(
              was_zero_rtt_rejected()
                  ? QUIC_HTTP_ZERO_RTT_REJECTION_SETTINGS_MISMATCH
                  : QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
              absl::StrCat(was_zero_rtt_rejected()
                               ? "Server rejected 0-RTT, aborting because "
                               : "",
                           "Server sent an SETTINGS_MAX_FIELD_SECTION_SIZE: ",
                           value, "which reduces current value: ",
                           max_outbound_header_list_size_));
          return false;
        }
        max_outbound_header_list_size_ = value;
        break;
      case SETTINGS_QPACK_BLOCKED_STREAMS: {
        QUIC_DVLOG(1) << ENDPOINT
                      << "SETTINGS_QPACK_BLOCKED_STREAMS received with value "
                      << value;
        bool success = qpack_encoder_->SetMaximumBlockedStreams(value);
        if (GetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2) && !success) {
          CloseConnectionWithDetails(
              was_zero_rtt_rejected()
                  ? QUIC_HTTP_ZERO_RTT_REJECTION_SETTINGS_MISMATCH
                  : QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
              absl::StrCat(was_zero_rtt_rejected()
                               ? "Server rejected 0-RTT, aborting because "
                               : "",
                           "Server sent an SETTINGS_QPACK_BLOCKED_STREAMS: ",
                           value, "which reduces current value: ",
                           qpack_encoder_->maximum_blocked_streams()));
          return false;
        }
        break;
      }
      case spdy::SETTINGS_ENABLE_PUSH:
        ABSL_FALLTHROUGH_INTENDED;
      case spdy::SETTINGS_MAX_CONCURRENT_STREAMS:
        ABSL_FALLTHROUGH_INTENDED;
      case spdy::SETTINGS_INITIAL_WINDOW_SIZE:
        ABSL_FALLTHROUGH_INTENDED;
      case spdy::SETTINGS_MAX_FRAME_SIZE:
        CloseConnectionWithDetails(
            QUIC_HTTP_RECEIVE_SPDY_SETTING,
            absl::StrCat("received HTTP/2 specific setting in HTTP/3 session: ",
                         id));
        return false;
      default:
        QUIC_DVLOG(1) << ENDPOINT << "Unknown setting identifier " << id
                      << " received with value " << value;
        // Ignore unknown settings.
        break;
    }
    return true;
  }

  // SETTINGS frame received on the headers stream.
  switch (id) {
    case spdy::SETTINGS_HEADER_TABLE_SIZE:
      QUIC_DVLOG(1) << ENDPOINT
                    << "SETTINGS_HEADER_TABLE_SIZE received with value "
                    << value;
      spdy_framer_.UpdateHeaderEncoderTableSize(value);
      break;
    case spdy::SETTINGS_ENABLE_PUSH:
      if (perspective() == Perspective::IS_SERVER) {
        // See rfc7540, Section 6.5.2.
        if (value > 1) {
          QUIC_DLOG(ERROR) << ENDPOINT << "Invalid value " << value
                           << " received for SETTINGS_ENABLE_PUSH.";
          if (IsConnected()) {
            CloseConnectionWithDetails(
                QUIC_INVALID_HEADERS_STREAM_DATA,
                absl::StrCat("Invalid value for SETTINGS_ENABLE_PUSH: ",
                             value));
          }
          return true;
        }
        QUIC_DVLOG(1) << ENDPOINT << "SETTINGS_ENABLE_PUSH received with value "
                      << value;
        server_push_enabled_ = value;
        break;
      } else {
        QUIC_DLOG(ERROR)
            << ENDPOINT
            << "Invalid SETTINGS_ENABLE_PUSH received by client with value "
            << value;
        if (IsConnected()) {
          CloseConnectionWithDetails(
              QUIC_INVALID_HEADERS_STREAM_DATA,
              absl::StrCat("Unsupported field of HTTP/2 SETTINGS frame: ", id));
        }
      }
      break;
    case spdy::SETTINGS_MAX_HEADER_LIST_SIZE:
      QUIC_DVLOG(1) << ENDPOINT
                    << "SETTINGS_MAX_HEADER_LIST_SIZE received with value "
                    << value;
      max_outbound_header_list_size_ = value;
      break;
    default:
      QUIC_DLOG(ERROR) << ENDPOINT << "Unknown setting identifier " << id
                       << " received with value " << value;
      if (IsConnected()) {
        CloseConnectionWithDetails(
            QUIC_INVALID_HEADERS_STREAM_DATA,
            absl::StrCat("Unsupported field of HTTP/2 SETTINGS frame: ", id));
      }
  }
  return true;
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
  QUICHE_DCHECK_EQ(QuicUtils::GetInvalidStreamId(transport_version()),
                   stream_id_);
  QUICHE_DCHECK_EQ(QuicUtils::GetInvalidStreamId(transport_version()),
                   promised_stream_id_);
  stream_id_ = stream_id;
  fin_ = fin;
}

void QuicSpdySession::OnPushPromise(SpdyStreamId stream_id,
                                    SpdyStreamId promised_stream_id) {
  QUICHE_DCHECK_EQ(QuicUtils::GetInvalidStreamId(transport_version()),
                   stream_id_);
  QUICHE_DCHECK_EQ(QuicUtils::GetInvalidStreamId(transport_version()),
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
  QUIC_DVLOG(1) << ENDPOINT << "Received header list for stream " << stream_id_
                << ": " << header_list.DebugString();
  // This code path is only executed for push promise in IETF QUIC.
  if (VersionUsesHttp3(transport_version())) {
    QUICHE_DCHECK(promised_stream_id_ !=
                  QuicUtils::GetInvalidStreamId(transport_version()));
  }
  if (promised_stream_id_ ==
      QuicUtils::GetInvalidStreamId(transport_version())) {
    OnStreamHeaderList(stream_id_, fin_, frame_len_, header_list);
  } else {
    OnPromiseHeaderList(stream_id_, promised_stream_id_, frame_len_,
                        header_list);
  }
  // Reset state for the next frame.
  promised_stream_id_ = QuicUtils::GetInvalidStreamId(transport_version());
  stream_id_ = QuicUtils::GetInvalidStreamId(transport_version());
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
      std::make_unique<HeaderTableDebugVisitor>(
          connection()->helper()->GetClock(), std::move(visitor)));
}

void QuicSpdySession::CloseConnectionWithDetails(QuicErrorCode error,
                                                 const std::string& details) {
  connection()->CloseConnection(
      error, details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

bool QuicSpdySession::HasActiveRequestStreams() const {
  return GetNumActiveStreams() + num_draining_streams() > 0;
}

bool QuicSpdySession::ProcessPendingStream(PendingStream* pending) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK(connection()->connected());
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
      auto receive_stream =
          std::make_unique<QuicReceiveControlStream>(pending, this);
      receive_control_stream_ = receive_stream.get();
      ActivateStream(std::move(receive_stream));
      receive_control_stream_->SetUnblocked();
      QUIC_DVLOG(1) << ENDPOINT << "Receive Control stream is created";
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPeerControlStreamCreated(
            receive_control_stream_->id());
      }
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
      auto encoder_receive = std::make_unique<QpackReceiveStream>(
          pending, this, qpack_decoder_->encoder_stream_receiver());
      qpack_encoder_receive_stream_ = encoder_receive.get();
      ActivateStream(std::move(encoder_receive));
      qpack_encoder_receive_stream_->SetUnblocked();
      QUIC_DVLOG(1) << ENDPOINT << "Receive QPACK Encoder stream is created";
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPeerQpackEncoderStreamCreated(
            qpack_encoder_receive_stream_->id());
      }
      return true;
    }
    case kQpackDecoderStream: {  // QPACK decoder stream.
      if (qpack_decoder_receive_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("QPACK decoder");
        return false;
      }
      auto decoder_receive = std::make_unique<QpackReceiveStream>(
          pending, this, qpack_encoder_->decoder_stream_receiver());
      qpack_decoder_receive_stream_ = decoder_receive.get();
      ActivateStream(std::move(decoder_receive));
      qpack_decoder_receive_stream_->SetUnblocked();
      QUIC_DVLOG(1) << ENDPOINT << "Receive QPACK Decoder stream is created";
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPeerQpackDecoderStreamCreated(
            qpack_decoder_receive_stream_->id());
      }
      return true;
    }
    default:
      if (GetQuicReloadableFlag(quic_unify_stop_sending)) {
        QUIC_RELOADABLE_FLAG_COUNT(quic_unify_stop_sending);
        MaybeSendStopSendingFrame(pending->id(),
                                  QUIC_STREAM_STREAM_CREATION_ERROR);
      } else {
        // TODO(renjietang): deprecate SendStopSending() when the flag is
        // deprecated.
        SendStopSending(QUIC_STREAM_STREAM_CREATION_ERROR, pending->id());
      }
      pending->StopReading();
  }
  return false;
}

void QuicSpdySession::MaybeInitializeHttp3UnidirectionalStreams() {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  if (!send_control_stream_ && CanOpenNextOutgoingUnidirectionalStream()) {
    auto send_control = std::make_unique<QuicSendControlStream>(
        GetNextOutgoingUnidirectionalStreamId(), this, settings_);
    send_control_stream_ = send_control.get();
    ActivateStream(std::move(send_control));
    if (debug_visitor_) {
      debug_visitor_->OnControlStreamCreated(send_control_stream_->id());
    }
  }

  if (!qpack_decoder_send_stream_ &&
      CanOpenNextOutgoingUnidirectionalStream()) {
    auto decoder_send = std::make_unique<QpackSendStream>(
        GetNextOutgoingUnidirectionalStreamId(), this, kQpackDecoderStream);
    qpack_decoder_send_stream_ = decoder_send.get();
    ActivateStream(std::move(decoder_send));
    qpack_decoder_->set_qpack_stream_sender_delegate(
        qpack_decoder_send_stream_);
    if (debug_visitor_) {
      debug_visitor_->OnQpackDecoderStreamCreated(
          qpack_decoder_send_stream_->id());
    }
  }

  if (!qpack_encoder_send_stream_ &&
      CanOpenNextOutgoingUnidirectionalStream()) {
    auto encoder_send = std::make_unique<QpackSendStream>(
        GetNextOutgoingUnidirectionalStreamId(), this, kQpackEncoderStream);
    qpack_encoder_send_stream_ = encoder_send.get();
    ActivateStream(std::move(encoder_send));
    qpack_encoder_->set_qpack_stream_sender_delegate(
        qpack_encoder_send_stream_);
    if (debug_visitor_) {
      debug_visitor_->OnQpackEncoderStreamCreated(
          qpack_encoder_send_stream_->id());
    }
  }
}

void QuicSpdySession::BeforeConnectionCloseSent() {
  if (!GetQuicReloadableFlag(quic_send_goaway_with_connection_close) ||
      !VersionUsesHttp3(transport_version()) || !IsEncryptionEstablished()) {
    return;
  }

  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);

  QUIC_CODE_COUNT(quic_send_goaway_with_connection_close);

  QuicStreamId stream_id =
      GetLargestPeerCreatedStreamId(/*unidirectional = */ false);

  if (stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
    // No client-initiated bidirectional streams received yet.
    // Send 0 to let client know that all requests can be retried.
    stream_id = 0;
  } else {
    // Tell client that streams starting with the next after the largest
    // received one can be retried.
    stream_id += QuicUtils::StreamIdDelta(transport_version());
  }
  if (last_sent_http3_goaway_id_.has_value() &&
      last_sent_http3_goaway_id_.value() <= stream_id) {
    if (goaway_with_max_stream_id_) {
      // A previous GOAWAY frame was sent with smaller stream ID.  This is not
      // possible, because this is the only method sending a GOAWAY frame with
      // non-maximal stream ID, and this must only be called once, right
      // before closing connection.
      QUIC_BUG << "GOAWAY frame with smaller ID already sent.";
    }
    // MUST not send GOAWAY with identifier larger than previously sent.
    // Do not bother sending one with same identifier as before, since GOAWAY
    // frames on the control stream are guaranteed to be processed in order.
    return;
  }

  send_control_stream_->SendGoAway(stream_id);
  last_sent_http3_goaway_id_ = stream_id;
}

void QuicSpdySession::OnCanCreateNewOutgoingStream(bool unidirectional) {
  if (unidirectional && VersionUsesHttp3(transport_version())) {
    MaybeInitializeHttp3UnidirectionalStreams();
  }
}

void QuicSpdySession::SetMaxPushId(PushId max_push_id) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective());
  if (max_push_id_.has_value()) {
    QUICHE_DCHECK_GE(max_push_id, max_push_id_.value());
  }

  if (!max_push_id_.has_value() && max_push_id == 0) {
    // The default max_push_id is 0. So no need to send out MaxPushId frame.
    return;
  }

  ietf_server_push_enabled_ = true;

  if (max_push_id_.has_value()) {
    if (max_push_id == max_push_id_.value()) {
      QUIC_DVLOG(1) << "Not changing max_push_id: " << max_push_id;
      return;
    }

    QUIC_DVLOG(1) << "Setting max_push_id to: " << max_push_id
                  << " from: " << max_push_id_.value();
  } else {
    QUIC_DVLOG(1) << "Setting max_push_id to: " << max_push_id << " from unset";
  }
  max_push_id_ = max_push_id;

  if (IsEncryptionEstablished()) {
    SendMaxPushId();
  }
}

bool QuicSpdySession::OnMaxPushIdFrame(PushId max_push_id) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK_EQ(Perspective::IS_SERVER, perspective());

  if (max_push_id_.has_value()) {
    QUIC_DVLOG(1) << "Setting max_push_id to:  " << max_push_id
                  << " from: " << max_push_id_.value();
  } else {
    QUIC_DVLOG(1) << "Setting max_push_id to:  " << max_push_id
                  << " from unset";
  }
  absl::optional<PushId> old_max_push_id = max_push_id_;
  max_push_id_ = max_push_id;

  if (!old_max_push_id.has_value() || max_push_id > old_max_push_id.value()) {
    OnCanCreateNewOutgoingStream(true);
    return true;
  }

  // Equal value is not considered an error.
  if (max_push_id < old_max_push_id.value()) {
    CloseConnectionWithDetails(
        QUIC_HTTP_INVALID_MAX_PUSH_ID,
        absl::StrCat("MAX_PUSH_ID received with value ", max_push_id,
                     " which is smaller that previously received value ",
                     old_max_push_id.value()));
    return false;
  }

  return true;
}

void QuicSpdySession::SendMaxPushId() {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective());

  send_control_stream_->SendMaxPushIdFrame(max_push_id_.value());
  http3_max_push_id_sent_ = true;
}

void QuicSpdySession::EnableServerPush() {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);

  ietf_server_push_enabled_ = true;
}

bool QuicSpdySession::goaway_received() const {
  return VersionUsesHttp3(transport_version())
             ? last_received_http3_goaway_id_.has_value()
             : transport_goaway_received();
}

bool QuicSpdySession::goaway_sent() const {
  return VersionUsesHttp3(transport_version())
             ? last_sent_http3_goaway_id_.has_value()
             : transport_goaway_sent();
}

bool QuicSpdySession::CanCreatePushStreamWithId(PushId push_id) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  return ietf_server_push_enabled_ && max_push_id_.has_value() &&
         max_push_id_.value() >= push_id;
}

void QuicSpdySession::CloseConnectionOnDuplicateHttp3UnidirectionalStreams(
    absl::string_view type) {
  QUIC_PEER_BUG << absl::StrCat("Received a duplicate ", type,
                                " stream: Closing connection.");
  CloseConnectionWithDetails(QUIC_HTTP_DUPLICATE_UNIDIRECTIONAL_STREAM,
                             absl::StrCat(type, " stream is received twice."));
}

// static
void QuicSpdySession::LogHeaderCompressionRatioHistogram(
    bool using_qpack,
    bool is_sent,
    QuicByteCount compressed,
    QuicByteCount uncompressed) {
  if (compressed <= 0 || uncompressed <= 0) {
    return;
  }

  int ratio = 100 * (compressed) / (uncompressed);
  if (ratio < 1) {
    ratio = 1;
  } else if (ratio > 200) {
    ratio = 200;
  }

  // Note that when using histogram macros in Chromium, the histogram name must
  // be the same across calls for any given call site.
  if (using_qpack) {
    if (is_sent) {
      QUIC_HISTOGRAM_COUNTS("QuicSession.HeaderCompressionRatioQpackSent",
                            ratio, 1, 200, 200,
                            "Header compression ratio as percentage for sent "
                            "headers using QPACK.");
    } else {
      QUIC_HISTOGRAM_COUNTS("QuicSession.HeaderCompressionRatioQpackReceived",
                            ratio, 1, 200, 200,
                            "Header compression ratio as percentage for "
                            "received headers using QPACK.");
    }
  } else {
    if (is_sent) {
      QUIC_HISTOGRAM_COUNTS("QuicSession.HeaderCompressionRatioHpackSent",
                            ratio, 1, 200, 200,
                            "Header compression ratio as percentage for sent "
                            "headers using HPACK.");
    } else {
      QUIC_HISTOGRAM_COUNTS("QuicSession.HeaderCompressionRatioHpackReceived",
                            ratio, 1, 200, 200,
                            "Header compression ratio as percentage for "
                            "received headers using HPACK.");
    }
  }
}

#undef ENDPOINT  // undef for jumbo builds

}  // namespace quic
