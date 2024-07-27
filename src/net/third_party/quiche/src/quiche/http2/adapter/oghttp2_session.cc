#include "quiche/http2/adapter/oghttp2_session.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "quiche/http2/adapter/header_validator.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_util.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/noop_header_validator.h"
#include "quiche/http2/adapter/oghttp2_util.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

namespace {

using ConnectionError = Http2VisitorInterface::ConnectionError;
using DataFrameHeaderInfo = Http2VisitorInterface::DataFrameHeaderInfo;
using SpdyFramerError = Http2DecoderAdapter::SpdyFramerError;

using ::spdy::SpdySettingsIR;

const uint32_t kMaxAllowedMetadataFrameSize = 65536u;
const uint32_t kDefaultHpackTableCapacity = 4096u;
const uint32_t kMaximumHpackTableCapacity = 65536u;

// Corresponds to NGHTTP2_ERR_CALLBACK_FAILURE.
const int kSendError = -902;

constexpr absl::string_view kHeadValue = "HEAD";

// TODO(birenroy): Consider incorporating spdy::FlagsSerializionVisitor here.
class FrameAttributeCollector : public spdy::SpdyFrameVisitor {
 public:
  FrameAttributeCollector() = default;
  void VisitData(const spdy::SpdyDataIR& data) override {
    frame_type_ = static_cast<uint8_t>(data.frame_type());
    stream_id_ = data.stream_id();
    flags_ =
        (data.fin() ? END_STREAM_FLAG : 0) | (data.padded() ? PADDED_FLAG : 0);
  }
  void VisitHeaders(const spdy::SpdyHeadersIR& headers) override {
    frame_type_ = static_cast<uint8_t>(headers.frame_type());
    stream_id_ = headers.stream_id();
    flags_ = END_HEADERS_FLAG | (headers.fin() ? END_STREAM_FLAG : 0) |
             (headers.padded() ? PADDED_FLAG : 0) |
             (headers.has_priority() ? PRIORITY_FLAG : 0);
  }
  void VisitPriority(const spdy::SpdyPriorityIR& priority) override {
    frame_type_ = static_cast<uint8_t>(priority.frame_type());
    frame_type_ = 2;
    stream_id_ = priority.stream_id();
  }
  void VisitRstStream(const spdy::SpdyRstStreamIR& rst_stream) override {
    frame_type_ = static_cast<uint8_t>(rst_stream.frame_type());
    frame_type_ = 3;
    stream_id_ = rst_stream.stream_id();
    error_code_ = rst_stream.error_code();
  }
  void VisitSettings(const spdy::SpdySettingsIR& settings) override {
    frame_type_ = static_cast<uint8_t>(settings.frame_type());
    frame_type_ = 4;
    flags_ = (settings.is_ack() ? ACK_FLAG : 0);
  }
  void VisitPushPromise(const spdy::SpdyPushPromiseIR& push_promise) override {
    frame_type_ = static_cast<uint8_t>(push_promise.frame_type());
    frame_type_ = 5;
    stream_id_ = push_promise.stream_id();
    flags_ = (push_promise.padded() ? PADDED_FLAG : 0);
  }
  void VisitPing(const spdy::SpdyPingIR& ping) override {
    frame_type_ = static_cast<uint8_t>(ping.frame_type());
    frame_type_ = 6;
    flags_ = (ping.is_ack() ? ACK_FLAG : 0);
  }
  void VisitGoAway(const spdy::SpdyGoAwayIR& goaway) override {
    frame_type_ = static_cast<uint8_t>(goaway.frame_type());
    frame_type_ = 7;
    error_code_ = goaway.error_code();
  }
  void VisitWindowUpdate(
      const spdy::SpdyWindowUpdateIR& window_update) override {
    frame_type_ = static_cast<uint8_t>(window_update.frame_type());
    frame_type_ = 8;
    stream_id_ = window_update.stream_id();
  }
  void VisitContinuation(
      const spdy::SpdyContinuationIR& continuation) override {
    frame_type_ = static_cast<uint8_t>(continuation.frame_type());
    stream_id_ = continuation.stream_id();
    flags_ = continuation.end_headers() ? END_HEADERS_FLAG : 0;
  }
  void VisitUnknown(const spdy::SpdyUnknownIR& unknown) override {
    frame_type_ = static_cast<uint8_t>(unknown.frame_type());
    stream_id_ = unknown.stream_id();
    flags_ = unknown.flags();
  }
  void VisitAltSvc(const spdy::SpdyAltSvcIR& /*altsvc*/) override {}
  void VisitPriorityUpdate(
      const spdy::SpdyPriorityUpdateIR& /*priority_update*/) override {}
  void VisitAcceptCh(const spdy::SpdyAcceptChIR& /*accept_ch*/) override {}

  uint32_t stream_id() { return stream_id_; }
  uint32_t error_code() { return error_code_; }
  uint8_t frame_type() { return frame_type_; }
  uint8_t flags() { return flags_; }

 private:
  uint32_t stream_id_ = 0;
  uint32_t error_code_ = 0;
  uint8_t frame_type_ = 0;
  uint8_t flags_ = 0;
};

absl::string_view TracePerspectiveAsString(Perspective p) {
  switch (p) {
    case Perspective::kClient:
      return "OGHTTP2_CLIENT";
    case Perspective::kServer:
      return "OGHTTP2_SERVER";
  }
  return "OGHTTP2_SERVER";
}

Http2ErrorCode GetHttp2ErrorCode(SpdyFramerError error) {
  switch (error) {
    case SpdyFramerError::SPDY_NO_ERROR:
      return Http2ErrorCode::HTTP2_NO_ERROR;
    case SpdyFramerError::SPDY_INVALID_STREAM_ID:
    case SpdyFramerError::SPDY_INVALID_CONTROL_FRAME:
    case SpdyFramerError::SPDY_INVALID_PADDING:
    case SpdyFramerError::SPDY_INVALID_DATA_FRAME_FLAGS:
    case SpdyFramerError::SPDY_UNEXPECTED_FRAME:
      return Http2ErrorCode::PROTOCOL_ERROR;
    case SpdyFramerError::SPDY_CONTROL_PAYLOAD_TOO_LARGE:
    case SpdyFramerError::SPDY_INVALID_CONTROL_FRAME_SIZE:
    case SpdyFramerError::SPDY_OVERSIZED_PAYLOAD:
      return Http2ErrorCode::FRAME_SIZE_ERROR;
    case SpdyFramerError::SPDY_DECOMPRESS_FAILURE:
    case SpdyFramerError::SPDY_HPACK_INDEX_VARINT_ERROR:
    case SpdyFramerError::SPDY_HPACK_NAME_LENGTH_VARINT_ERROR:
    case SpdyFramerError::SPDY_HPACK_VALUE_LENGTH_VARINT_ERROR:
    case SpdyFramerError::SPDY_HPACK_NAME_TOO_LONG:
    case SpdyFramerError::SPDY_HPACK_VALUE_TOO_LONG:
    case SpdyFramerError::SPDY_HPACK_NAME_HUFFMAN_ERROR:
    case SpdyFramerError::SPDY_HPACK_VALUE_HUFFMAN_ERROR:
    case SpdyFramerError::SPDY_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE:
    case SpdyFramerError::SPDY_HPACK_INVALID_INDEX:
    case SpdyFramerError::SPDY_HPACK_INVALID_NAME_INDEX:
    case SpdyFramerError::SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED:
    case SpdyFramerError::
        SPDY_HPACK_INITIAL_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK:
    case SpdyFramerError::
        SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING:
    case SpdyFramerError::SPDY_HPACK_TRUNCATED_BLOCK:
    case SpdyFramerError::SPDY_HPACK_FRAGMENT_TOO_LONG:
    case SpdyFramerError::SPDY_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT:
      return Http2ErrorCode::COMPRESSION_ERROR;
    case SpdyFramerError::SPDY_INTERNAL_FRAMER_ERROR:
    case SpdyFramerError::SPDY_STOP_PROCESSING:
    case SpdyFramerError::LAST_ERROR:
      return Http2ErrorCode::INTERNAL_ERROR;
  }
  return Http2ErrorCode::INTERNAL_ERROR;
}

bool IsResponse(HeaderType type) {
  return type == HeaderType::RESPONSE_100 || type == HeaderType::RESPONSE;
}

bool StatusIs1xx(absl::string_view status) {
  return status.size() == 3 && status[0] == '1';
}

// Returns the upper bound on HPACK encoder table capacity. If not specified in
// the Options, a reasonable default upper bound is used.
uint32_t HpackCapacityBound(const OgHttp2Session::Options& o) {
  return o.max_hpack_encoding_table_capacity.value_or(
      kMaximumHpackTableCapacity);
}

bool IsNonAckSettings(const spdy::SpdyFrameIR& frame) {
  return frame.frame_type() == spdy::SpdyFrameType::SETTINGS &&
         !reinterpret_cast<const SpdySettingsIR&>(frame).is_ack();
}

}  // namespace

