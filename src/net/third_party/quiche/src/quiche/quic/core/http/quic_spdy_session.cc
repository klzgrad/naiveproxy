// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_spdy_session.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/http_constants.h"
#include "quiche/quic/core/http/http_decoder.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_headers_stream.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/spdy/core/http2_frame_decoder_adapter.h"

using http2::Http2DecoderAdapter;
using spdy::Http2HeaderBlock;
using spdy::Http2WeightToSpdy3Priority;
using spdy::Spdy3PriorityToHttp2Weight;
using spdy::SpdyErrorCode;
using spdy::SpdyFramer;
using spdy::SpdyFramerDebugVisitorInterface;
using spdy::SpdyFramerVisitorInterface;
using spdy::SpdyFrameType;
using spdy::SpdyHeadersHandlerInterface;
using spdy::SpdyHeadersIR;
using spdy::SpdyPingId;
using spdy::SpdyPriority;
using spdy::SpdyPriorityIR;
using spdy::SpdyPushPromiseIR;
using spdy::SpdySerializedFrame;
using spdy::SpdySettingsId;
using spdy::SpdyStreamId;

namespace quic {

ABSL_CONST_INIT const size_t kMaxUnassociatedWebTransportStreams = 24;

namespace {

// Limit on HPACK encoder dynamic table size.
// Only used for Google QUIC, not IETF QUIC.
constexpr uint64_t kHpackEncoderDynamicTableSizeLimit = 16384;

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

// Class to forward ACCEPT_CH frame to QuicSpdySession,
// and ignore every other frame.
class AlpsFrameDecoder : public HttpDecoder::Visitor {
 public:
  explicit AlpsFrameDecoder(QuicSpdySession* session) : session_(session) {}
  ~AlpsFrameDecoder() override = default;