OgHttp2Session::PassthroughHeadersHandler::PassthroughHeadersHandler(
    OgHttp2Session& session, Http2VisitorInterface& visitor)
    : session_(session), visitor_(visitor) {
  if (session_.options_.validate_http_headers) {
    QUICHE_VLOG(2) << "instantiating regular header validator";
    validator_ = std::make_unique<HeaderValidator>();
    if (session_.options_.validate_path) {
      validator_->SetValidatePath();
    }
    if (session_.options_.allow_fragment_in_path) {
      validator_->SetAllowFragmentInPath();
    }
    if (session_.options_.allow_different_host_and_authority) {
      validator_->SetAllowDifferentHostAndAuthority();
    }
  } else {
    QUICHE_VLOG(2) << "instantiating noop header validator";
    validator_ = std::make_unique<NoopHeaderValidator>();
  }
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockStart() {
  Reset();
  const bool status = visitor_.OnBeginHeadersForStream(stream_id_);
  if (!status) {
    QUICHE_VLOG(1)
        << "Visitor rejected header block, returning HEADER_CONNECTION_ERROR";
    SetResult(Http2VisitorInterface::HEADER_CONNECTION_ERROR);
  }
  validator_->StartHeaderBlock();
}

Http2VisitorInterface::OnHeaderResult InterpretHeaderStatus(
    HeaderValidator::HeaderStatus status) {
  switch (status) {
    case HeaderValidator::HEADER_OK:
    case HeaderValidator::HEADER_SKIP:
      return Http2VisitorInterface::HEADER_OK;
    case HeaderValidator::HEADER_FIELD_INVALID:
      return Http2VisitorInterface::HEADER_FIELD_INVALID;
    case HeaderValidator::HEADER_FIELD_TOO_LONG:
      return Http2VisitorInterface::HEADER_RST_STREAM;
  }
  return Http2VisitorInterface::HEADER_CONNECTION_ERROR;
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeader(
    absl::string_view key, absl::string_view value) {
  if (error_encountered_) {
    QUICHE_VLOG(2) << "Early return; status not HEADER_OK";
    return;
  }
  const HeaderValidator::HeaderStatus validation_result =
      validator_->ValidateSingleHeader(key, value);
  if (validation_result == HeaderValidator::HEADER_SKIP) {
    return;
  }
  if (validation_result != HeaderValidator::HEADER_OK) {
    QUICHE_VLOG(2) << "Header validation failed with result "
                   << static_cast<int>(validation_result);
    SetResult(InterpretHeaderStatus(validation_result));
    return;
  }
  const Http2VisitorInterface::OnHeaderResult result =
      visitor_.OnHeaderForStream(stream_id_, key, value);
  SetResult(result);
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockEnd(
    size_t /* uncompressed_header_bytes */,
    size_t /* compressed_header_bytes */) {
  if (error_encountered_) {
    // The error has already been handled.
    return;
  }
  if (!validator_->FinishHeaderBlock(type_)) {
    QUICHE_VLOG(1) << "FinishHeaderBlock returned false; returning "
                   << "HEADER_HTTP_MESSAGING";
    SetResult(Http2VisitorInterface::HEADER_HTTP_MESSAGING);
    return;
  }
  if (frame_contains_fin_ && IsResponse(type_) &&
      StatusIs1xx(status_header())) {
    QUICHE_VLOG(1) << "Unexpected end of stream without final headers";
    SetResult(Http2VisitorInterface::HEADER_HTTP_MESSAGING);
    return;
  }
  const bool result = visitor_.OnEndHeadersForStream(stream_id_);
  if (!result) {
    session_.fatal_visitor_callback_failure_ = true;
    session_.decoder_.StopProcessing();
  }
}

// TODO(diannahu): Add checks for request methods.
bool OgHttp2Session::PassthroughHeadersHandler::CanReceiveBody() const {
  switch (header_type()) {
    case HeaderType::REQUEST_TRAILER:
    case HeaderType::RESPONSE_TRAILER:
    case HeaderType::RESPONSE_100:
      return false;
    case HeaderType::RESPONSE:
      // 304 responses should not have a body:
      // https://httpwg.org/specs/rfc7230.html#rfc.section.3.3.2
      // Neither should 204 responses:
      // https://httpwg.org/specs/rfc7231.html#rfc.section.6.3.5
      return status_header() != "304" && status_header() != "204";
    case HeaderType::REQUEST:
      return true;
  }
  return true;
}

void OgHttp2Session::PassthroughHeadersHandler::SetResult(
    Http2VisitorInterface::OnHeaderResult result) {
  if (result != Http2VisitorInterface::HEADER_OK) {
    error_encountered_ = true;
    session_.OnHeaderStatus(stream_id_, result);
  }
}

// A visitor that extracts an int64_t from each type of a ProcessBytesResult.
struct OgHttp2Session::ProcessBytesResultVisitor {
  int64_t operator()(const int64_t bytes) const { return bytes; }

  int64_t operator()(const ProcessBytesError error) const {
    switch (error) {
      case ProcessBytesError::kUnspecified:
        return -1;
      case ProcessBytesError::kInvalidConnectionPreface:
        return -903;  // NGHTTP2_ERR_BAD_CLIENT_MAGIC
      case ProcessBytesError::kVisitorCallbackFailed:
        return -902;  // NGHTTP2_ERR_CALLBACK_FAILURE
    }
    return -1;
  }
};

OgHttp2Session::OgHttp2Session(Http2VisitorInterface& visitor, Options options)
    : visitor_(visitor),
      options_(options),
      event_forwarder_([this]() { return !latched_error_; }, *this),
      receive_logger_(
          &event_forwarder_, TracePerspectiveAsString(options.perspective),
          [logging_enabled = GetQuicheFlag(quiche_oghttp2_debug_trace)]() {
            return logging_enabled;
          },
          this),
      send_logger_(
          TracePerspectiveAsString(options.perspective),
          [logging_enabled = GetQuicheFlag(quiche_oghttp2_debug_trace)]() {
            return logging_enabled;
          },
          this),
      headers_handler_(*this, visitor),
      noop_headers_handler_(/*listener=*/nullptr),
      connection_window_manager_(
          kInitialFlowControlWindowSize,
          [this](size_t window_update_delta) {
            SendWindowUpdate(kConnectionStreamId, window_update_delta);
          },
          options.should_window_update_fn,
          /*update_window_on_notify=*/false),
      max_outbound_concurrent_streams_(
          options.remote_max_concurrent_streams.value_or(100u)) {
  decoder_.set_visitor(&receive_logger_);
  if (options_.max_header_list_bytes) {
    // Limit buffering of encoded HPACK data to 2x the decoded limit.
    decoder_.GetHpackDecoder().set_max_decode_buffer_size_bytes(
        2 * *options_.max_header_list_bytes);
    // Limit the total bytes accepted for HPACK decoding to 4x the limit.
    decoder_.GetHpackDecoder().set_max_header_block_bytes(
        4 * *options_.max_header_list_bytes);
  }
  if (IsServerSession()) {
    remaining_preface_ = {spdy::kHttp2ConnectionHeaderPrefix,
                          spdy::kHttp2ConnectionHeaderPrefixSize};
  }
  if (options_.max_header_field_size.has_value()) {
    headers_handler_.SetMaxFieldSize(*options_.max_header_field_size);
  }
  headers_handler_.SetAllowObsText(options_.allow_obs_text);
  if (!options_.crumble_cookies) {
    // As seen in https://github.com/envoyproxy/envoy/issues/32611, some HTTP/2
    // endpoints don't properly handle multiple `Cookie` header fields.
    framer_.GetHpackEncoder()->DisableCookieCrumbling();
  }
}

OgHttp2Session::~OgHttp2Session() {}

void OgHttp2Session::SetStreamUserData(Http2StreamId stream_id,
                                       void* user_data) {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    it->second.user_data = user_data;
  }
}

void* OgHttp2Session::GetStreamUserData(Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return it->second.user_data;
  }
  auto p = pending_streams_.find(stream_id);
  if (p != pending_streams_.end()) {
    return p->second.user_data;
  }
  return nullptr;
}

bool OgHttp2Session::ResumeStream(Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end() || !HasMoreData(it->second) ||
      !write_scheduler_.StreamRegistered(stream_id)) {
    return false;
  }
  it->second.data_deferred = false;
  write_scheduler_.MarkStreamReady(stream_id, /*add_to_front=*/false);
  return true;
}

int OgHttp2Session::GetStreamSendWindowSize(Http2StreamId stream_id) const {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return it->second.send_window;
  }
  return -1;
}

int OgHttp2Session::GetStreamReceiveWindowLimit(Http2StreamId stream_id) const {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return it->second.window_manager.WindowSizeLimit();
  }
  return -1;
}

int OgHttp2Session::GetStreamReceiveWindowSize(Http2StreamId stream_id) const {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    return it->second.window_manager.CurrentWindowSize();
  }
  return -1;
}

int OgHttp2Session::GetReceiveWindowSize() const {
  return connection_window_manager_.CurrentWindowSize();
}

int OgHttp2Session::GetHpackEncoderDynamicTableSize() const {
  const spdy::HpackEncoder* encoder = framer_.GetHpackEncoder();
  return encoder == nullptr ? 0 : encoder->GetDynamicTableSize();
}

int OgHttp2Session::GetHpackEncoderDynamicTableCapacity() const {
  const spdy::HpackEncoder* encoder = framer_.GetHpackEncoder();
  return encoder == nullptr ? kDefaultHpackTableCapacity
                            : encoder->CurrentHeaderTableSizeSetting();
}

int OgHttp2Session::GetHpackDecoderDynamicTableSize() const {
  return decoder_.GetHpackDecoder().GetDynamicTableSize();
}

int OgHttp2Session::GetHpackDecoderSizeLimit() const {
  return decoder_.GetHpackDecoder().GetCurrentHeaderTableSizeSetting();
}

int64_t OgHttp2Session::ProcessBytes(absl::string_view bytes) {
  QUICHE_VLOG(2) << TracePerspectiveAsString(options_.perspective)
                 << " processing [" << absl::CEscape(bytes) << "]";
  return absl::visit(ProcessBytesResultVisitor(), ProcessBytesImpl(bytes));
}

absl::variant<int64_t, OgHttp2Session::ProcessBytesError>
OgHttp2Session::ProcessBytesImpl(absl::string_view bytes) {
  if (processing_bytes_) {
    QUICHE_VLOG(1) << "Returning early; already processing bytes.";
    return 0;
  }
  processing_bytes_ = true;
  auto cleanup = absl::MakeCleanup([this]() { processing_bytes_ = false; });

  if (options_.blackhole_data_on_connection_error && latched_error_) {
    return static_cast<int64_t>(bytes.size());
  }

  int64_t preface_consumed = 0;
  if (!remaining_preface_.empty()) {
    QUICHE_VLOG(2) << "Preface bytes remaining: " << remaining_preface_.size();
    // decoder_ does not understand the client connection preface.
    size_t min_size = std::min(remaining_preface_.size(), bytes.size());
    if (!absl::StartsWith(remaining_preface_, bytes.substr(0, min_size))) {
      // Preface doesn't match!
      QUICHE_DLOG(INFO) << "Preface doesn't match! Expected: ["
                        << absl::CEscape(remaining_preface_) << "], actual: ["
                        << absl::CEscape(bytes) << "]";
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kInvalidConnectionPreface);
      return ProcessBytesError::kInvalidConnectionPreface;
    }
    remaining_preface_.remove_prefix(min_size);
    bytes.remove_prefix(min_size);
    if (!remaining_preface_.empty()) {
      QUICHE_VLOG(2) << "Preface bytes remaining: "
                     << remaining_preface_.size();
      return static_cast<int64_t>(min_size);
    }
    preface_consumed = min_size;
  }
  int64_t result = decoder_.ProcessInput(bytes.data(), bytes.size());
  QUICHE_VLOG(2) << "ProcessBytes result: " << result;
  if (fatal_visitor_callback_failure_) {
    QUICHE_DCHECK(latched_error_);
    QUICHE_VLOG(2) << "Visitor callback failed while processing bytes.";
    return ProcessBytesError::kVisitorCallbackFailed;
  }
  if (latched_error_ || result < 0) {
    QUICHE_VLOG(2) << "ProcessBytes encountered an error.";
    if (options_.blackhole_data_on_connection_error) {
      return static_cast<int64_t>(bytes.size() + preface_consumed);
    } else {
      return ProcessBytesError::kUnspecified;
    }
  }
  return result + preface_consumed;
}

int OgHttp2Session::Consume(Http2StreamId stream_id, size_t num_bytes) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    QUICHE_LOG(ERROR) << "Stream " << stream_id << " not found when consuming "
                      << num_bytes << " bytes";
  } else {
    it->second.window_manager.MarkDataFlushed(num_bytes);
  }
  connection_window_manager_.MarkDataFlushed(num_bytes);
  return 0;  // Remove?
}

void OgHttp2Session::StartGracefulShutdown() {
  if (IsServerSession()) {
    if (!queued_goaway_) {
      EnqueueFrame(std::make_unique<spdy::SpdyGoAwayIR>(
          std::numeric_limits<int32_t>::max(), spdy::ERROR_CODE_NO_ERROR,
          "graceful_shutdown"));
    }
  } else {
    QUICHE_LOG(ERROR) << "Graceful shutdown not needed for clients.";
  }
}

void OgHttp2Session::EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame) {
  if (queued_immediate_goaway_) {
    // Do not allow additional frames to be enqueued after the GOAWAY.
    return;
  }

  const bool is_non_ack_settings = IsNonAckSettings(*frame);
  MaybeSetupPreface(is_non_ack_settings);

  if (frame->frame_type() == spdy::SpdyFrameType::GOAWAY) {
    queued_goaway_ = true;
    if (latched_error_) {
      PrepareForImmediateGoAway();
    }
  } else if (frame->fin() ||
             frame->frame_type() == spdy::SpdyFrameType::RST_STREAM) {
    auto iter = stream_map_.find(frame->stream_id());
    if (iter != stream_map_.end()) {
      iter->second.half_closed_local = true;
    }
    if (frame->frame_type() == spdy::SpdyFrameType::RST_STREAM) {
      // TODO(diannahu): Condition on existence in the stream map?
      streams_reset_.insert(frame->stream_id());
    }
  } else if (frame->frame_type() == spdy::SpdyFrameType::WINDOW_UPDATE) {
    UpdateReceiveWindow(
        frame->stream_id(),
        reinterpret_cast<spdy::SpdyWindowUpdateIR&>(*frame).delta());
  } else if (is_non_ack_settings) {
    HandleOutboundSettings(
        *reinterpret_cast<spdy::SpdySettingsIR*>(frame.get()));
  }
  if (frame->stream_id() != 0) {
    auto result = queued_frames_.insert({frame->stream_id(), 1});
    if (!result.second) {
      ++(result.first->second);
    }
  }
  frames_.push_back(std::move(frame));
}

int OgHttp2Session::Send() {
  if (sending_) {
    QUICHE_VLOG(1) << TracePerspectiveAsString(options_.perspective)
                   << " returning early; already sending.";
    return 0;
  }
  sending_ = true;
  auto cleanup = absl::MakeCleanup([this]() { sending_ = false; });

  if (fatal_send_error_) {
    return kSendError;
  }

  MaybeSetupPreface(/*sending_outbound_settings=*/false);

  SendResult continue_writing = SendQueuedFrames();
  if (queued_immediate_goaway_) {
    // If an immediate GOAWAY was queued, then the above flush either sent the
    // GOAWAY or buffered it to be sent on the next successful flush. In either
    // case, return early here to avoid sending other frames.
    return InterpretSendResult(continue_writing);
  }
  // Notify on new/pending streams closed due to GOAWAY receipt.
  CloseGoAwayRejectedStreams();
  // Wake streams for writes.
  while (continue_writing == SendResult::SEND_OK && HasReadyStream()) {
    const Http2StreamId stream_id = GetNextReadyStream();
    // TODO(birenroy): Add a return value to indicate write blockage, so streams
    // aren't woken unnecessarily.
    QUICHE_VLOG(1) << "Waking stream " << stream_id << " for writes.";
    continue_writing = WriteForStream(stream_id);
  }
  if (continue_writing == SendResult::SEND_OK) {
    continue_writing = SendQueuedFrames();
  }
  return InterpretSendResult(continue_writing);
}

int OgHttp2Session::InterpretSendResult(SendResult result) {
  if (result == SendResult::SEND_ERROR) {
    fatal_send_error_ = true;
    return kSendError;
  } else {
    return 0;
  }
}

bool OgHttp2Session::HasReadyStream() const {
  return !trailers_ready_.empty() ||
         (write_scheduler_.HasReadyStreams() && connection_send_window_ > 0);
}

Http2StreamId OgHttp2Session::GetNextReadyStream() {
  QUICHE_DCHECK(HasReadyStream());
  if (!trailers_ready_.empty()) {
    const Http2StreamId stream_id = *trailers_ready_.begin();
    // WriteForStream() will re-mark the stream as ready, if necessary.
    write_scheduler_.MarkStreamNotReady(stream_id);
    trailers_ready_.erase(trailers_ready_.begin());
    return stream_id;
  }
  return write_scheduler_.PopNextReadyStream();
}

int32_t OgHttp2Session::SubmitRequestInternal(
    absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source, bool end_stream,
    void* user_data) {
  // TODO(birenroy): return an error for the incorrect perspective
  const Http2StreamId stream_id = next_stream_id_;
  next_stream_id_ += 2;
  if (!pending_streams_.empty() || !CanCreateStream()) {
    // TODO(diannahu): There should probably be a limit to the number of allowed
    // pending streams.
    pending_streams_.insert(
        {stream_id,
         PendingStreamState{ToHeaderBlock(headers), std::move(data_source),
                            user_data, end_stream}});
    StartPendingStreams();
  } else {
    StartRequest(stream_id, ToHeaderBlock(headers), std::move(data_source),
                 user_data, end_stream);
  }
  return stream_id;
}

int OgHttp2Session::SubmitResponseInternal(
    Http2StreamId stream_id, absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source, bool end_stream) {
  // TODO(birenroy): return an error for the incorrect perspective
  auto iter = stream_map_.find(stream_id);
  if (iter == stream_map_.end()) {
    QUICHE_LOG(ERROR) << "Unable to find stream " << stream_id;
    return -501;  // NGHTTP2_ERR_INVALID_ARGUMENT
  }
  if (data_source != nullptr) {
    // Add data source to stream state
    iter->second.outbound_body = std::move(data_source);
    write_scheduler_.MarkStreamReady(stream_id, false);
  } else if (!end_stream) {
    iter->second.check_visitor_for_body = true;
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  SendHeaders(stream_id, ToHeaderBlock(headers), end_stream);
  return 0;
}

OgHttp2Session::SendResult OgHttp2Session::MaybeSendBufferedData() {
  int64_t result = std::numeric_limits<int64_t>::max();
  while (result > 0 && !buffered_data_.Empty()) {
    result = visitor_.OnReadyToSend(buffered_data_.GetPrefix());
    if (result > 0) {
      buffered_data_.RemovePrefix(result);
    }
  }
  if (result < 0) {
    LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                        ConnectionError::kSendError);
    return SendResult::SEND_ERROR;
  }
  return buffered_data_.Empty() ? SendResult::SEND_OK
                                : SendResult::SEND_BLOCKED;
}

OgHttp2Session::SendResult OgHttp2Session::SendQueuedFrames() {
  // Flush any serialized prefix.
  if (const SendResult result = MaybeSendBufferedData();
      result != SendResult::SEND_OK) {
    return result;
  }
  // Serialize and send frames in the queue.
  while (!frames_.empty()) {
    const auto& frame_ptr = frames_.front();
    FrameAttributeCollector c;
    frame_ptr->Visit(&c);

    // DATA frames should never be queued.
    QUICHE_DCHECK_NE(c.frame_type(), 0);

    const bool stream_reset =
        c.stream_id() != 0 && streams_reset_.count(c.stream_id()) > 0;
    if (stream_reset &&
        c.frame_type() != static_cast<uint8_t>(FrameType::RST_STREAM)) {
      // The stream has been reset, so any other remaining frames can be
      // skipped.
      // TODO(birenroy): inform the visitor of frames that are skipped.
      DecrementQueuedFrameCount(c.stream_id(), c.frame_type());
      frames_.pop_front();
      continue;
    } else if (!IsServerSession() && received_goaway_ &&
               c.stream_id() >
                   static_cast<uint32_t>(received_goaway_stream_id_)) {
      // This frame will be ignored by the server, so don't send it. The stream
      // associated with this frame should have been closed in OnGoAway().
      frames_.pop_front();
      continue;
    }
    // Frames can't accurately report their own length; the actual serialized
    // length must be used instead.
    spdy::SpdySerializedFrame frame = framer_.SerializeFrame(*frame_ptr);
    const size_t frame_payload_length = frame.size() - spdy::kFrameHeaderSize;
    frame_ptr->Visit(&send_logger_);
    visitor_.OnBeforeFrameSent(c.frame_type(), c.stream_id(),
                               frame_payload_length, c.flags());
    const int64_t result = visitor_.OnReadyToSend(absl::string_view(frame));
    if (result < 0) {
      LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                          ConnectionError::kSendError);
      return SendResult::SEND_ERROR;
    } else if (result == 0) {
      // Write blocked.
      return SendResult::SEND_BLOCKED;
    } else {
      frames_.pop_front();

      const bool ok =
          AfterFrameSent(c.frame_type(), c.stream_id(), frame_payload_length,
                         c.flags(), c.error_code());
      if (!ok) {
        LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                            ConnectionError::kSendError);
        return SendResult::SEND_ERROR;
      }
      if (static_cast<size_t>(result) < frame.size()) {
        // The frame was partially written, so the rest must be buffered.
        buffered_data_.Append(
            absl::string_view(frame.data() + result, frame.size() - result));
        return SendResult::SEND_BLOCKED;
      }
    }
  }
  return SendResult::SEND_OK;
}