  // HttpDecoder::Visitor implementation.
  void OnError(HttpDecoder* /*decoder*/) override {}
  bool OnMaxPushIdFrame() override {
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
  bool OnSettingsFrame(const SettingsFrame& frame) override {
    if (settings_frame_received_via_alps_) {
      error_detail_ = "multiple SETTINGS frames";
      return false;
    }

    settings_frame_received_via_alps_ = true;

    error_detail_ = session_->OnSettingsFrameViaAlps(frame);
    return !error_detail_;
  }
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
  void OnWebTransportStreamFrameType(
      QuicByteCount /*header_length*/,
      WebTransportSessionId /*session_id*/) override {
    QUICHE_NOTREACHED();
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

  // True if SETTINGS frame has been received via ALPS.
  bool settings_frame_received_via_alps_ = false;
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

  void OnStreamFrameData(SpdyStreamId /*stream_id*/, const char* /*data*/,
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

  void OnDataFrameHeader(SpdyStreamId /*stream_id*/, size_t /*length*/,
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

  void OnHeaders(SpdyStreamId stream_id, size_t /*payload_length*/,
                 bool has_priority, int weight,
                 SpdyStreamId /*parent_stream_id*/, bool /*exclusive*/,
                 bool fin, bool /*end*/) override {
    if (!session_->IsConnected()) {
      return;
    }

    if (VersionUsesHttp3(session_->transport_version())) {
      CloseConnection("HEADERS frame not allowed on headers stream.",
                      QUIC_INVALID_HEADERS_STREAM_DATA);
      return;
    }

    QUIC_BUG_IF(quic_bug_12477_1,
                session_->destruction_indicator() != 123456789)
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

  void OnPushPromise(SpdyStreamId stream_id, SpdyStreamId promised_stream_id,
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

  void OnContinuation(SpdyStreamId /*stream_id*/, size_t /*payload_size*/,
                      bool /*end*/) override {}

  void OnPriority(SpdyStreamId stream_id, SpdyStreamId /* parent_id */,
                  int weight, bool /* exclusive */) override {
    QUICHE_DCHECK(!VersionUsesHttp3(session_->transport_version()));
    if (!session_->IsConnected()) {
      return;
    }
    SpdyPriority priority = Http2WeightToSpdy3Priority(weight);
    session_->OnPriority(stream_id, spdy::SpdyStreamPrecedence(priority));
  }

  void OnPriorityUpdate(SpdyStreamId /*prioritized_stream_id*/,
                        absl::string_view /*priority_field_value*/) override {}

  bool OnUnknownFrame(SpdyStreamId /*stream_id*/,
                      uint8_t /*frame_type*/) override {
    CloseConnection("Unknown frame type received.",
                    QUIC_INVALID_HEADERS_STREAM_DATA);
    return false;
  }

  void OnUnknownFrameStart(SpdyStreamId /*stream_id*/, size_t /*length*/,
                           uint8_t /*type*/, uint8_t /*flags*/) override {}

  void OnUnknownFramePayload(SpdyStreamId /*stream_id*/,
                             absl::string_view /*payload*/) override {}

  // SpdyFramerDebugVisitorInterface implementation
  void OnSendCompressedFrame(SpdyStreamId /*stream_id*/, SpdyFrameType /*type*/,
                             size_t payload_len, size_t frame_len) override {
    if (payload_len == 0) {
      QUIC_BUG(quic_bug_10360_1) << "Zero payload length.";
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

Http3DebugVisitor::Http3DebugVisitor() {}

Http3DebugVisitor::~Http3DebugVisitor() {}

// Expected unidirectional static streams Requirement can be found at
// https://tools.ietf.org/html/draft-ietf-quic-http-22#section-6.2.
QuicSpdySession::QuicSpdySession(
    QuicConnection* connection, QuicSession::Visitor* visitor,
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions)
    : QuicSession(connection, visitor, config, supported_versions,
                  /*num_expected_unidirectional_static_streams = */
                  VersionUsesHttp3(connection->transport_version())
                      ? static_cast<QuicStreamCount>(
                            kHttp3StaticUnidirectionalStreamCount)
                      : 0u,
                  std::make_unique<DatagramObserver>(this)),
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
      allow_extended_connect_(
          GetQuicReloadableFlag(quic_verify_request_headers_2) &&
          perspective() == Perspective::IS_SERVER &&
          VersionUsesHttp3(transport_version())) {
  h2_deframer_.set_visitor(spdy_framer_visitor_.get());
  h2_deframer_.set_debug_visitor(spdy_framer_visitor_.get());
  spdy_framer_.set_debug_visitor(spdy_framer_visitor_.get());
}

QuicSpdySession::~QuicSpdySession() {
  QUIC_BUG_IF(quic_bug_12477_2, destruction_indicator_ != 123456789)
      << "QuicSpdySession use after free. " << destruction_indicator_
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
  if (version().UsesHttp3()) {
    switch (LocalHttpDatagramSupport()) {
      case HttpDatagramSupport::kNone:
        break;
      case HttpDatagramSupport::kDraft04:
        settings_.values[SETTINGS_H3_DATAGRAM_DRAFT04] = 1;
        break;
      case HttpDatagramSupport::kRfc:
        settings_.values[SETTINGS_H3_DATAGRAM] = 1;
        break;
      case HttpDatagramSupport::kRfcAndDraft04:
        settings_.values[SETTINGS_H3_DATAGRAM] = 1;
        settings_.values[SETTINGS_H3_DATAGRAM_DRAFT04] = 1;
        break;
    }
  }
  if (WillNegotiateWebTransport()) {
    settings_.values[SETTINGS_WEBTRANS_DRAFT00] = 1;
  }
  if (allow_extended_connect()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_verify_request_headers_2, 1, 3);
    settings_.values[SETTINGS_ENABLE_CONNECT_PROTOCOL] = 1;
  }
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
    QuicStreamId stream_id, const spdy::SpdyStreamPrecedence& precedence) {
  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive headers after a stream has been reset.
    return;
  }
  stream->OnStreamHeadersPriority(precedence);
}

void QuicSpdySession::OnStreamHeaderList(QuicStreamId stream_id, bool fin,
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
    QuicStreamId stream_id, const spdy::SpdyStreamPrecedence& precedence) {
  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive a PRIORITY frame after a stream has been
    // reset.
    return;
  }
  stream->OnPriorityFrame(precedence);
}

bool QuicSpdySession::OnPriorityUpdateForRequestStream(
    QuicStreamId stream_id, QuicStreamPriority priority) {
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

  if (MaybeSetStreamPriority(stream_id, priority)) {
    return true;
  }

  if (IsClosedStream(stream_id)) {
    return true;
  }

  buffered_stream_priorities_[stream_id] = priority;

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
    QUIC_BUG(quic_bug_10360_2) << error_message;
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, error_message,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  return true;
}

size_t QuicSpdySession::ProcessHeaderData(const struct iovec& iov) {
  QUIC_BUG_IF(quic_bug_12477_4, destruction_indicator_ != 123456789)
      << "QuicSpdyStream use after free. " << destruction_indicator_
      << QuicStackTrace();
  return h2_deframer_.ProcessInput(static_cast<char*>(iov.iov_base),
                                   iov.iov_len);
}

size_t QuicSpdySession::WriteHeadersOnHeadersStream(
    QuicStreamId id, Http2HeaderBlock headers, bool fin,
    const spdy::SpdyStreamPrecedence& precedence,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
  QUICHE_DCHECK(!VersionUsesHttp3(transport_version()));

  return WriteHeadersOnHeadersStreamImpl(
      id, std::move(headers), fin,
      /* parent_stream_id = */ 0,
      Spdy3PriorityToHttp2Weight(precedence.spdy3_priority()),
      /* exclusive = */ false, std::move(ack_listener));
}

size_t QuicSpdySession::WritePriority(QuicStreamId stream_id,
                                      QuicStreamId parent_stream_id, int weight,
                                      bool exclusive) {
  QUICHE_DCHECK(!VersionUsesHttp3(transport_version()));
  SpdyPriorityIR priority_frame(stream_id, parent_stream_id, weight, exclusive);
  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(priority_frame));
  headers_stream()->WriteOrBufferData(
      absl::string_view(frame.data(), frame.size()), false, nullptr);
  return frame.size();
}

void QuicSpdySession::WriteHttp3PriorityUpdate(QuicStreamId stream_id,
                                               QuicStreamPriority priority) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  send_control_stream_->WritePriorityUpdate(stream_id, priority);
}

void QuicSpdySession::OnHttp3GoAway(uint64_t id) {
  QUIC_BUG_IF(quic_bug_12477_5, !version().UsesHttp3())
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
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  if (!IsEncryptionEstablished()) {
    QUIC_CODE_COUNT(quic_h3_goaway_before_encryption_established);
    connection()->CloseConnection(
        error_code, reason,
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  QuicStreamId stream_id;

  stream_id = QuicUtils::GetMaxClientInitiatedBidirectionalStreamId(
      transport_version());
  if (last_sent_http3_goaway_id_.has_value() &&
      last_sent_http3_goaway_id_.value() <= stream_id) {
    // Do not send GOAWAY frame with a higher id, because it is forbidden.
    // Do not send one with same stream id as before, since frames on the
    // control stream are guaranteed to be processed in order.
    return;
  }

  send_control_stream_->SendGoAway(stream_id);
  last_sent_http3_goaway_id_ = stream_id;
}

void QuicSpdySession::WritePushPromise(QuicStreamId original_stream_id,
                                       QuicStreamId promised_stream_id,
                                       Http2HeaderBlock headers) {
  if (perspective() == Perspective::IS_CLIENT) {
    QUIC_BUG(quic_bug_10360_4) << "Client shouldn't send PUSH_PROMISE";
    return;
  }

  if (VersionUsesHttp3(transport_version())) {
    QUIC_BUG(quic_bug_12477_6)
        << "Support for server push over HTTP/3 has been removed.";
    return;
  }

  SpdyPushPromiseIR push_promise(original_stream_id, promised_stream_id,
                                 std::move(headers));
  // PUSH_PROMISE must not be the last frame sent out, at least followed by
  // response headers.
  push_promise.set_fin(false);

  SpdySerializedFrame frame(spdy_framer_.SerializeFrame(push_promise));
  headers_stream()->WriteOrBufferData(
      absl::string_view(frame.data(), frame.size()), false, nullptr);
}

void QuicSpdySession::SendInitialData() {
  if (!VersionUsesHttp3(transport_version())) {
    return;
  }
  QuicConnection::ScopedPacketFlusher flusher(connection());
  send_control_stream_->MaybeSendSettingsFrame();
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

  stream->SetPriority(it->second);
  buffered_stream_priorities_.erase(it);
}

QuicSpdyStream* QuicSpdySession::GetOrCreateSpdyDataStream(
    const QuicStreamId stream_id) {
  QuicStream* stream = GetOrCreateStream(stream_id);
  if (stream && stream->is_static()) {
    QUIC_BUG(quic_bug_10360_5)
        << "GetOrCreateSpdyDataStream returns static stream " << stream_id
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
    EncryptionLevel level, std::unique_ptr<QuicEncrypter> encrypter) {
  QuicSession::OnNewEncryptionKeyAvailable(level, std::move(encrypter));
  if (IsEncryptionEstablished()) {
    // Send H3 SETTINGs once encryption is established.
    SendInitialData();
  }
}

bool QuicSpdySession::ShouldNegotiateWebTransport() { return false; }

bool QuicSpdySession::ShouldValidateWebTransportVersion() const { return true; }

bool QuicSpdySession::WillNegotiateWebTransport() {
  return LocalHttpDatagramSupport() != HttpDatagramSupport::kNone &&
         version().UsesHttp3() && ShouldNegotiateWebTransport();
}

// True if there are open HTTP requests.
bool QuicSpdySession::ShouldKeepConnectionAlive() const {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()) ||
                0u == pending_streams_size());
  return GetNumActiveStreams() + pending_streams_size() > 0;
}

bool QuicSpdySession::UsesPendingStreamForFrame(QuicFrameType type,
                                                QuicStreamId stream_id) const {
  // Pending streams can only be used to handle unidirectional stream with
  // STREAM & RESET_STREAM frames in IETF QUIC.
  return VersionUsesHttp3(transport_version()) &&
         (type == STREAM_FRAME || type == RST_STREAM_FRAME) &&
         QuicUtils::GetStreamType(stream_id, perspective(),
                                  IsIncomingStream(stream_id),
                                  version()) == READ_UNIDIRECTIONAL;
}

size_t QuicSpdySession::WriteHeadersOnHeadersStreamImpl(
    QuicStreamId id, spdy::Http2HeaderBlock headers, bool fin,
    QuicStreamId parent_stream_id, int weight, bool exclusive,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
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
    QuicStreamId /*stream_id*/, QuicStreamId /*promised_stream_id*/,
    size_t /*frame_len*/, const QuicHeaderList& /*header_list*/) {
  std::string error =
      "OnPromiseHeaderList should be overridden in client code.";
  QUIC_BUG(quic_bug_10360_6) << error;
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
  QUICHE_DCHECK(streams_waiting_for_settings_.empty());
  for (const auto& setting : out.values) {
    OnSetting(setting.first, setting.second);
  }
  return true;
}

absl::optional<std::string> QuicSpdySession::OnAlpsData(
    const uint8_t* alps_data, size_t alps_length) {
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
  for (QuicStreamId stream_id : streams_waiting_for_settings_) {
    QUICHE_DCHECK(ShouldBufferRequestsUntilSettings());
    QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
    if (stream == nullptr) {
      // The stream may no longer exist, since it is possible for a stream to
      // get reset while waiting for the SETTINGS frame.
      continue;
    }
    stream->OnDataAvailable();
  }
  streams_waiting_for_settings_.clear();
  return true;
}

absl::optional<std::string> QuicSpdySession::OnSettingsFrameViaAlps(
    const SettingsFrame& frame) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSettingsFrameReceivedViaAlps(frame);
  }
  for (const auto& setting : frame.values) {
    if (!OnSetting(setting.first, setting.second)) {
      // Do not bother adding the setting identifier or value to the error
      // message, because OnSetting() already closed the connection, therefore
      // the error message will be ignored.
      return "error parsing setting";
    }
  }
  return absl::nullopt;
}

bool QuicSpdySession::VerifySettingIsZeroOrOne(uint64_t id, uint64_t value) {
  if (value == 0 || value == 1) {
    return true;
  }
  std::string error_details = absl::StrCat(
      "Received ",
      H3SettingsToString(static_cast<Http3AndQpackSettingsIdentifiers>(id)),
      " with invalid value ", value);
  QUIC_PEER_BUG(bad received setting) << ENDPOINT << error_details;
  CloseConnectionWithDetails(QUIC_HTTP_INVALID_SETTING_VALUE, error_details);
  return false;
}

bool QuicSpdySession::OnSetting(uint64_t id, uint64_t value) {
  any_settings_received_ = true;

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
        if (!qpack_encoder_->SetMaximumDynamicTableCapacity(value)) {
          CloseConnectionWithDetails(
              was_zero_rtt_rejected()
                  ? QUIC_HTTP_ZERO_RTT_REJECTION_SETTINGS_MISMATCH
                  : QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
              absl::StrCat(was_zero_rtt_rejected()
                               ? "Server rejected 0-RTT, aborting because "
                               : "",
                           "Server sent an SETTINGS_QPACK_MAX_TABLE_CAPACITY: ",
                           value, " while current value is: ",
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
        if (max_outbound_header_list_size_ !=
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
                           value, " which reduces current value: ",
                           max_outbound_header_list_size_));
          return false;
        }
        max_outbound_header_list_size_ = value;
        break;
      case SETTINGS_QPACK_BLOCKED_STREAMS: {
        QUIC_DVLOG(1) << ENDPOINT
                      << "SETTINGS_QPACK_BLOCKED_STREAMS received with value "
                      << value;
        if (!qpack_encoder_->SetMaximumBlockedStreams(value)) {
          CloseConnectionWithDetails(
              was_zero_rtt_rejected()
                  ? QUIC_HTTP_ZERO_RTT_REJECTION_SETTINGS_MISMATCH
                  : QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
              absl::StrCat(was_zero_rtt_rejected()
                               ? "Server rejected 0-RTT, aborting because "
                               : "",
                           "Server sent an SETTINGS_QPACK_BLOCKED_STREAMS: ",
                           value, " which reduces current value: ",
                           qpack_encoder_->maximum_blocked_streams()));
          return false;
        }
        break;
      }
      case SETTINGS_ENABLE_CONNECT_PROTOCOL: {
        QUIC_DVLOG(1) << ENDPOINT
                      << "SETTINGS_ENABLE_CONNECT_PROTOCOL received with value "
                      << value;
        if (!VerifySettingIsZeroOrOne(id, value)) {
          return false;
        }
        if (perspective() == Perspective::IS_CLIENT) {
          allow_extended_connect_ = value != 0;
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
      case SETTINGS_H3_DATAGRAM_DRAFT04: {
        HttpDatagramSupport local_http_datagram_support =
            LocalHttpDatagramSupport();
        if (local_http_datagram_support != HttpDatagramSupport::kDraft04 &&
            local_http_datagram_support !=
                HttpDatagramSupport::kRfcAndDraft04) {
          break;
        }
        QUIC_DVLOG(1) << ENDPOINT
                      << "SETTINGS_H3_DATAGRAM_DRAFT04 received with value "
                      << value;
        if (!version().UsesHttp3()) {
          break;
        }
        if (!VerifySettingIsZeroOrOne(id, value)) {
          return false;
        }
        if (value && http_datagram_support_ != HttpDatagramSupport::kRfc) {
          // If both RFC 9297 and draft-04 are supported, we use the RFC. This
          // is implemented by ignoring SETTINGS_H3_DATAGRAM_DRAFT04 when we've
          // already parsed SETTINGS_H3_DATAGRAM.
          http_datagram_support_ = HttpDatagramSupport::kDraft04;
        }
        break;
      }
      case SETTINGS_H3_DATAGRAM: {
        HttpDatagramSupport local_http_datagram_support =
            LocalHttpDatagramSupport();
        if (local_http_datagram_support != HttpDatagramSupport::kRfc &&
            local_http_datagram_support !=
                HttpDatagramSupport::kRfcAndDraft04) {
          break;
        }
        QUIC_DVLOG(1) << ENDPOINT << "SETTINGS_H3_DATAGRAM received with value "
                      << value;
        if (!version().UsesHttp3()) {
          break;
        }
        if (!VerifySettingIsZeroOrOne(id, value)) {
          return false;
        }
        if (value) {
          http_datagram_support_ = HttpDatagramSupport::kRfc;
        }
        break;
      }
      case SETTINGS_WEBTRANS_DRAFT00:
        if (!WillNegotiateWebTransport()) {
          break;
        }
        QUIC_DVLOG(1) << ENDPOINT
                      << "SETTINGS_ENABLE_WEBTRANSPORT received with value "
                      << value;
        if (!VerifySettingIsZeroOrOne(id, value)) {
          return false;
        }
        peer_supports_webtransport_ = (value == 1);
        if (perspective() == Perspective::IS_CLIENT && value == 1) {
          allow_extended_connect_ = true;
        }
        break;
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
      spdy_framer_.UpdateHeaderEncoderTableSize(
          std::min<uint64_t>(value, kHpackEncoderDynamicTableSizeLimit));
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
                      << value << ", ignoring.";
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

void QuicSpdySession::OnHeaders(SpdyStreamId stream_id, bool has_priority,
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

void QuicSpdySession::CloseConnectionWithDetails(QuicErrorCode error,
                                                 const std::string& details) {
  connection()->CloseConnection(
      error, details, ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

bool QuicSpdySession::HasActiveRequestStreams() const {
  return GetNumActiveStreams() + num_draining_streams() > 0;
}

QuicStream* QuicSpdySession::ProcessPendingStream(PendingStream* pending) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK(connection()->connected());
  struct iovec iov;
  if (!pending->sequencer()->GetReadableRegion(&iov)) {
    // The first byte hasn't been received yet.
    return nullptr;
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
    return nullptr;
  }
  pending->MarkConsumed(stream_type_length);

  switch (stream_type) {
    case kControlStream: {  // HTTP/3 control stream.
      if (receive_control_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("Control");
        return nullptr;
      }
      auto receive_stream =
          std::make_unique<QuicReceiveControlStream>(pending, this);
      receive_control_stream_ = receive_stream.get();
      ActivateStream(std::move(receive_stream));
      QUIC_DVLOG(1) << ENDPOINT << "Receive Control stream is created";
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPeerControlStreamCreated(
            receive_control_stream_->id());
      }
      return receive_control_stream_;
    }
    case kServerPushStream: {  // Push Stream.
      CloseConnectionWithDetails(QUIC_HTTP_RECEIVE_SERVER_PUSH,
                                 "Received server push stream");
      return nullptr;
    }
    case kQpackEncoderStream: {  // QPACK encoder stream.
      if (qpack_encoder_receive_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("QPACK encoder");
        return nullptr;
      }
      auto encoder_receive = std::make_unique<QpackReceiveStream>(
          pending, this, qpack_decoder_->encoder_stream_receiver());
      qpack_encoder_receive_stream_ = encoder_receive.get();
      ActivateStream(std::move(encoder_receive));
      QUIC_DVLOG(1) << ENDPOINT << "Receive QPACK Encoder stream is created";
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPeerQpackEncoderStreamCreated(
            qpack_encoder_receive_stream_->id());
      }
      return qpack_encoder_receive_stream_;
    }
    case kQpackDecoderStream: {  // QPACK decoder stream.
      if (qpack_decoder_receive_stream_) {
        CloseConnectionOnDuplicateHttp3UnidirectionalStreams("QPACK decoder");
        return nullptr;
      }
      auto decoder_receive = std::make_unique<QpackReceiveStream>(
          pending, this, qpack_encoder_->decoder_stream_receiver());
      qpack_decoder_receive_stream_ = decoder_receive.get();
      ActivateStream(std::move(decoder_receive));
      QUIC_DVLOG(1) << ENDPOINT << "Receive QPACK Decoder stream is created";
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPeerQpackDecoderStreamCreated(
            qpack_decoder_receive_stream_->id());
      }
      return qpack_decoder_receive_stream_;
    }
    case kWebTransportUnidirectionalStream: {
      // Note that this checks whether WebTransport is enabled on the receiver
      // side, as we may receive WebTransport streams before peer's SETTINGS are
      // received.
      // TODO(b/184156476): consider whether this means we should drop buffered
      // streams if we don't receive indication of WebTransport support.
      if (!WillNegotiateWebTransport()) {
        // Treat as unknown stream type.
        break;
      }
      QUIC_DVLOG(1) << ENDPOINT << "Created an incoming WebTransport stream "
                    << pending->id();
      auto stream_owned =
          std::make_unique<WebTransportHttp3UnidirectionalStream>(pending,
                                                                  this);
      WebTransportHttp3UnidirectionalStream* stream = stream_owned.get();
      ActivateStream(std::move(stream_owned));
      return stream;
    }
    default:
      break;
  }
  MaybeSendStopSendingFrame(
      pending->id(),
      QuicResetStreamError::FromInternal(QUIC_STREAM_STREAM_CREATION_ERROR));
  pending->StopReading();
  return nullptr;
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
  if (!VersionUsesHttp3(transport_version()) || !IsEncryptionEstablished()) {
    return;
  }

  QUICHE_DCHECK_EQ(perspective(), Perspective::IS_SERVER);

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
    // Do not send GOAWAY frame with a higher id, because it is forbidden.
    // Do not send one with same stream id as before, since frames on the
    // control stream are guaranteed to be processed in order.
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

void QuicSpdySession::CloseConnectionOnDuplicateHttp3UnidirectionalStreams(
    absl::string_view type) {
  QUIC_PEER_BUG(quic_peer_bug_10360_9) << absl::StrCat(
      "Received a duplicate ", type, " stream: Closing connection.");
  CloseConnectionWithDetails(QUIC_HTTP_DUPLICATE_UNIDIRECTIONAL_STREAM,
                             absl::StrCat(type, " stream is received twice."));
}

// static
void QuicSpdySession::LogHeaderCompressionRatioHistogram(
    bool using_qpack, bool is_sent, QuicByteCount compressed,
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

MessageStatus QuicSpdySession::SendHttp3Datagram(QuicStreamId stream_id,
                                                 absl::string_view payload) {
  if (!SupportsH3Datagram()) {
    QUIC_BUG(send http datagram too early)
        << "Refusing to send HTTP Datagram before SETTINGS received";
    return MESSAGE_STATUS_INTERNAL_ERROR;
  }
  // Stream ID is sent divided by four as per the specification.
  uint64_t stream_id_to_write = stream_id / kHttpDatagramStreamIdDivisor;
  size_t slice_length =
      QuicDataWriter::GetVarInt62Len(stream_id_to_write) + payload.length();
  quiche::QuicheBuffer buffer(
      connection()->helper()->GetStreamSendBufferAllocator(), slice_length);
  QuicDataWriter writer(slice_length, buffer.data());
  if (!writer.WriteVarInt62(stream_id_to_write)) {
    QUIC_BUG(h3 datagram stream ID write fail)
        << "Failed to write HTTP/3 datagram stream ID";
    return MESSAGE_STATUS_INTERNAL_ERROR;
  }
  if (!writer.WriteBytes(payload.data(), payload.length())) {
    QUIC_BUG(h3 datagram payload write fail)
        << "Failed to write HTTP/3 datagram payload";
    return MESSAGE_STATUS_INTERNAL_ERROR;
  }

  quiche::QuicheMemSlice slice(std::move(buffer));
  return datagram_queue()->SendOrQueueDatagram(std::move(slice));
}

void QuicSpdySession::SetMaxDatagramTimeInQueueForStreamId(
    QuicStreamId /*stream_id*/, QuicTime::Delta max_time_in_queue) {
  // TODO(b/184598230): implement this in a way that works for multiple sessions
  // on a same connection.
  datagram_queue()->SetMaxTimeInQueue(max_time_in_queue);
}

void QuicSpdySession::OnMessageReceived(absl::string_view message) {
  QuicSession::OnMessageReceived(message);
  if (!SupportsH3Datagram()) {
    QUIC_DLOG(INFO) << "Ignoring unexpected received HTTP/3 datagram";
    return;
  }
  QuicDataReader reader(message);
  uint64_t stream_id64;
  if (!reader.ReadVarInt62(&stream_id64)) {
    QUIC_DLOG(ERROR) << "Failed to parse stream ID in received HTTP/3 datagram";
    return;
  }
  // Stream ID is sent divided by four as per the specification.
  if (stream_id64 >
      std::numeric_limits<QuicStreamId>::max() / kHttpDatagramStreamIdDivisor) {
    CloseConnectionWithDetails(
        QUIC_HTTP_FRAME_ERROR,
        absl::StrCat("Received HTTP Datagram with invalid quarter stream ID ",
                     stream_id64));
    return;
  }
  stream_id64 *= kHttpDatagramStreamIdDivisor;
  QuicStreamId stream_id = static_cast<QuicStreamId>(stream_id64);
  QuicSpdyStream* stream =
      static_cast<QuicSpdyStream*>(GetActiveStream(stream_id));
  if (stream == nullptr) {
    QUIC_DLOG(INFO) << "Received HTTP/3 datagram for unknown stream ID "
                    << stream_id;
    // TODO(b/181256914) buffer HTTP/3 datagrams with unknown stream IDs for a
    // short period of time in case they were reordered.
    return;
  }
  stream->OnDatagramReceived(&reader);
}

bool QuicSpdySession::SupportsWebTransport() {
  return WillNegotiateWebTransport() && SupportsH3Datagram() &&
         peer_supports_webtransport_ &&
         (!GetQuicReloadableFlag(quic_verify_request_headers_2) ||
          allow_extended_connect_);
}

bool QuicSpdySession::SupportsH3Datagram() const {
  return http_datagram_support_ != HttpDatagramSupport::kNone;
}

WebTransportHttp3* QuicSpdySession::GetWebTransportSession(
    WebTransportSessionId id) {
  if (!SupportsWebTransport()) {
    return nullptr;
  }
  if (!IsValidWebTransportSessionId(id, version())) {
    return nullptr;
  }
  QuicSpdyStream* connect_stream = GetOrCreateSpdyDataStream(id);
  if (connect_stream == nullptr) {
    return nullptr;
  }
  return connect_stream->web_transport();
}

bool QuicSpdySession::ShouldProcessIncomingRequests() {
  if (!ShouldBufferRequestsUntilSettings()) {
    return true;
  }

  return any_settings_received_;
}

void QuicSpdySession::OnStreamWaitingForClientSettings(QuicStreamId id) {
  QUICHE_DCHECK(ShouldBufferRequestsUntilSettings());
  QUICHE_DCHECK(QuicUtils::IsBidirectionalStreamId(id, version()));
  streams_waiting_for_settings_.insert(id);
}

void QuicSpdySession::AssociateIncomingWebTransportStreamWithSession(
    WebTransportSessionId session_id, QuicStreamId stream_id) {
  if (QuicUtils::IsOutgoingStreamId(version(), stream_id, perspective())) {
    QUIC_BUG(AssociateIncomingWebTransportStreamWithSession got outgoing stream)
        << ENDPOINT
        << "AssociateIncomingWebTransportStreamWithSession() got an outgoing "
           "stream ID: "
        << stream_id;
    return;
  }
  WebTransportHttp3* session = GetWebTransportSession(session_id);
  if (session != nullptr) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Successfully associated incoming WebTransport stream "
                  << stream_id << " with session ID " << session_id;

    session->AssociateStream(stream_id);
    return;
  }
  // Evict the oldest streams until we are under the limit.
  while (buffered_streams_.size() >= kMaxUnassociatedWebTransportStreams) {
    QUIC_DVLOG(1) << ENDPOINT << "Removing stream "
                  << buffered_streams_.front().stream_id
                  << " from buffered streams as the queue is full.";
    ResetStream(buffered_streams_.front().stream_id,
                QUIC_STREAM_WEBTRANSPORT_BUFFERED_STREAMS_LIMIT_EXCEEDED);
    buffered_streams_.pop_front();
  }
  QUIC_DVLOG(1) << ENDPOINT << "Received a WebTransport stream " << stream_id
                << " for session ID " << session_id
                << " but cannot associate it; buffering instead.";
  buffered_streams_.push_back(
      BufferedWebTransportStream{session_id, stream_id});
}

void QuicSpdySession::ProcessBufferedWebTransportStreamsForSession(
    WebTransportHttp3* session) {
  const WebTransportSessionId session_id = session->id();
  QUIC_DVLOG(1) << "Processing buffered WebTransport streams for "
                << session_id;
  auto it = buffered_streams_.begin();
  while (it != buffered_streams_.end()) {
    if (it->session_id == session_id) {
      QUIC_DVLOG(1) << "Unbuffered and associated WebTransport stream "
                    << it->stream_id << " with session " << it->session_id;
      session->AssociateStream(it->stream_id);
      it = buffered_streams_.erase(it);
    } else {
      it++;
    }
  }
}

WebTransportHttp3UnidirectionalStream*
QuicSpdySession::CreateOutgoingUnidirectionalWebTransportStream(
    WebTransportHttp3* session) {
  if (!CanOpenNextOutgoingUnidirectionalStream()) {
    return nullptr;
  }

  QuicStreamId stream_id = GetNextOutgoingUnidirectionalStreamId();
  auto stream_owned = std::make_unique<WebTransportHttp3UnidirectionalStream>(
      stream_id, this, session->id());
  WebTransportHttp3UnidirectionalStream* stream = stream_owned.get();
  ActivateStream(std::move(stream_owned));
  stream->WritePreamble();
  session->AssociateStream(stream_id);
  return stream;
}

QuicSpdyStream* QuicSpdySession::CreateOutgoingBidirectionalWebTransportStream(
    WebTransportHttp3* session) {
  QuicSpdyStream* stream = CreateOutgoingBidirectionalStream();
  if (stream == nullptr) {
    return nullptr;
  }
  QuicStreamId stream_id = stream->id();
  stream->ConvertToWebTransportDataStream(session->id());
  if (stream->web_transport_stream() == nullptr) {
    // An error in ConvertToWebTransportDataStream() would result in
    // CONNECTION_CLOSE, thus we don't need to do anything here.
    return nullptr;
  }
  session->AssociateStream(stream_id);
  return stream;
}

void QuicSpdySession::OnDatagramProcessed(
    absl::optional<MessageStatus> /*status*/) {
  // TODO(b/184598230): make this work with multiple datagram flows.
}

void QuicSpdySession::DatagramObserver::OnDatagramProcessed(
    absl::optional<MessageStatus> status) {
  session_->OnDatagramProcessed(status);
}

HttpDatagramSupport QuicSpdySession::LocalHttpDatagramSupport() {
  return HttpDatagramSupport::kNone;
}

std::string HttpDatagramSupportToString(
    HttpDatagramSupport http_datagram_support) {
  switch (http_datagram_support) {
    case HttpDatagramSupport::kNone:
      return "None";
    case HttpDatagramSupport::kDraft04:
      return "Draft04";
    case HttpDatagramSupport::kRfc:
      return "Rfc";
    case HttpDatagramSupport::kRfcAndDraft04:
      return "RfcAndDraft04";
  }
  return absl::StrCat("Unknown(", static_cast<int>(http_datagram_support), ")");
}

std::ostream& operator<<(std::ostream& os,
                         const HttpDatagramSupport& http_datagram_support) {
  os << HttpDatagramSupportToString(http_datagram_support);
  return os;
}

// Must not be called after Initialize().
void QuicSpdySession::set_allow_extended_connect(bool allow_extended_connect) {
  QUIC_BUG_IF(extended connect wrong version,
              !GetQuicReloadableFlag(quic_verify_request_headers_2) ||
                  !VersionUsesHttp3(transport_version()))
      << "Try to enable/disable extended CONNECT in Google QUIC";
  QUIC_BUG_IF(extended connect on client,
              !GetQuicReloadableFlag(quic_verify_request_headers_2) ||
                  perspective() == Perspective::IS_CLIENT)
      << "Enabling/disabling extended CONNECT on the client side has no effect";
  if (ShouldNegotiateWebTransport()) {
    QUIC_BUG_IF(disable extended connect, !allow_extended_connect)
        << "Disabling extended CONNECT with web transport enabled has no "
           "effect.";
    return;
  }
  allow_extended_connect_ = allow_extended_connect;
}

#undef ENDPOINT  // undef for jumbo builds

}  // namespace quic