bool OgHttp2Session::AfterFrameSent(uint8_t frame_type_int, uint32_t stream_id,
                                    size_t payload_length, uint8_t flags,
                                    uint32_t error_code) {
  const FrameType frame_type = static_cast<FrameType>(frame_type_int);
  int result = visitor_.OnFrameSent(frame_type_int, stream_id, payload_length,
                                    flags, error_code);
  if (result < 0) {
    return false;
  }
  if (stream_id == 0) {
    if (frame_type == FrameType::SETTINGS) {
      const bool is_settings_ack = (flags & ACK_FLAG);
      if (is_settings_ack && encoder_header_table_capacity_when_acking_) {
        framer_.UpdateHeaderEncoderTableSize(
            *encoder_header_table_capacity_when_acking_);
        encoder_header_table_capacity_when_acking_ = std::nullopt;
      } else if (!is_settings_ack) {
        sent_non_ack_settings_ = true;
      }
    }
    return true;
  }

  const bool contains_fin =
      (frame_type == FrameType::DATA || frame_type == FrameType::HEADERS) &&
      (flags & END_STREAM_FLAG) == END_STREAM_FLAG;
  auto it = stream_map_.find(stream_id);
  const bool still_open_remote =
      it != stream_map_.end() && !it->second.half_closed_remote;
  if (contains_fin && still_open_remote &&
      options_.rst_stream_no_error_when_incomplete && IsServerSession()) {
    // Since the peer has not yet ended the stream, this endpoint should
    // send a RST_STREAM NO_ERROR. See RFC 7540 Section 8.1.
    frames_.push_front(std::make_unique<spdy::SpdyRstStreamIR>(
        stream_id, spdy::SpdyErrorCode::ERROR_CODE_NO_ERROR));
    auto queued_result = queued_frames_.insert({stream_id, 1});
    if (!queued_result.second) {
      ++(queued_result.first->second);
    }
    it->second.half_closed_remote = true;
  }

  DecrementQueuedFrameCount(stream_id, frame_type_int);
  return true;
}

OgHttp2Session::SendResult OgHttp2Session::WriteForStream(
    Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    QUICHE_LOG(ERROR) << "Can't find stream " << stream_id
                      << " which is ready to write!";
    return SendResult::SEND_OK;
  }
  StreamState& state = it->second;
  auto reset_it = streams_reset_.find(stream_id);
  if (reset_it != streams_reset_.end()) {
    // The stream has been reset; there's no point in sending DATA or trailing
    // HEADERS.
    AbandonData(state);
    state.trailers = nullptr;
    return SendResult::SEND_OK;
  }

  SendResult connection_can_write = SendResult::SEND_OK;
  if (!IsReadyToWriteData(state)) {
    // No data to send, but there might be trailers.
    if (state.trailers != nullptr) {
      // Trailers will include END_STREAM, so the data source can be discarded.
      // Since data_deferred is true, there is no data waiting to be flushed for
      // this stream.
      AbandonData(state);
      auto block_ptr = std::move(state.trailers);
      if (state.half_closed_local) {
        QUICHE_LOG(ERROR) << "Sent fin; can't send trailers.";

        // TODO(birenroy,diannahu): Consider queuing a RST_STREAM INTERNAL_ERROR
        // instead.
        CloseStream(stream_id, Http2ErrorCode::INTERNAL_ERROR);
      } else {
        SendTrailers(stream_id, std::move(*block_ptr));
      }
    }
    return SendResult::SEND_OK;
  }
  int32_t available_window =
      std::min({connection_send_window_, state.send_window,
                static_cast<int32_t>(max_frame_payload_)});
  while (connection_can_write == SendResult::SEND_OK && available_window > 0 &&
         IsReadyToWriteData(state)) {
    DataFrameHeaderInfo info =
        GetDataFrameInfo(stream_id, available_window, state);
    QUICHE_VLOG(2) << "WriteForStream | length: " << info.payload_length
                   << " end_data: " << info.end_data
                   << " end_stream: " << info.end_stream
                   << " trailers: " << state.trailers.get();
    if (info.payload_length == 0 && !info.end_data &&
        state.trailers == nullptr) {
      // An unproductive call to SelectPayloadLength() results in this stream
      // entering the "deferred" state only if no trailers are available to
      // send.
      state.data_deferred = true;
      break;
    } else if (info.payload_length == DataFrameSource::kError) {
      // TODO(birenroy,diannahu): Consider queuing a RST_STREAM INTERNAL_ERROR
      // instead.
      CloseStream(stream_id, Http2ErrorCode::INTERNAL_ERROR);
      // No more work on the stream; it has been closed.
      break;
    }
    if (info.payload_length > 0 || info.end_stream) {
      spdy::SpdyDataIR data(stream_id);
      data.set_fin(info.end_stream);
      data.SetDataShallow(info.payload_length);
      spdy::SpdySerializedFrame header =
          spdy::SpdyFramer::SerializeDataFrameHeaderWithPaddingLengthField(
              data);
      QUICHE_DCHECK(buffered_data_.Empty() && frames_.empty());
      data.Visit(&send_logger_);
      const bool success = SendDataFrame(stream_id, absl::string_view(header),
                                         info.payload_length, state);
      if (!success) {
        connection_can_write = SendResult::SEND_BLOCKED;
        break;
      }
      connection_send_window_ -= info.payload_length;
      state.send_window -= info.payload_length;
      available_window = std::min({connection_send_window_, state.send_window,
                                   static_cast<int32_t>(max_frame_payload_)});
      if (info.end_stream) {
        state.half_closed_local = true;
        MaybeFinWithRstStream(it);
      }
      const bool ok =
          AfterFrameSent(/* DATA */ 0, stream_id, info.payload_length,
                         info.end_stream ? END_STREAM_FLAG : 0x0, 0);
      if (!ok) {
        LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                            ConnectionError::kSendError);
        return SendResult::SEND_ERROR;
      }
      if (!stream_map_.contains(stream_id)) {
        // Note: the stream may have been closed if `fin` is true.
        break;
      }
    }
    if (info.end_data ||
        (info.payload_length == 0 && state.trailers != nullptr)) {
      // If SelectPayloadLength() returned {0, false}, and there are trailers to
      // send, it's okay to send the trailers.
      if (state.trailers != nullptr) {
        auto block_ptr = std::move(state.trailers);
        if (info.end_stream) {
          QUICHE_LOG(ERROR) << "Sent fin; can't send trailers.";

          // TODO(birenroy,diannahu): Consider queuing a RST_STREAM
          // INTERNAL_ERROR instead.
          CloseStream(stream_id, Http2ErrorCode::INTERNAL_ERROR);
          // No more work on this stream; it has been closed.
          break;
        } else {
          SendTrailers(stream_id, std::move(*block_ptr));
        }
      }
      AbandonData(state);
    }
  }
  // If the stream still exists and has data to send, it should be marked as
  // ready in the write scheduler.
  if (stream_map_.contains(stream_id) && !state.data_deferred &&
      state.send_window > 0 && HasMoreData(state)) {
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  // Streams can continue writing as long as the connection is not write-blocked
  // and there is additional flow control quota available.
  if (connection_can_write != SendResult::SEND_OK) {
    return connection_can_write;
  }
  return connection_send_window_ <= 0 ? SendResult::SEND_BLOCKED
                                      : SendResult::SEND_OK;
}

void OgHttp2Session::SerializeMetadata(Http2StreamId stream_id,
                                       std::unique_ptr<MetadataSource> source) {
  const uint32_t max_payload_size =
      std::min(kMaxAllowedMetadataFrameSize, max_frame_payload_);
  auto payload_buffer = std::make_unique<uint8_t[]>(max_payload_size);

  while (true) {
    auto [written, end_metadata] =
        source->Pack(payload_buffer.get(), max_payload_size);
    if (written < 0) {
      // Unable to pack any metadata.
      return;
    }
    QUICHE_DCHECK_LE(static_cast<size_t>(written), max_payload_size);
    auto payload = absl::string_view(
        reinterpret_cast<const char*>(payload_buffer.get()), written);
    EnqueueFrame(std::make_unique<spdy::SpdyUnknownIR>(
        stream_id, kMetadataFrameType, end_metadata ? kMetadataEndFlag : 0u,
        std::string(payload)));
    if (end_metadata) {
      return;
    }
  }
}

int32_t OgHttp2Session::SubmitRequest(
    absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source, bool end_stream,
    void* user_data) {
  return SubmitRequestInternal(headers, std::move(data_source), end_stream,
                               user_data);
}

int OgHttp2Session::SubmitResponse(Http2StreamId stream_id,
                                   absl::Span<const Header> headers,
                                   std::unique_ptr<DataFrameSource> data_source,
                                   bool end_stream) {
  return SubmitResponseInternal(stream_id, headers, std::move(data_source),
                                end_stream);
}

int OgHttp2Session::SubmitTrailer(Http2StreamId stream_id,
                                  absl::Span<const Header> trailers) {
  // TODO(birenroy): Reject trailers when acting as a client?
  auto iter = stream_map_.find(stream_id);
  if (iter == stream_map_.end()) {
    QUICHE_LOG(ERROR) << "Unable to find stream " << stream_id;
    return -501;  // NGHTTP2_ERR_INVALID_ARGUMENT
  }
  StreamState& state = iter->second;
  if (state.half_closed_local) {
    QUICHE_LOG(ERROR) << "Stream " << stream_id << " is half closed (local)";
    return -514;  // NGHTTP2_ERR_INVALID_STREAM_STATE
  }
  if (state.trailers != nullptr) {
    QUICHE_LOG(ERROR) << "Stream " << stream_id
                      << " already has trailers queued";
    return -514;  // NGHTTP2_ERR_INVALID_STREAM_STATE
  }
  if (!HasMoreData(state)) {
    // Enqueue trailers immediately.
    SendTrailers(stream_id, ToHeaderBlock(trailers));
  } else {
    // Save trailers so they can be written once data is done.
    state.trailers =
        std::make_unique<spdy::Http2HeaderBlock>(ToHeaderBlock(trailers));
    trailers_ready_.insert(stream_id);
  }
  return 0;
}

void OgHttp2Session::SubmitMetadata(Http2StreamId stream_id,
                                    std::unique_ptr<MetadataSource> source) {
  SerializeMetadata(stream_id, std::move(source));
}

void OgHttp2Session::SubmitSettings(absl::Span<const Http2Setting> settings) {
  auto frame = PrepareSettingsFrame(settings);
  EnqueueFrame(std::move(frame));
}

void OgHttp2Session::OnError(SpdyFramerError error,
                             std::string detailed_error) {
  QUICHE_VLOG(1) << "Error: "
                 << http2::Http2DecoderAdapter::SpdyFramerErrorToString(error)
                 << " details: " << detailed_error;
  // TODO(diannahu): Consider propagating `detailed_error`.
  LatchErrorAndNotify(GetHttp2ErrorCode(error), ConnectionError::kParseError);
}

void OgHttp2Session::OnCommonHeader(spdy::SpdyStreamId stream_id, size_t length,
                                    uint8_t type, uint8_t flags) {
  current_frame_type_ = type;
  highest_received_stream_id_ = std::max(static_cast<Http2StreamId>(stream_id),
                                         highest_received_stream_id_);
  if (streams_reset_.contains(stream_id)) {
    return;
  }
  const bool result = visitor_.OnFrameHeader(stream_id, length, type, flags);
  if (!result) {
    fatal_visitor_callback_failure_ = true;
    decoder_.StopProcessing();
  }
}

void OgHttp2Session::OnDataFrameHeader(spdy::SpdyStreamId stream_id,
                                       size_t length, bool /*fin*/) {
  auto iter = stream_map_.find(stream_id);
  if (iter == stream_map_.end() || streams_reset_.contains(stream_id)) {
    // The stream does not exist; it could be an error or a benign close, e.g.,
    // getting data for a stream this connection recently closed.
    if (static_cast<Http2StreamId>(stream_id) > highest_processed_stream_id_) {
      // Receiving DATA before HEADERS is a connection error.
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kWrongFrameSequence);
    }
    return;
  }

  if (static_cast<int64_t>(length) >
      connection_window_manager_.CurrentWindowSize()) {
    // Peer exceeded the connection flow control limit.
    LatchErrorAndNotify(
        Http2ErrorCode::FLOW_CONTROL_ERROR,
        Http2VisitorInterface::ConnectionError::kFlowControlError);
    return;
  }

  if (static_cast<int64_t>(length) >
      iter->second.window_manager.CurrentWindowSize()) {
    // Peer exceeded the stream flow control limit.
    EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
        stream_id, spdy::ERROR_CODE_FLOW_CONTROL_ERROR));
    return;
  }

  const bool result = visitor_.OnBeginDataForStream(stream_id, length);
  if (!result) {
    fatal_visitor_callback_failure_ = true;
    decoder_.StopProcessing();
  }

  if (!iter->second.can_receive_body && length > 0) {
    EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
        stream_id, spdy::ERROR_CODE_PROTOCOL_ERROR));
    return;
  }

  // Validate against the content-length if it exists.
  if (iter->second.remaining_content_length.has_value()) {
    if (length > *iter->second.remaining_content_length) {
      HandleContentLengthError(stream_id);
      iter->second.remaining_content_length.reset();
    } else {
      *iter->second.remaining_content_length -= length;
    }
  }
}

void OgHttp2Session::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                       const char* data, size_t len) {
  // Count the data against flow control, even if the stream is unknown.
  MarkDataBuffered(stream_id, len);

  if (!stream_map_.contains(stream_id) || streams_reset_.contains(stream_id)) {
    // If the stream was unknown due to a protocol error, the visitor was
    // informed in OnDataFrameHeader().
    return;
  }

  const bool result =
      visitor_.OnDataForStream(stream_id, absl::string_view(data, len));
  if (!result) {
    fatal_visitor_callback_failure_ = true;
    decoder_.StopProcessing();
  }
}

void OgHttp2Session::OnStreamEnd(spdy::SpdyStreamId stream_id) {
  auto iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second.half_closed_remote = true;
    if (streams_reset_.contains(stream_id)) {
      return;
    }

    // Validate against the content-length if it exists.
    if (iter->second.remaining_content_length.has_value() &&
        *iter->second.remaining_content_length != 0) {
      HandleContentLengthError(stream_id);
      return;
    }

    const bool result = visitor_.OnEndStream(stream_id);
    if (!result) {
      fatal_visitor_callback_failure_ = true;
      decoder_.StopProcessing();
    }
  }

  auto queued_frames_iter = queued_frames_.find(stream_id);
  const bool no_queued_frames = queued_frames_iter == queued_frames_.end() ||
                                queued_frames_iter->second == 0;
  if (iter != stream_map_.end() && iter->second.half_closed_local &&
      !IsServerSession() && no_queued_frames) {
    // From the client's perspective, the stream can be closed if it's already
    // half_closed_local.
    CloseStream(stream_id, Http2ErrorCode::HTTP2_NO_ERROR);
  }
}

void OgHttp2Session::OnStreamPadLength(spdy::SpdyStreamId stream_id,
                                       size_t value) {
  const size_t padding_length = 1 + value;
  const bool result = visitor_.OnDataPaddingLength(stream_id, padding_length);
  if (!result) {
    fatal_visitor_callback_failure_ = true;
    decoder_.StopProcessing();
  }
  connection_window_manager_.MarkWindowConsumed(padding_length);
  if (auto it = stream_map_.find(stream_id); it != stream_map_.end()) {
    it->second.window_manager.MarkWindowConsumed(padding_length);
  }
}

void OgHttp2Session::OnStreamPadding(spdy::SpdyStreamId /*stream_id*/, size_t
                                     /*len*/) {
  // Flow control was accounted for in OnStreamPadLength().
  // TODO(181586191): Pass padding to the visitor?
}

spdy::SpdyHeadersHandlerInterface* OgHttp2Session::OnHeaderFrameStart(
    spdy::SpdyStreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end() && !streams_reset_.contains(stream_id)) {
    headers_handler_.set_stream_id(stream_id);
    headers_handler_.set_header_type(
        NextHeaderType(it->second.received_header_type));
    return &headers_handler_;
  } else {
    return &noop_headers_handler_;
  }
}

void OgHttp2Session::OnHeaderFrameEnd(spdy::SpdyStreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    if (headers_handler_.header_type() == HeaderType::RESPONSE &&
        !headers_handler_.status_header().empty() &&
        headers_handler_.status_header()[0] == '1') {
      // If response headers carried a 1xx response code, final response headers
      // should still be forthcoming.
      headers_handler_.set_header_type(HeaderType::RESPONSE_100);
    }
    it->second.received_header_type = headers_handler_.header_type();

    // Track the content-length if the headers indicate that a body can follow.
    it->second.can_receive_body =
        headers_handler_.CanReceiveBody() && !it->second.sent_head_method;
    if (it->second.can_receive_body) {
      it->second.remaining_content_length = headers_handler_.content_length();
    }

    headers_handler_.set_stream_id(0);
  }
}

void OgHttp2Session::OnRstStream(spdy::SpdyStreamId stream_id,
                                 spdy::SpdyErrorCode error_code) {
  auto iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second.half_closed_remote = true;
    AbandonData(iter->second);
  } else if (static_cast<Http2StreamId>(stream_id) >
             highest_processed_stream_id_) {
    // Receiving RST_STREAM before HEADERS is a connection error.
    LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                        ConnectionError::kWrongFrameSequence);
    return;
  }
  if (streams_reset_.contains(stream_id)) {
    return;
  }
  visitor_.OnRstStream(stream_id, TranslateErrorCode(error_code));
  // TODO(birenroy): Consider whether there are outbound frames queued for the
  // stream.
  CloseStream(stream_id, TranslateErrorCode(error_code));
}

void OgHttp2Session::OnSettings() {
  visitor_.OnSettingsStart();
  auto settings = std::make_unique<SpdySettingsIR>();
  settings->set_is_ack(true);
  EnqueueFrame(std::move(settings));
}

void OgHttp2Session::OnSetting(spdy::SpdySettingsId id, uint32_t value) {
  switch (id) {
    case HEADER_TABLE_SIZE:
      value = std::min(value, HpackCapacityBound(options_));
      if (value < framer_.GetHpackEncoder()->CurrentHeaderTableSizeSetting()) {
        // Safe to apply a smaller table capacity immediately.
        QUICHE_VLOG(2) << TracePerspectiveAsString(options_.perspective)
                       << " applying encoder table capacity " << value;
        framer_.GetHpackEncoder()->ApplyHeaderTableSizeSetting(value);
      } else {
        QUICHE_VLOG(2)
            << TracePerspectiveAsString(options_.perspective)
            << " NOT applying encoder table capacity until writing ack: "
            << value;
        encoder_header_table_capacity_when_acking_ = value;
      }
      break;
    case ENABLE_PUSH:
      if (value > 1u) {
        visitor_.OnInvalidFrame(
            0, Http2VisitorInterface::InvalidFrameError::kProtocol);
        // The specification says this is a connection-level protocol error.
        LatchErrorAndNotify(
            Http2ErrorCode::PROTOCOL_ERROR,
            Http2VisitorInterface::ConnectionError::kInvalidSetting);
        return;
      }
      // Aside from validation, this setting is ignored.
      break;
    case MAX_CONCURRENT_STREAMS:
      max_outbound_concurrent_streams_ = value;
      if (!IsServerSession()) {
        // We may now be able to start pending streams.
        StartPendingStreams();
      }
      break;
    case INITIAL_WINDOW_SIZE:
      if (value > spdy::kSpdyMaximumWindowSize) {
        visitor_.OnInvalidFrame(
            0, Http2VisitorInterface::InvalidFrameError::kFlowControl);
        // The specification says this is a connection-level flow control error.
        LatchErrorAndNotify(
            Http2ErrorCode::FLOW_CONTROL_ERROR,
            Http2VisitorInterface::ConnectionError::kFlowControlError);
        return;
      } else {
        UpdateStreamSendWindowSizes(value);
      }
      break;
    case MAX_FRAME_SIZE:
      if (value < kDefaultFramePayloadSizeLimit ||
          value > kMaximumFramePayloadSizeLimit) {
        visitor_.OnInvalidFrame(
            0, Http2VisitorInterface::InvalidFrameError::kProtocol);
        // The specification says this is a connection-level protocol error.
        LatchErrorAndNotify(
            Http2ErrorCode::PROTOCOL_ERROR,
            Http2VisitorInterface::ConnectionError::kInvalidSetting);
        return;
      }
      max_frame_payload_ = value;
      break;
    case ENABLE_CONNECT_PROTOCOL:
      if (value > 1u || (value == 0 && peer_enables_connect_protocol_)) {
        visitor_.OnInvalidFrame(
            0, Http2VisitorInterface::InvalidFrameError::kProtocol);
        LatchErrorAndNotify(
            Http2ErrorCode::PROTOCOL_ERROR,
            Http2VisitorInterface::ConnectionError::kInvalidSetting);
        return;
      }
      peer_enables_connect_protocol_ = (value == 1u);
      break;
    default:
      // TODO(bnc): See if C++17 inline constants are allowed in QUICHE.
      if (id == kMetadataExtensionId) {
        peer_supports_metadata_ = (value != 0);
      } else {
        QUICHE_VLOG(1) << "Unimplemented SETTING id: " << id;
      }
  }
  visitor_.OnSetting({id, value});
}

void OgHttp2Session::OnSettingsEnd() { visitor_.OnSettingsEnd(); }

void OgHttp2Session::OnSettingsAck() {
  if (!settings_ack_callbacks_.empty()) {
    SettingsAckCallback callback = std::move(settings_ack_callbacks_.front());
    settings_ack_callbacks_.pop_front();
    std::move(callback)();
  }

  visitor_.OnSettingsAck();
}

void OgHttp2Session::OnPing(spdy::SpdyPingId unique_id, bool is_ack) {
  visitor_.OnPing(unique_id, is_ack);
  if (options_.auto_ping_ack && !is_ack) {
    auto ping = std::make_unique<spdy::SpdyPingIR>(unique_id);
    ping->set_is_ack(true);
    EnqueueFrame(std::move(ping));
  }
}

void OgHttp2Session::OnGoAway(spdy::SpdyStreamId last_accepted_stream_id,
                              spdy::SpdyErrorCode error_code) {
  if (received_goaway_ &&
      last_accepted_stream_id >
          static_cast<spdy::SpdyStreamId>(received_goaway_stream_id_)) {
    // This GOAWAY has a higher `last_accepted_stream_id` than a previous
    // GOAWAY, a connection-level spec violation.
    const bool ok = visitor_.OnInvalidFrame(
        kConnectionStreamId,
        Http2VisitorInterface::InvalidFrameError::kProtocol);
    if (!ok) {
      fatal_visitor_callback_failure_ = true;
    }
    LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                        ConnectionError::kInvalidGoAwayLastStreamId);
    return;
  }

  received_goaway_ = true;
  received_goaway_stream_id_ = last_accepted_stream_id;
  const bool result = visitor_.OnGoAway(last_accepted_stream_id,
                                        TranslateErrorCode(error_code), "");
  if (!result) {
    fatal_visitor_callback_failure_ = true;
    decoder_.StopProcessing();
  }

  // Close the streams above `last_accepted_stream_id`. Only applies if the
  // session receives a GOAWAY as a client, as we do not support server push.
  if (last_accepted_stream_id == spdy::kMaxStreamId || IsServerSession()) {
    return;
  }
  std::vector<Http2StreamId> streams_to_close;
  for (const auto& [stream_id, stream_state] : stream_map_) {
    if (static_cast<spdy::SpdyStreamId>(stream_id) > last_accepted_stream_id) {
      streams_to_close.push_back(stream_id);
    }
  }
  for (Http2StreamId stream_id : streams_to_close) {
    CloseStream(stream_id, Http2ErrorCode::REFUSED_STREAM);
  }
}

bool OgHttp2Session::OnGoAwayFrameData(const char* /*goaway_data*/, size_t
                                       /*len*/) {
  // Opaque data is currently ignored.
  return true;
}

void OgHttp2Session::OnHeaders(spdy::SpdyStreamId stream_id,
                               size_t /*payload_length*/, bool /*has_priority*/,
                               int /*weight*/,
                               spdy::SpdyStreamId /*parent_stream_id*/,
                               bool /*exclusive*/, bool fin, bool /*end*/) {
  if (stream_id % 2 == 0) {
    // Server push is disabled; receiving push HEADERS is a connection error.
    LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                        ConnectionError::kInvalidNewStreamId);
    return;
  }
  headers_handler_.set_frame_contains_fin(fin);
  if (IsServerSession()) {
    const auto new_stream_id = static_cast<Http2StreamId>(stream_id);
    if (stream_map_.find(new_stream_id) != stream_map_.end() && fin) {
      // Not a new stream, must be trailers.
      return;
    }
    if (new_stream_id <= highest_processed_stream_id_) {
      // A new stream ID lower than the watermark is a connection error.
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kInvalidNewStreamId);
      return;
    }

    if (stream_map_.size() >= max_inbound_concurrent_streams_) {
      // The new stream would exceed our advertised and acknowledged
      // MAX_CONCURRENT_STREAMS. For parity with nghttp2, treat this error as a
      // connection-level PROTOCOL_ERROR.
      bool ok = visitor_.OnInvalidFrame(
          stream_id, Http2VisitorInterface::InvalidFrameError::kProtocol);
      if (!ok) {
        fatal_visitor_callback_failure_ = true;
      }
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kExceededMaxConcurrentStreams);
      return;
    }
    if (stream_map_.size() >= pending_max_inbound_concurrent_streams_) {
      // The new stream would exceed our advertised but unacked
      // MAX_CONCURRENT_STREAMS. Refuse the stream for parity with nghttp2.
      EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
          stream_id, spdy::ERROR_CODE_REFUSED_STREAM));
      const bool ok = visitor_.OnInvalidFrame(
          stream_id, Http2VisitorInterface::InvalidFrameError::kRefusedStream);
      if (!ok) {
        fatal_visitor_callback_failure_ = true;
        LatchErrorAndNotify(Http2ErrorCode::REFUSED_STREAM,
                            ConnectionError::kExceededMaxConcurrentStreams);
      }
      return;
    }

    CreateStream(stream_id);
  }
}

void OgHttp2Session::OnWindowUpdate(spdy::SpdyStreamId stream_id,
                                    int delta_window_size) {
  constexpr int kMaxWindowValue = 2147483647;  // (1 << 31) - 1
  if (stream_id == 0) {
    if (delta_window_size == 0) {
      // A PROTOCOL_ERROR, according to RFC 9113 Section 6.9.
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kFlowControlError);
      return;
    }
    if (connection_send_window_ > 0 &&
        delta_window_size > (kMaxWindowValue - connection_send_window_)) {
      // Window overflow is a FLOW_CONTROL_ERROR.
      LatchErrorAndNotify(Http2ErrorCode::FLOW_CONTROL_ERROR,
                          ConnectionError::kFlowControlError);
      return;
    }
    connection_send_window_ += delta_window_size;
  } else {
    if (delta_window_size == 0) {
      // A PROTOCOL_ERROR, according to RFC 9113 Section 6.9.
      EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
          stream_id, spdy::ERROR_CODE_PROTOCOL_ERROR));
      return;
    }
    auto it = stream_map_.find(stream_id);
    if (it == stream_map_.end()) {
      QUICHE_VLOG(1) << "Stream " << stream_id << " not found!";
      if (static_cast<Http2StreamId>(stream_id) >
          highest_processed_stream_id_) {
        // Receiving WINDOW_UPDATE before HEADERS is a connection error.
        LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                            ConnectionError::kWrongFrameSequence);
      }
      // Do not inform the visitor of a WINDOW_UPDATE for a non-existent stream.
      return;
    } else {
      if (streams_reset_.contains(stream_id)) {
        return;
      }
      if (it->second.send_window > 0 &&
          delta_window_size > (kMaxWindowValue - it->second.send_window)) {
        // Window overflow is a FLOW_CONTROL_ERROR.
        EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
            stream_id, spdy::ERROR_CODE_FLOW_CONTROL_ERROR));
        return;
      }
      const bool was_blocked = (it->second.send_window <= 0);
      it->second.send_window += delta_window_size;
      if (was_blocked && it->second.send_window > 0) {
        // The stream was blocked on flow control.
        QUICHE_VLOG(1) << "Marking stream " << stream_id << " ready to write.";
        write_scheduler_.MarkStreamReady(stream_id, false);
      }
    }
  }
  visitor_.OnWindowUpdate(stream_id, delta_window_size);
}

void OgHttp2Session::OnPushPromise(spdy::SpdyStreamId /*stream_id*/,
                                   spdy::SpdyStreamId /*promised_stream_id*/,
                                   bool /*end*/) {
  // Server push is disabled; PUSH_PROMISE is an invalid frame.
  LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                      ConnectionError::kInvalidPushPromise);
}

void OgHttp2Session::OnContinuation(spdy::SpdyStreamId /*stream_id*/,
                                    size_t /*payload_length*/, bool /*end*/) {}

void OgHttp2Session::OnAltSvc(spdy::SpdyStreamId /*stream_id*/,
                              absl::string_view /*origin*/,
                              const spdy::SpdyAltSvcWireFormat::
                                  AlternativeServiceVector& /*altsvc_vector*/) {
}

void OgHttp2Session::OnPriority(spdy::SpdyStreamId /*stream_id*/,
                                spdy::SpdyStreamId /*parent_stream_id*/,
                                int /*weight*/, bool /*exclusive*/) {}

void OgHttp2Session::OnPriorityUpdate(
    spdy::SpdyStreamId /*prioritized_stream_id*/,
    absl::string_view /*priority_field_value*/) {}

bool OgHttp2Session::OnUnknownFrame(spdy::SpdyStreamId /*stream_id*/,
                                    uint8_t /*frame_type*/) {
  return true;
}

void OgHttp2Session::OnUnknownFrameStart(spdy::SpdyStreamId stream_id,
                                         size_t length, uint8_t type,
                                         uint8_t flags) {
  process_metadata_ = false;
  if (streams_reset_.contains(stream_id)) {
    return;
  }
  if (type == kMetadataFrameType) {
    QUICHE_DCHECK_EQ(metadata_length_, 0u);
    visitor_.OnBeginMetadataForStream(stream_id, length);
    metadata_length_ = length;
    process_metadata_ = true;
    end_metadata_ = flags & kMetadataEndFlag;

    // Empty metadata payloads will not trigger OnUnknownFramePayload(), so
    // handle that possibility here.
    MaybeHandleMetadataEndForStream(stream_id);
  } else {
    QUICHE_DLOG(INFO) << "Received unexpected frame type "
                      << static_cast<int>(type);
  }
}

void OgHttp2Session::OnUnknownFramePayload(spdy::SpdyStreamId stream_id,
                                           absl::string_view payload) {
  if (!process_metadata_) {
    return;
  }
  if (streams_reset_.contains(stream_id)) {
    return;
  }
  if (metadata_length_ > 0) {
    QUICHE_DCHECK_LE(payload.size(), metadata_length_);
    const bool payload_success =
        visitor_.OnMetadataForStream(stream_id, payload);
    if (payload_success) {
      metadata_length_ -= payload.size();
      MaybeHandleMetadataEndForStream(stream_id);
    } else {
      fatal_visitor_callback_failure_ = true;
      decoder_.StopProcessing();
    }
  } else {
    QUICHE_DLOG(INFO) << "Unexpected metadata payload for stream " << stream_id;
  }
}

void OgHttp2Session::OnHeaderStatus(
    Http2StreamId stream_id, Http2VisitorInterface::OnHeaderResult result) {
  QUICHE_DCHECK_NE(result, Http2VisitorInterface::HEADER_OK);
  QUICHE_VLOG(1) << "OnHeaderStatus(stream_id=" << stream_id
                 << ", result=" << result << ")";
  const bool should_reset_stream =
      result == Http2VisitorInterface::HEADER_RST_STREAM ||
      result == Http2VisitorInterface::HEADER_FIELD_INVALID ||
      result == Http2VisitorInterface::HEADER_HTTP_MESSAGING;
  if (should_reset_stream) {
    const Http2ErrorCode error_code =
        (result == Http2VisitorInterface::HEADER_RST_STREAM)
            ? Http2ErrorCode::INTERNAL_ERROR
            : Http2ErrorCode::PROTOCOL_ERROR;
    const spdy::SpdyErrorCode spdy_error_code = TranslateErrorCode(error_code);
    const Http2VisitorInterface::InvalidFrameError frame_error =
        (result == Http2VisitorInterface::HEADER_RST_STREAM ||
         result == Http2VisitorInterface::HEADER_FIELD_INVALID)
            ? Http2VisitorInterface::InvalidFrameError::kHttpHeader
            : Http2VisitorInterface::InvalidFrameError::kHttpMessaging;
    auto it = streams_reset_.find(stream_id);
    if (it == streams_reset_.end()) {
      EnqueueFrame(
          std::make_unique<spdy::SpdyRstStreamIR>(stream_id, spdy_error_code));

      if (result == Http2VisitorInterface::HEADER_FIELD_INVALID ||
          result == Http2VisitorInterface::HEADER_HTTP_MESSAGING) {
        const bool ok = visitor_.OnInvalidFrame(stream_id, frame_error);
        if (!ok) {
          fatal_visitor_callback_failure_ = true;
          LatchErrorAndNotify(error_code, ConnectionError::kHeaderError);
        }
      }
    }
  } else if (result == Http2VisitorInterface::HEADER_CONNECTION_ERROR) {
    fatal_visitor_callback_failure_ = true;
    LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                        ConnectionError::kHeaderError);
  } else if (result == Http2VisitorInterface::HEADER_COMPRESSION_ERROR) {
    LatchErrorAndNotify(Http2ErrorCode::COMPRESSION_ERROR,
                        ConnectionError::kHeaderError);
  }
}

void OgHttp2Session::MaybeSetupPreface(bool sending_outbound_settings) {
  if (!queued_preface_) {
    queued_preface_ = true;
    if (!IsServerSession()) {
      buffered_data_.Append(
          absl::string_view(spdy::kHttp2ConnectionHeaderPrefix,
                            spdy::kHttp2ConnectionHeaderPrefixSize));
    }
    if (!sending_outbound_settings) {
      QUICHE_DCHECK(frames_.empty());
      // First frame must be a non-ack SETTINGS.
      EnqueueFrame(PrepareSettingsFrame(GetInitialSettings()));
    }
  }
}

std::vector<Http2Setting> OgHttp2Session::GetInitialSettings() const {
  std::vector<Http2Setting> settings;
  if (!IsServerSession()) {
    // Disable server push. Note that server push from clients is already
    // disabled, so the server does not need to send this disabling setting.
    // TODO(diannahu): Consider applying server push disabling on SETTINGS ack.
    settings.push_back({Http2KnownSettingsId::ENABLE_PUSH, 0});
  }
  if (options_.max_header_list_bytes) {
    settings.push_back({Http2KnownSettingsId::MAX_HEADER_LIST_SIZE,
                        *options_.max_header_list_bytes});
  }
  if (options_.allow_extended_connect && IsServerSession()) {
    settings.push_back({Http2KnownSettingsId::ENABLE_CONNECT_PROTOCOL, 1u});
  }
  return settings;
}

std::unique_ptr<SpdySettingsIR> OgHttp2Session::PrepareSettingsFrame(
    absl::Span<const Http2Setting> settings) {
  auto settings_ir = std::make_unique<SpdySettingsIR>();
  for (const Http2Setting& setting : settings) {
    settings_ir->AddSetting(setting.id, setting.value);
  }
  return settings_ir;
}

void OgHttp2Session::HandleOutboundSettings(
    const spdy::SpdySettingsIR& settings_frame) {
  for (const auto& [id, value] : settings_frame.values()) {
    switch (static_cast<Http2KnownSettingsId>(id)) {
      case MAX_CONCURRENT_STREAMS:
        pending_max_inbound_concurrent_streams_ = value;
        break;
      case ENABLE_CONNECT_PROTOCOL:
        if (value == 1u && IsServerSession()) {
          // Allow extended CONNECT semantics even before SETTINGS are acked, to
          // make things easier for clients.
          headers_handler_.SetAllowExtendedConnect();
        }
        break;
      case HEADER_TABLE_SIZE:
      case ENABLE_PUSH:
      case INITIAL_WINDOW_SIZE:
      case MAX_FRAME_SIZE:
      case MAX_HEADER_LIST_SIZE:
        QUICHE_VLOG(2)
            << "Not adjusting internal state for outbound setting with id "
            << id;
        break;
    }
  }

  // Copy the (small) map of settings we are about to send so that we can set
  // values in the SETTINGS ack callback.
  settings_ack_callbacks_.push_back(
      [this, settings_map = settings_frame.values()]() {
        for (const auto& [id, value] : settings_map) {
          switch (static_cast<Http2KnownSettingsId>(id)) {
            case MAX_CONCURRENT_STREAMS:
              max_inbound_concurrent_streams_ = value;
              break;
            case HEADER_TABLE_SIZE:
              decoder_.GetHpackDecoder().ApplyHeaderTableSizeSetting(value);
              break;
            case INITIAL_WINDOW_SIZE:
              UpdateStreamReceiveWindowSizes(value);
              initial_stream_receive_window_ = value;
              break;
            case MAX_FRAME_SIZE:
              decoder_.SetMaxFrameSize(value);
              break;
            case ENABLE_PUSH:
            case MAX_HEADER_LIST_SIZE:
            case ENABLE_CONNECT_PROTOCOL:
              QUICHE_VLOG(2)
                  << "No action required in ack for outbound setting with id "
                  << id;
              break;
          }
        }
      });
}

void OgHttp2Session::SendWindowUpdate(Http2StreamId stream_id,
                                      size_t update_delta) {
  EnqueueFrame(
      std::make_unique<spdy::SpdyWindowUpdateIR>(stream_id, update_delta));
}

void OgHttp2Session::SendHeaders(Http2StreamId stream_id,
                                 spdy::Http2HeaderBlock headers,
                                 bool end_stream) {
  auto frame =
      std::make_unique<spdy::SpdyHeadersIR>(stream_id, std::move(headers));
  frame->set_fin(end_stream);
  EnqueueFrame(std::move(frame));
}

void OgHttp2Session::SendTrailers(Http2StreamId stream_id,
                                  spdy::Http2HeaderBlock trailers) {
  auto frame =
      std::make_unique<spdy::SpdyHeadersIR>(stream_id, std::move(trailers));
  frame->set_fin(true);
  EnqueueFrame(std::move(frame));
  trailers_ready_.erase(stream_id);
}

void OgHttp2Session::MaybeFinWithRstStream(StreamStateMap::iterator iter) {
  QUICHE_DCHECK(iter != stream_map_.end() && iter->second.half_closed_local);

  if (options_.rst_stream_no_error_when_incomplete && IsServerSession() &&
      !iter->second.half_closed_remote) {
    // Since the peer has not yet ended the stream, this endpoint should
    // send a RST_STREAM NO_ERROR. See RFC 7540 Section 8.1.
    EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
        iter->first, spdy::SpdyErrorCode::ERROR_CODE_NO_ERROR));
    iter->second.half_closed_remote = true;
  }
}

void OgHttp2Session::MarkDataBuffered(Http2StreamId stream_id, size_t bytes) {
  connection_window_manager_.MarkDataBuffered(bytes);
  if (auto it = stream_map_.find(stream_id); it != stream_map_.end()) {
    it->second.window_manager.MarkDataBuffered(bytes);
  }
}

OgHttp2Session::StreamStateMap::iterator OgHttp2Session::CreateStream(
    Http2StreamId stream_id) {
  WindowManager::WindowUpdateListener listener =
      [this, stream_id](size_t window_update_delta) {
        SendWindowUpdate(stream_id, window_update_delta);
      };
  auto [iter, inserted] = stream_map_.try_emplace(
      stream_id,
      StreamState(initial_stream_receive_window_, initial_stream_send_window_,
                  std::move(listener), options_.should_window_update_fn));
  if (inserted) {
    // Add the stream to the write scheduler.
    const spdy::SpdyPriority priority = 3;
    write_scheduler_.RegisterStream(stream_id, priority);

    highest_processed_stream_id_ =
        std::max(highest_processed_stream_id_, stream_id);
  }
  return iter;
}

void OgHttp2Session::StartRequest(Http2StreamId stream_id,
                                  spdy::Http2HeaderBlock headers,
                                  std::unique_ptr<DataFrameSource> data_source,
                                  void* user_data, bool end_stream) {
  if (received_goaway_) {
    // Do not start new streams after receiving a GOAWAY.
    goaway_rejected_streams_.insert(stream_id);
    return;
  }

  auto iter = CreateStream(stream_id);
  if (data_source != nullptr) {
    iter->second.outbound_body = std::move(data_source);
    write_scheduler_.MarkStreamReady(stream_id, false);
  } else if (!end_stream) {
    iter->second.check_visitor_for_body = true;
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  iter->second.user_data = user_data;
  for (const auto& [name, value] : headers) {
    if (name == kHttp2MethodPseudoHeader && value == kHeadValue) {
      iter->second.sent_head_method = true;
    }
  }
  SendHeaders(stream_id, std::move(headers), end_stream);
}

void OgHttp2Session::StartPendingStreams() {
  while (!pending_streams_.empty() && CanCreateStream()) {
    auto& [stream_id, pending_stream] = pending_streams_.front();
    StartRequest(stream_id, std::move(pending_stream.headers),
                 std::move(pending_stream.data_source),
                 pending_stream.user_data, pending_stream.end_stream);
    pending_streams_.pop_front();
  }
}

void OgHttp2Session::CloseStream(Http2StreamId stream_id,
                                 Http2ErrorCode error_code) {
  const bool result = visitor_.OnCloseStream(stream_id, error_code);
  if (!result) {
    latched_error_ = true;
    decoder_.StopProcessing();
  }
  stream_map_.erase(stream_id);
  trailers_ready_.erase(stream_id);
  streams_reset_.erase(stream_id);
  auto queued_it = queued_frames_.find(stream_id);
  if (queued_it != queued_frames_.end()) {
    // Remove any queued frames for this stream.
    int frames_remaining = queued_it->second;
    queued_frames_.erase(queued_it);
    for (auto it = frames_.begin();
         frames_remaining > 0 && it != frames_.end();) {
      if (static_cast<Http2StreamId>((*it)->stream_id()) == stream_id) {
        it = frames_.erase(it);
        --frames_remaining;
      } else {
        ++it;
      }
    }
  }
  if (write_scheduler_.StreamRegistered(stream_id)) {
    write_scheduler_.UnregisterStream(stream_id);
  }

  StartPendingStreams();
}

bool OgHttp2Session::CanCreateStream() const {
  return stream_map_.size() < max_outbound_concurrent_streams_;
}

HeaderType OgHttp2Session::NextHeaderType(
    std::optional<HeaderType> current_type) {
  if (IsServerSession()) {
    if (!current_type) {
      return HeaderType::REQUEST;
    } else {
      return HeaderType::REQUEST_TRAILER;
    }
  } else if (!current_type || *current_type == HeaderType::RESPONSE_100) {
    return HeaderType::RESPONSE;
  } else {
    return HeaderType::RESPONSE_TRAILER;
  }
}

void OgHttp2Session::LatchErrorAndNotify(Http2ErrorCode error_code,
                                         ConnectionError error) {
  if (latched_error_) {
    // Do not kick a connection when it is down.
    return;
  }

  latched_error_ = true;
  visitor_.OnConnectionError(error);
  decoder_.StopProcessing();
  EnqueueFrame(std::make_unique<spdy::SpdyGoAwayIR>(
      highest_processed_stream_id_, TranslateErrorCode(error_code),
      ConnectionErrorToString(error)));
}

void OgHttp2Session::CloseStreamIfReady(uint8_t frame_type,
                                        uint32_t stream_id) {
  auto iter = stream_map_.find(stream_id);
  if (iter == stream_map_.end()) {
    return;
  }
  const StreamState& state = iter->second;
  if (static_cast<FrameType>(frame_type) == FrameType::RST_STREAM ||
      (state.half_closed_local && state.half_closed_remote)) {
    CloseStream(stream_id, Http2ErrorCode::HTTP2_NO_ERROR);
  }
}

void OgHttp2Session::CloseGoAwayRejectedStreams() {
  for (Http2StreamId stream_id : goaway_rejected_streams_) {
    const bool result =
        visitor_.OnCloseStream(stream_id, Http2ErrorCode::REFUSED_STREAM);
    if (!result) {
      latched_error_ = true;
      decoder_.StopProcessing();
    }
  }
  goaway_rejected_streams_.clear();
}

void OgHttp2Session::PrepareForImmediateGoAway() {
  queued_immediate_goaway_ = true;

  // Keep the initial SETTINGS frame if the session has SETTINGS at the front of
  // the queue but has not sent SETTINGS yet. The session should send initial
  // SETTINGS before GOAWAY.
  std::unique_ptr<spdy::SpdyFrameIR> initial_settings;
  if (!sent_non_ack_settings_ && !frames_.empty() &&
      IsNonAckSettings(*frames_.front())) {
    initial_settings = std::move(frames_.front());
    frames_.pop_front();
  }

  // Remove all pending frames except for RST_STREAMs. It is important to send
  // RST_STREAMs so the peer knows of errors below the GOAWAY last stream ID.
  // TODO(diannahu): Consider informing the visitor of dropped frames. This may
  // mean keeping the frames and invoking a frame-not-sent callback, similar to
  // nghttp2. Could add a closure to each frame in the frames queue.
  frames_.remove_if([](const auto& frame) {
    return frame->frame_type() != spdy::SpdyFrameType::RST_STREAM;
  });

  if (initial_settings != nullptr) {
    frames_.push_front(std::move(initial_settings));
  }
}

void OgHttp2Session::MaybeHandleMetadataEndForStream(Http2StreamId stream_id) {
  if (metadata_length_ == 0 && end_metadata_) {
    const bool completion_success = visitor_.OnMetadataEndForStream(stream_id);
    if (!completion_success) {
      fatal_visitor_callback_failure_ = true;
      decoder_.StopProcessing();
    }
    process_metadata_ = false;
    end_metadata_ = false;
  }
}

void OgHttp2Session::DecrementQueuedFrameCount(uint32_t stream_id,
                                               uint8_t frame_type) {
  auto iter = queued_frames_.find(stream_id);
  if (iter == queued_frames_.end()) {
    QUICHE_LOG(ERROR) << "Unable to find a queued frame count for stream "
                      << stream_id;
    return;
  }
  if (static_cast<FrameType>(frame_type) != FrameType::DATA) {
    --iter->second;
  }
  if (iter->second == 0) {
    // TODO(birenroy): Consider passing through `error_code` here.
    CloseStreamIfReady(frame_type, stream_id);
  }
}

void OgHttp2Session::HandleContentLengthError(Http2StreamId stream_id) {
  if (current_frame_type_ == static_cast<uint8_t>(FrameType::HEADERS)) {
    // For consistency, either OnInvalidFrame should always be invoked,
    // regardless of frame type, or perhaps we should introduce an OnStreamError
    // callback.
    visitor_.OnInvalidFrame(
        stream_id, Http2VisitorInterface::InvalidFrameError::kHttpMessaging);
  }
  EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
      stream_id, spdy::ERROR_CODE_PROTOCOL_ERROR));
}

void OgHttp2Session::UpdateReceiveWindow(Http2StreamId stream_id,
                                         int32_t delta) {
  if (stream_id == 0) {
    connection_window_manager_.IncreaseWindow(delta);
    // TODO(b/181586191): Provide an explicit way to set the desired window
    // limit, remove the upsize-on-window-update behavior.
    const int64_t current_window =
        connection_window_manager_.CurrentWindowSize();
    if (current_window > connection_window_manager_.WindowSizeLimit()) {
      connection_window_manager_.SetWindowSizeLimit(current_window);
    }
  } else {
    auto iter = stream_map_.find(stream_id);
    if (iter != stream_map_.end()) {
      WindowManager& manager = iter->second.window_manager;
      manager.IncreaseWindow(delta);
      // TODO(b/181586191): Provide an explicit way to set the desired window
      // limit, remove the upsize-on-window-update behavior.
      const int64_t current_window = manager.CurrentWindowSize();
      if (current_window > manager.WindowSizeLimit()) {
        manager.SetWindowSizeLimit(current_window);
      }
    }
  }
}

void OgHttp2Session::UpdateStreamSendWindowSizes(uint32_t new_value) {
  const int32_t delta =
      static_cast<int32_t>(new_value) - initial_stream_send_window_;
  initial_stream_send_window_ = new_value;
  for (auto& [stream_id, stream_state] : stream_map_) {
    const int64_t current_window_size = stream_state.send_window;
    const int64_t new_window_size = current_window_size + delta;
    if (new_window_size > spdy::kSpdyMaximumWindowSize) {
      EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
          stream_id, spdy::ERROR_CODE_FLOW_CONTROL_ERROR));
    } else {
      stream_state.send_window += delta;
    }
    if (current_window_size <= 0 && new_window_size > 0) {
      write_scheduler_.MarkStreamReady(stream_id, false);
    }
  }
}

void OgHttp2Session::UpdateStreamReceiveWindowSizes(uint32_t new_value) {
  for (auto& [stream_id, stream_state] : stream_map_) {
    stream_state.window_manager.OnWindowSizeLimitChange(new_value);
  }
}

bool OgHttp2Session::HasMoreData(const StreamState& stream_state) const {
  return stream_state.outbound_body != nullptr ||
         stream_state.check_visitor_for_body;
}

bool OgHttp2Session::IsReadyToWriteData(const StreamState& stream_state) const {
  return HasMoreData(stream_state) && !stream_state.data_deferred;
}

void OgHttp2Session::AbandonData(StreamState& stream_state) {
  stream_state.outbound_body = nullptr;
  stream_state.check_visitor_for_body = false;
}

OgHttp2Session::DataFrameHeaderInfo OgHttp2Session::GetDataFrameInfo(
    Http2StreamId stream_id, size_t flow_control_available,
    StreamState& stream_state) {
  if (stream_state.outbound_body != nullptr) {
    DataFrameHeaderInfo info;
    std::tie(info.payload_length, info.end_data) =
        stream_state.outbound_body->SelectPayloadLength(flow_control_available);
    info.end_stream =
        info.end_data ? stream_state.outbound_body->send_fin() : false;
    return info;
  } else if (stream_state.check_visitor_for_body) {
    DataFrameHeaderInfo info =
        visitor_.OnReadyToSendDataForStream(stream_id, flow_control_available);
    info.end_data = info.end_data || info.end_stream;
    return info;
  }
  QUICHE_LOG(DFATAL) << "GetDataFrameInfo for stream " << stream_id
                     << " but no body available!";
  return {/*payload_length=*/0, /*end_data=*/true, /*end_stream=*/true};
}

bool OgHttp2Session::SendDataFrame(Http2StreamId stream_id,
                                   absl::string_view frame_header,
                                   size_t payload_length,
                                   StreamState& stream_state) {
  if (stream_state.outbound_body != nullptr) {
    return stream_state.outbound_body->Send(frame_header, payload_length);
  } else {
    QUICHE_DCHECK(stream_state.check_visitor_for_body);
    return visitor_.SendDataFrame(stream_id, frame_header, payload_length);
  }
}

}  // namespace adapter
}  // namespace http2
