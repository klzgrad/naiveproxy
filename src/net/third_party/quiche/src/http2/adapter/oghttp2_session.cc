#include "http2/adapter/oghttp2_session.h"

#include <tuple>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/http2_util.h"
#include "http2/adapter/http2_visitor_interface.h"
#include "http2/adapter/oghttp2_util.h"
#include "spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

namespace {

using ConnectionError = Http2VisitorInterface::ConnectionError;
using SpdyFramerError = Http2DecoderAdapter::SpdyFramerError;

using ::spdy::SpdySettingsIR;

// #define OGHTTP2_DEBUG_TRACE 1

#ifdef OGHTTP2_DEBUG_TRACE
const bool kTraceLoggingEnabled = true;
#else
const bool kTraceLoggingEnabled = false;
#endif

const uint32_t kMaxAllowedMetadataFrameSize = 65536u;
const uint32_t kDefaultHpackTableCapacity = 4096u;
const uint32_t kMaximumHpackTableCapacity = 65536u;

// TODO(birenroy): Consider incorporating spdy::FlagsSerializionVisitor here.
class FrameAttributeCollector : public spdy::SpdyFrameVisitor {
 public:
  FrameAttributeCollector() = default;
  void VisitData(const spdy::SpdyDataIR& data) override {
    frame_type_ = static_cast<uint8_t>(data.frame_type());
    stream_id_ = data.stream_id();
    flags_ = (data.fin() ? 0x1 : 0) | (data.padded() ? 0x8 : 0);
  }
  void VisitHeaders(const spdy::SpdyHeadersIR& headers) override {
    frame_type_ = static_cast<uint8_t>(headers.frame_type());
    stream_id_ = headers.stream_id();
    flags_ = 0x4 | (headers.fin() ? 0x1 : 0) | (headers.padded() ? 0x8 : 0) |
             (headers.has_priority() ? 0x20 : 0);
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
    flags_ = (settings.is_ack() ? 0x1 : 0);
  }
  void VisitPushPromise(const spdy::SpdyPushPromiseIR& push_promise) override {
    frame_type_ = static_cast<uint8_t>(push_promise.frame_type());
    frame_type_ = 5;
    stream_id_ = push_promise.stream_id();
    flags_ = (push_promise.padded() ? 0x8 : 0);
  }
  void VisitPing(const spdy::SpdyPingIR& ping) override {
    frame_type_ = static_cast<uint8_t>(ping.frame_type());
    frame_type_ = 6;
    flags_ = (ping.is_ack() ? 0x1 : 0);
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
    flags_ = continuation.end_headers() ? 0x4 : 0;
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

class RunOnExit {
 public:
  RunOnExit() = default;
  explicit RunOnExit(std::function<void()> f) : f_(std::move(f)) {}

  RunOnExit(const RunOnExit& other) = delete;
  RunOnExit& operator=(const RunOnExit& other) = delete;
  RunOnExit(RunOnExit&& other) = delete;
  RunOnExit& operator=(RunOnExit&& other) = delete;

  ~RunOnExit() {
    if (f_) {
      f_();
    }
    f_ = {};
  }

  void emplace(std::function<void()> f) { f_ = std::move(f); }

 private:
  std::function<void()> f_;
};

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

}  // namespace

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockStart() {
  result_ = Http2VisitorInterface::HEADER_OK;
  const bool status = visitor_.OnBeginHeadersForStream(stream_id_);
  if (!status) {
    result_ = Http2VisitorInterface::HEADER_CONNECTION_ERROR;
  }
  validator_.StartHeaderBlock();
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeader(
    absl::string_view key,
    absl::string_view value) {
  if (result_ != Http2VisitorInterface::HEADER_OK) {
    QUICHE_VLOG(2) << "Early return; status not HEADER_OK";
    return;
  }
  const auto validation_result = validator_.ValidateSingleHeader(key, value);
  if (validation_result == HeaderValidator::HEADER_VALUE_INVALID_STATUS) {
    QUICHE_VLOG(2) << "RST_STREAM: invalid status found";
    result_ = Http2VisitorInterface::HEADER_HTTP_MESSAGING;
    return;
  } else if (validation_result != HeaderValidator::HEADER_OK) {
    QUICHE_VLOG(2) << "RST_STREAM: invalid header found";
    // TODO(birenroy): consider updating this to return HEADER_HTTP_MESSAGING.
    result_ = Http2VisitorInterface::HEADER_RST_STREAM;
    return;
  }
  result_ = visitor_.OnHeaderForStream(stream_id_, key, value);
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockEnd(
    size_t /* uncompressed_header_bytes */,
    size_t /* compressed_header_bytes */) {
  if (result_ == Http2VisitorInterface::HEADER_OK) {
    if (!validator_.FinishHeaderBlock(type_)) {
      result_ = Http2VisitorInterface::HEADER_RST_STREAM;
    }
  }
  if (frame_contains_fin_ && IsResponse(type_) &&
      StatusIs1xx(status_header())) {
    // Unexpected end of stream without final headers.
    result_ = Http2VisitorInterface::HEADER_HTTP_MESSAGING;
  }
  if (result_ == Http2VisitorInterface::HEADER_OK) {
    const bool result = visitor_.OnEndHeadersForStream(stream_id_);
    if (!result) {
      session_.decoder_.StopProcessing();
    }
  } else {
    session_.OnHeaderStatus(stream_id_, result_);
  }
  frame_contains_fin_ = false;
}

OgHttp2Session::OgHttp2Session(Http2VisitorInterface& visitor, Options options)
    : visitor_(visitor),
      event_forwarder_([this]() { return !latched_error_; }, *this),
      receive_logger_(
          &event_forwarder_, TracePerspectiveAsString(options.perspective),
          []() { return kTraceLoggingEnabled; }, this),
      send_logger_(
          TracePerspectiveAsString(options.perspective),
          []() { return kTraceLoggingEnabled; }, this),
      headers_handler_(*this, visitor),
      noop_headers_handler_(/*listener=*/nullptr),
      connection_window_manager_(kInitialFlowControlWindowSize,
                                 [this](size_t window_update_delta) {
                                   SendWindowUpdate(kConnectionStreamId,
                                                    window_update_delta);
                                 }),
      options_(options) {
  decoder_.set_visitor(&receive_logger_);
  decoder_.set_extension_visitor(this);
  if (options_.perspective == Perspective::kServer) {
    remaining_preface_ = {spdy::kHttp2ConnectionHeaderPrefix,
                          spdy::kHttp2ConnectionHeaderPrefixSize};
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
  return nullptr;
}

bool OgHttp2Session::ResumeStream(Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end() || it->second.outbound_body == nullptr ||
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
  const spdy::HpackDecoderAdapter* decoder = decoder_.GetHpackDecoder();
  return decoder == nullptr ? 0 : decoder->GetDynamicTableSize();
}

int OgHttp2Session::GetHpackDecoderSizeLimit() const {
  const spdy::HpackDecoderAdapter* decoder = decoder_.GetHpackDecoder();
  return decoder == nullptr ? 0 : decoder->GetCurrentHeaderTableSizeSetting();
}

int64_t OgHttp2Session::ProcessBytes(absl::string_view bytes) {
  QUICHE_VLOG(2) << TracePerspectiveAsString(options_.perspective)
                 << " processing [" << absl::CEscape(bytes) << "]";
  if (processing_bytes_) {
    QUICHE_VLOG(1) << "Returning early; already processing bytes.";
    return 0;
  }
  processing_bytes_ = true;
  RunOnExit r{[this]() { processing_bytes_ = false; }};

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
      return -1;
    }
    remaining_preface_.remove_prefix(min_size);
    bytes.remove_prefix(min_size);
    if (!remaining_preface_.empty()) {
      QUICHE_VLOG(2) << "Preface bytes remaining: "
                     << remaining_preface_.size();
      return min_size;
    }
    preface_consumed = min_size;
  }
  int64_t result = decoder_.ProcessInput(bytes.data(), bytes.size());
  if (latched_error_) {
    QUICHE_VLOG(2) << "ProcessBytes encountered an error.";
    return -1;
  }
  const int64_t ret = result < 0 ? result : result + preface_consumed;
  QUICHE_VLOG(2) << "ProcessBytes returning: " << ret;
  return ret;
}

int OgHttp2Session::Consume(Http2StreamId stream_id, size_t num_bytes) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    // TODO(b/181586191): LOG_ERROR rather than QUICHE_BUG.
    QUICHE_BUG(stream_consume_notfound)
        << "Stream " << stream_id << " not found";
  } else {
    it->second.window_manager.MarkDataFlushed(num_bytes);
  }
  connection_window_manager_.MarkDataFlushed(num_bytes);
  return 0;  // Remove?
}

void OgHttp2Session::StartGracefulShutdown() {
  if (options_.perspective == Perspective::kServer) {
    if (!queued_goaway_) {
      EnqueueFrame(absl::make_unique<spdy::SpdyGoAwayIR>(
          std::numeric_limits<int32_t>::max(), spdy::ERROR_CODE_NO_ERROR,
          "graceful_shutdown"));
    }
  } else {
    QUICHE_LOG(ERROR) << "Graceful shutdown not needed for clients.";
  }
}

void OgHttp2Session::EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame) {
  RunOnExit r;
  if (frame->frame_type() == spdy::SpdyFrameType::GOAWAY) {
    queued_goaway_ = true;
  } else if (frame->fin() ||
             frame->frame_type() == spdy::SpdyFrameType::RST_STREAM) {
    auto iter = stream_map_.find(frame->stream_id());
    if (iter != stream_map_.end()) {
      iter->second.half_closed_local = true;
    }
    if (frame->frame_type() == spdy::SpdyFrameType::RST_STREAM) {
      streams_reset_.insert(frame->stream_id());
    } else if (iter != stream_map_.end()) {
      // Enqueue RST_STREAM NO_ERROR if appropriate.
      r.emplace([this, iter]() { MaybeFinWithRstStream(iter); });
    }
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
  RunOnExit r{[this]() { sending_ = false; }};

  MaybeSetupPreface();

  SendResult continue_writing = SendQueuedFrames();
  while (continue_writing == SendResult::SEND_OK &&
         !connection_metadata_.empty()) {
    continue_writing = SendMetadata(0, connection_metadata_);
  }
  // Wake streams for writes.
  while (continue_writing == SendResult::SEND_OK &&
         write_scheduler_.HasReadyStreams() && connection_send_window_ > 0) {
    const Http2StreamId stream_id = write_scheduler_.PopNextReadyStream();
    // TODO(birenroy): Add a return value to indicate write blockage, so streams
    // aren't woken unnecessarily.
    QUICHE_VLOG(1) << "Waking stream " << stream_id << " for writes.";
    continue_writing = WriteForStream(stream_id);
  }
  if (continue_writing == SendResult::SEND_OK) {
    continue_writing = SendQueuedFrames();
  }
  return continue_writing == SendResult::SEND_ERROR ? -1 : 0;
}

OgHttp2Session::SendResult OgHttp2Session::MaybeSendBufferedData() {
  int64_t result = std::numeric_limits<int64_t>::max();
  while (result > 0 && !buffered_data_.empty()) {
    result = visitor_.OnReadyToSend(buffered_data_);
    if (result > 0) {
      buffered_data_.erase(0, result);
    }
  }
  if (result < 0) {
    LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                        ConnectionError::kSendError);
    return SendResult::SEND_ERROR;
  }
  return buffered_data_.empty() ? SendResult::SEND_OK
                                : SendResult::SEND_BLOCKED;
}

OgHttp2Session::SendResult OgHttp2Session::SendQueuedFrames() {
  // Flush any serialized prefix.
  const SendResult result = MaybeSendBufferedData();
  if (result != SendResult::SEND_OK) {
    return result;
  }
  // Serialize and send frames in the queue.
  while (!frames_.empty()) {
    const auto& frame_ptr = frames_.front();
    FrameAttributeCollector c;
    frame_ptr->Visit(&c);
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
      AfterFrameSent(c.frame_type(), c.stream_id(), frame_payload_length,
                     c.flags(), c.error_code());

      frames_.pop_front();
      if (static_cast<size_t>(result) < frame.size()) {
        // The frame was partially written, so the rest must be buffered.
        buffered_data_.append(frame.data() + result, frame.size() - result);
        return SendResult::SEND_BLOCKED;
      }
    }
  }
  return SendResult::SEND_OK;
}

void OgHttp2Session::AfterFrameSent(uint8_t frame_type, uint32_t stream_id,
                                    size_t payload_length, uint8_t flags,
                                    uint32_t error_code) {
  visitor_.OnFrameSent(frame_type, stream_id, payload_length, flags,
                       error_code);
  if (stream_id == 0) {
    const bool is_settings_ack =
        static_cast<FrameType>(frame_type) == FrameType::SETTINGS &&
        (flags & 0x01);
    if (is_settings_ack && encoder_header_table_capacity_when_acking_) {
      framer_.UpdateHeaderEncoderTableSize(
          encoder_header_table_capacity_when_acking_.value());
      encoder_header_table_capacity_when_acking_ = absl::nullopt;
    }
    return;
  }
  auto iter = queued_frames_.find(stream_id);
  if (frame_type != 0) {
    --iter->second;
  }
  if (iter->second == 0) {
    // TODO(birenroy): Consider passing through `error_code` here.
    CloseStreamIfReady(frame_type, stream_id);
  }
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
  SendResult connection_can_write = SendResult::SEND_OK;
  if (!state.outbound_metadata.empty()) {
    connection_can_write = SendMetadata(stream_id, state.outbound_metadata);
  }

  if (state.outbound_body == nullptr) {
    // No data to send, but there might be trailers.
    if (state.trailers != nullptr) {
      auto block_ptr = std::move(state.trailers);
      if (state.half_closed_local) {
        QUICHE_LOG(ERROR) << "Sent fin; can't send trailers.";
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
         state.outbound_body != nullptr && !state.data_deferred) {
    int64_t length;
    bool end_data;
    std::tie(length, end_data) =
        state.outbound_body->SelectPayloadLength(available_window);
    QUICHE_VLOG(2) << "WriteForStream | length: " << length
                   << " end_data: " << end_data
                   << " trailers: " << state.trailers.get();
    if (length == 0 && !end_data &&
        (options_.trailers_require_end_data || state.trailers == nullptr)) {
      // An unproductive call to SelectPayloadLength() results in this stream
      // entering the "deferred" state only if either no trailers are available
      // to send, or trailers require an explicit end_data before being sent.
      state.data_deferred = true;
      break;
    } else if (length == DataFrameSource::kError) {
      // TODO(birenroy): Consider queuing a RST_STREAM INTERNAL_ERROR instead.
      CloseStream(stream_id, Http2ErrorCode::INTERNAL_ERROR);
      // No more work on the stream; it has been closed.
      break;
    }
    const bool fin = end_data ? state.outbound_body->send_fin() : false;
    if (length > 0 || fin) {
      spdy::SpdyDataIR data(stream_id);
      data.set_fin(fin);
      data.SetDataShallow(length);
      spdy::SpdySerializedFrame header =
          spdy::SpdyFramer::SerializeDataFrameHeaderWithPaddingLengthField(
              data);
      QUICHE_DCHECK(buffered_data_.empty() && frames_.empty());
      const bool success =
          state.outbound_body->Send(absl::string_view(header), length);
      if (!success) {
        connection_can_write = SendResult::SEND_BLOCKED;
        break;
      }
      connection_send_window_ -= length;
      state.send_window -= length;
      available_window = std::min({connection_send_window_, state.send_window,
                                   static_cast<int32_t>(max_frame_payload_)});
      if (fin) {
        state.half_closed_local = true;
        MaybeFinWithRstStream(it);
      }
      AfterFrameSent(/* DATA */ 0, stream_id, length, fin ? 0x1 : 0x0, 0);
      if (!stream_map_.contains(stream_id)) {
        // Note: the stream may have been closed if `fin` is true.
        break;
      }
    }
    if (end_data || (length == 0 && state.trailers != nullptr &&
                     !options_.trailers_require_end_data)) {
      // If SelectPayloadLength() returned {0, false}, and there are trailers to
      // send, and the safety feature is disabled, it's okay to send the
      // trailers.
      if (state.trailers != nullptr) {
        auto block_ptr = std::move(state.trailers);
        if (fin) {
          QUICHE_LOG(ERROR) << "Sent fin; can't send trailers.";
        } else {
          SendTrailers(stream_id, std::move(*block_ptr));
        }
      }
      state.outbound_body = nullptr;
    }
  }
  // If the stream still exists and has data to send, it should be marked as
  // ready in the write scheduler.
  if (stream_map_.contains(stream_id) && !state.data_deferred &&
      state.send_window > 0 && state.outbound_body != nullptr) {
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  // Streams can continue writing as long as the connection is not write-blocked
  // and there is additional flow control quota available.
  if (connection_can_write != SendResult::SEND_OK) {
    return connection_can_write;
  }
  return available_window <= 0 ? SendResult::SEND_BLOCKED : SendResult::SEND_OK;
}

OgHttp2Session::SendResult OgHttp2Session::SendMetadata(
    Http2StreamId stream_id, OgHttp2Session::MetadataSequence& sequence) {
  const uint32_t max_payload_size =
      std::min(kMaxAllowedMetadataFrameSize, max_frame_payload_);
  auto payload_buffer = absl::make_unique<uint8_t[]>(max_payload_size);
  while (!sequence.empty()) {
    MetadataSource& source = *sequence.front();

    int64_t written;
    bool end_metadata;
    std::tie(written, end_metadata) =
        source.Pack(payload_buffer.get(), max_payload_size);
    if (written < 0) {
      // Did not touch the connection, so perhaps writes are still possible.
      return SendResult::SEND_OK;
    }
    QUICHE_DCHECK_LE(static_cast<size_t>(written), max_payload_size);
    auto payload = absl::string_view(
        reinterpret_cast<const char*>(payload_buffer.get()), written);
    EnqueueFrame(absl::make_unique<spdy::SpdyUnknownIR>(
        stream_id, kMetadataFrameType, end_metadata ? kMetadataEndFlag : 0u,
        std::string(payload)));
    if (end_metadata) {
      sequence.erase(sequence.begin());
    }
  }
  return SendQueuedFrames();
}

int32_t OgHttp2Session::SubmitRequest(
    absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source, void* user_data) {
  // TODO(birenroy): return an error for the incorrect perspective
  const Http2StreamId stream_id = next_stream_id_;
  next_stream_id_ += 2;
  if (CanCreateStream()) {
    StartRequest(stream_id, ToHeaderBlock(headers), std::move(data_source),
                 user_data);
  } else {
    // TODO(diannahu): There should probably be a limit to the number of allowed
    // pending streams.
    pending_streams_.push_back(
        {stream_id, ToHeaderBlock(headers), std::move(data_source), user_data});
  }
  return stream_id;
}

int OgHttp2Session::SubmitResponse(
    Http2StreamId stream_id, absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source) {
  // TODO(birenroy): return an error for the incorrect perspective
  auto iter = stream_map_.find(stream_id);
  if (iter == stream_map_.end()) {
    QUICHE_LOG(ERROR) << "Unable to find stream " << stream_id;
    return -501;  // NGHTTP2_ERR_INVALID_ARGUMENT
  }
  const bool end_stream = data_source == nullptr;
  if (!end_stream) {
    // Add data source to stream state
    iter->second.outbound_body = std::move(data_source);
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  SendHeaders(stream_id, ToHeaderBlock(headers), end_stream);
  return 0;
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
  if (state.outbound_body == nullptr) {
    // Enqueue trailers immediately.
    SendTrailers(stream_id, ToHeaderBlock(trailers));
  } else {
    QUICHE_LOG_IF(ERROR, state.outbound_body->send_fin())
        << "DataFrameSource will send fin, preventing trailers!";
    // Save trailers so they can be written once data is done.
    state.trailers =
        absl::make_unique<spdy::SpdyHeaderBlock>(ToHeaderBlock(trailers));
    if (!options_.trailers_require_end_data) {
      iter->second.data_deferred = false;
    }
    if (!iter->second.data_deferred) {
      write_scheduler_.MarkStreamReady(stream_id, false);
    }
  }
  return 0;
}

void OgHttp2Session::SubmitMetadata(Http2StreamId stream_id,
                                    std::unique_ptr<MetadataSource> source) {
  if (stream_id == 0) {
    connection_metadata_.push_back(std::move(source));
  } else {
    auto iter = CreateStream(stream_id);
    iter->second.outbound_metadata.push_back(std::move(source));
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
}

void OgHttp2Session::SubmitSettings(absl::Span<const Http2Setting> settings) {
  EnqueueFrame(PrepareSettingsFrame(settings));
}

void OgHttp2Session::OnError(SpdyFramerError error,
                             std::string detailed_error) {
  QUICHE_VLOG(1) << "Error: "
                 << http2::Http2DecoderAdapter::SpdyFramerErrorToString(error)
                 << " details: " << detailed_error;
  // TODO(diannahu): Consider propagating `detailed_error`.
  LatchErrorAndNotify(GetHttp2ErrorCode(error), ConnectionError::kParseError);
}

void OgHttp2Session::OnCommonHeader(spdy::SpdyStreamId stream_id,
                                    size_t length,
                                    uint8_t type,
                                    uint8_t flags) {
  highest_received_stream_id_ = std::max(static_cast<Http2StreamId>(stream_id),
                                         highest_received_stream_id_);
  const bool result = visitor_.OnFrameHeader(stream_id, length, type, flags);
  if (!result) {
    decoder_.StopProcessing();
  }
}

void OgHttp2Session::OnDataFrameHeader(spdy::SpdyStreamId stream_id,
                                       size_t length, bool /*fin*/) {
  if (!stream_map_.contains(stream_id)) {
    // The stream does not exist; it could be an error or a benign close, e.g.,
    // getting data for a stream this connection recently closed.
    if (static_cast<Http2StreamId>(stream_id) > highest_processed_stream_id_) {
      // Receiving DATA before HEADERS is a connection error.
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kWrongFrameSequence);
    }
    return;
  }

  const bool result = visitor_.OnBeginDataForStream(stream_id, length);
  if (!result) {
    decoder_.StopProcessing();
  }
}

void OgHttp2Session::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                       const char* data,
                                       size_t len) {
  // Count the data against flow control, even if the stream is unknown.
  MarkDataBuffered(stream_id, len);

  if (!stream_map_.contains(stream_id)) {
    // If the stream was unknown due to a protocol error, the visitor was
    // informed in OnDataFrameHeader().
    return;
  }

  const bool result =
      visitor_.OnDataForStream(stream_id, absl::string_view(data, len));
  if (!result) {
    decoder_.StopProcessing();
  }
}

void OgHttp2Session::OnStreamEnd(spdy::SpdyStreamId stream_id) {
  auto iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second.half_closed_remote = true;
    visitor_.OnEndStream(stream_id);
  }
  auto queued_frames_iter = queued_frames_.find(stream_id);
  const bool no_queued_frames = queued_frames_iter == queued_frames_.end() ||
                                queued_frames_iter->second == 0;
  if (iter != stream_map_.end() && iter->second.half_closed_local &&
      options_.perspective == Perspective::kClient && no_queued_frames) {
    // From the client's perspective, the stream can be closed if it's already
    // half_closed_local.
    CloseStream(stream_id, Http2ErrorCode::HTTP2_NO_ERROR);
  }
}

void OgHttp2Session::OnStreamPadLength(spdy::SpdyStreamId stream_id,
                                       size_t value) {
  MarkDataBuffered(stream_id, 1 + value);
  // TODO(181586191): Pass padding to the visitor?
}

void OgHttp2Session::OnStreamPadding(spdy::SpdyStreamId /*stream_id*/, size_t
                                     /*len*/) {
  // Flow control was accounted for in OnStreamPadLength().
  // TODO(181586191): Pass padding to the visitor?
}

spdy::SpdyHeadersHandlerInterface* OgHttp2Session::OnHeaderFrameStart(
    spdy::SpdyStreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
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
      it->second.received_header_type = HeaderType::RESPONSE_100;
    } else {
      it->second.received_header_type = headers_handler_.header_type();
    }
    headers_handler_.set_stream_id(0);
  }
}

void OgHttp2Session::OnRstStream(spdy::SpdyStreamId stream_id,
                                 spdy::SpdyErrorCode error_code) {
  auto iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second.half_closed_remote = true;
    iter->second.outbound_body = nullptr;
  } else if (static_cast<Http2StreamId>(stream_id) >
             highest_processed_stream_id_) {
    // Receiving RST_STREAM before HEADERS is a connection error.
    LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                        ConnectionError::kWrongFrameSequence);
    return;
  }
  visitor_.OnRstStream(stream_id, TranslateErrorCode(error_code));
  // TODO(birenroy): Consider whether there are outbound frames queued for the
  // stream.
  CloseStream(stream_id, TranslateErrorCode(error_code));
}

void OgHttp2Session::OnSettings() {
  visitor_.OnSettingsStart();
  auto settings = absl::make_unique<SpdySettingsIR>();
  settings->set_is_ack(true);
  EnqueueFrame(std::move(settings));
}

void OgHttp2Session::OnSetting(spdy::SpdySettingsId id, uint32_t value) {
  visitor_.OnSetting({id, value});
  if (id == kMetadataExtensionId) {
    peer_supports_metadata_ = (value != 0);
  } else if (id == MAX_FRAME_SIZE) {
    max_frame_payload_ = value;
  } else if (id == MAX_CONCURRENT_STREAMS) {
    max_outbound_concurrent_streams_ = value;
  } else if (id == HEADER_TABLE_SIZE) {
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
  }
}

void OgHttp2Session::OnSettingsEnd() {
  visitor_.OnSettingsEnd();
}

void OgHttp2Session::OnSettingsAck() {
  if (!settings_ack_callbacks_.empty()) {
    SettingsAckCallback callback = std::move(settings_ack_callbacks_.front());
    settings_ack_callbacks_.pop_front();
    callback();
  }

  visitor_.OnSettingsAck();
}

void OgHttp2Session::OnPing(spdy::SpdyPingId unique_id, bool is_ack) {
  visitor_.OnPing(unique_id, is_ack);
  if (options_.auto_ping_ack && !is_ack) {
    auto ping = absl::make_unique<spdy::SpdyPingIR>(unique_id);
    ping->set_is_ack(true);
    EnqueueFrame(std::move(ping));
  }
}

void OgHttp2Session::OnGoAway(spdy::SpdyStreamId last_accepted_stream_id,
                              spdy::SpdyErrorCode error_code) {
  received_goaway_ = true;
  const bool result = visitor_.OnGoAway(last_accepted_stream_id,
                                        TranslateErrorCode(error_code), "");
  if (!result) {
    decoder_.StopProcessing();
  }
}

bool OgHttp2Session::OnGoAwayFrameData(const char* /*goaway_data*/, size_t
                                       /*len*/) {
  // Opaque data is currently ignored.
  return true;
}

void OgHttp2Session::OnHeaders(spdy::SpdyStreamId stream_id,
                               bool /*has_priority*/, int /*weight*/,
                               spdy::SpdyStreamId /*parent_stream_id*/,
                               bool /*exclusive*/, bool fin, bool /*end*/) {
  if (stream_id % 2 == 0) {
    // Server push is disabled; receiving push HEADERS is a connection error.
    LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                        ConnectionError::kInvalidNewStreamId);
    return;
  }
  if (fin) {
    headers_handler_.set_frame_contains_fin();
  }
  if (options_.perspective == Perspective::kServer) {
    const auto new_stream_id = static_cast<Http2StreamId>(stream_id);
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
      visitor_.OnInvalidFrame(
          stream_id, Http2VisitorInterface::InvalidFrameError::kProtocol);
      LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                          ConnectionError::kExceededMaxConcurrentStreams);
      return;
    }
    if (stream_map_.size() >= pending_max_inbound_concurrent_streams_) {
      // The new stream would exceed our advertised but unacked
      // MAX_CONCURRENT_STREAMS. Refuse the stream for parity with nghttp2.
      EnqueueFrame(absl::make_unique<spdy::SpdyRstStreamIR>(
          stream_id, spdy::ERROR_CODE_REFUSED_STREAM));
      const bool ok = visitor_.OnInvalidFrame(
          stream_id, Http2VisitorInterface::InvalidFrameError::kRefusedStream);
      if (!ok) {
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
  if (stream_id == 0) {
    connection_send_window_ += delta_window_size;
  } else {
    auto it = stream_map_.find(stream_id);
    if (it == stream_map_.end()) {
      QUICHE_VLOG(1) << "Stream " << stream_id << " not found!";
      if (static_cast<Http2StreamId>(stream_id) >
          highest_processed_stream_id_) {
        // Receiving WINDOW_UPDATE before HEADERS is a connection error.
        LatchErrorAndNotify(Http2ErrorCode::PROTOCOL_ERROR,
                            ConnectionError::kWrongFrameSequence);
        return;
      }
    } else {
      if (it->second.send_window == 0) {
        // The stream was blocked on flow control.
        write_scheduler_.MarkStreamReady(stream_id, false);
      }
      it->second.send_window += delta_window_size;
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

void OgHttp2Session::OnContinuation(spdy::SpdyStreamId /*stream_id*/, bool
                                    /*end*/) {}

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

void OgHttp2Session::OnHeaderStatus(
    Http2StreamId stream_id, Http2VisitorInterface::OnHeaderResult result) {
  QUICHE_DCHECK_NE(result, Http2VisitorInterface::HEADER_OK);
  const bool should_reset_stream =
      result == Http2VisitorInterface::HEADER_RST_STREAM ||
      result == Http2VisitorInterface::HEADER_HTTP_MESSAGING;
  if (should_reset_stream) {
    const Http2ErrorCode error_code =
        (result == Http2VisitorInterface::HEADER_RST_STREAM)
            ? Http2ErrorCode::INTERNAL_ERROR
            : Http2ErrorCode::PROTOCOL_ERROR;
    const spdy::SpdyErrorCode spdy_error_code = TranslateErrorCode(error_code);
    const Http2VisitorInterface::InvalidFrameError frame_error =
        (result == Http2VisitorInterface::HEADER_RST_STREAM)
            ? Http2VisitorInterface::InvalidFrameError::kHttpHeader
            : Http2VisitorInterface::InvalidFrameError::kHttpMessaging;
    auto it = streams_reset_.find(stream_id);
    if (it == streams_reset_.end()) {
      EnqueueFrame(
          absl::make_unique<spdy::SpdyRstStreamIR>(stream_id, spdy_error_code));

      const bool ok = visitor_.OnInvalidFrame(stream_id, frame_error);
      if (!ok) {
        LatchErrorAndNotify(error_code, ConnectionError::kHeaderError);
      }
    }
  } else if (result == Http2VisitorInterface::HEADER_CONNECTION_ERROR) {
    LatchErrorAndNotify(Http2ErrorCode::INTERNAL_ERROR,
                        ConnectionError::kHeaderError);
  }
}

bool OgHttp2Session::OnFrameHeader(spdy::SpdyStreamId stream_id, size_t length,
                                   uint8_t type, uint8_t flags) {
  if (type == kMetadataFrameType) {
    QUICHE_DCHECK_EQ(metadata_length_, 0u);
    visitor_.OnBeginMetadataForStream(stream_id, length);
    metadata_stream_id_ = stream_id;
    metadata_length_ = length;
    end_metadata_ = flags & kMetadataEndFlag;
    return true;
  } else {
    QUICHE_DLOG(INFO) << "Unexpected frame type " << static_cast<int>(type)
                      << " received by the extension visitor.";
    return false;
  }
}

void OgHttp2Session::OnFramePayload(const char* data, size_t len) {
  if (metadata_length_ > 0) {
    QUICHE_DCHECK_LE(len, metadata_length_);
    const bool success = visitor_.OnMetadataForStream(
        metadata_stream_id_, absl::string_view(data, len));
    if (success) {
      metadata_length_ -= len;
      if (metadata_length_ == 0 && end_metadata_) {
        visitor_.OnMetadataEndForStream(metadata_stream_id_);
        metadata_stream_id_ = 0;
        end_metadata_ = false;
      }
    } else {
      decoder_.StopProcessing();
    }
  } else {
    QUICHE_DLOG(INFO) << "Unexpected metadata payload for stream "
                      << metadata_stream_id_;
  }
}

void OgHttp2Session::MaybeSetupPreface() {
  if (!queued_preface_) {
    if (options_.perspective == Perspective::kClient) {
      buffered_data_.assign(spdy::kHttp2ConnectionHeaderPrefix,
                            spdy::kHttp2ConnectionHeaderPrefixSize);
    }
    // First frame must be a non-ack SETTINGS.
    if (frames_.empty() ||
        frames_.front()->frame_type() != spdy::SpdyFrameType::SETTINGS ||
        reinterpret_cast<SpdySettingsIR&>(*frames_.front()).is_ack()) {
      frames_.push_front(PrepareSettingsFrame(GetInitialSettings()));
    }
    queued_preface_ = true;
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
  return settings;
}

std::unique_ptr<SpdySettingsIR> OgHttp2Session::PrepareSettingsFrame(
    absl::Span<const Http2Setting> settings) {
  auto settings_ir = absl::make_unique<SpdySettingsIR>();
  for (const Http2Setting& setting : settings) {
    settings_ir->AddSetting(setting.id, setting.value);

    if (setting.id == Http2KnownSettingsId::MAX_CONCURRENT_STREAMS) {
      pending_max_inbound_concurrent_streams_ = setting.value;
    }
  }

  // Copy the (small) map of settings we are about to send so that we can set
  // values in the SETTINGS ack callback.
  settings_ack_callbacks_.push_back(
      [this, settings_map = settings_ir->values()]() {
        for (const auto id_and_value : settings_map) {
          if (id_and_value.first == spdy::SETTINGS_MAX_CONCURRENT_STREAMS) {
            max_inbound_concurrent_streams_ = id_and_value.second;
          } else if (id_and_value.first == spdy::SETTINGS_HEADER_TABLE_SIZE) {
            decoder_.GetHpackDecoder()->ApplyHeaderTableSizeSetting(
                id_and_value.second);
          }
        }
      });
  return settings_ir;
}

void OgHttp2Session::SendWindowUpdate(Http2StreamId stream_id,
                                      size_t update_delta) {
  EnqueueFrame(
      absl::make_unique<spdy::SpdyWindowUpdateIR>(stream_id, update_delta));
}

void OgHttp2Session::SendHeaders(Http2StreamId stream_id,
                                 spdy::SpdyHeaderBlock headers,
                                 bool end_stream) {
  auto frame =
      absl::make_unique<spdy::SpdyHeadersIR>(stream_id, std::move(headers));
  frame->set_fin(end_stream);
  EnqueueFrame(std::move(frame));
}

void OgHttp2Session::SendTrailers(Http2StreamId stream_id,
                                  spdy::SpdyHeaderBlock trailers) {
  auto frame =
      absl::make_unique<spdy::SpdyHeadersIR>(stream_id, std::move(trailers));
  frame->set_fin(true);
  EnqueueFrame(std::move(frame));
}

void OgHttp2Session::MaybeFinWithRstStream(StreamStateMap::iterator iter) {
  QUICHE_DCHECK(iter != stream_map_.end() && iter->second.half_closed_local);

  if (options_.rst_stream_no_error_when_incomplete &&
      options_.perspective == Perspective::kServer &&
      !iter->second.half_closed_remote) {
    // Since the peer has not yet ended the stream, this endpoint should
    // send a RST_STREAM NO_ERROR. See RFC 7540 Section 8.1.
    EnqueueFrame(absl::make_unique<spdy::SpdyRstStreamIR>(
        iter->first, spdy::SpdyErrorCode::ERROR_CODE_NO_ERROR));
    iter->second.half_closed_remote = true;
  }
}

void OgHttp2Session::MarkDataBuffered(Http2StreamId stream_id, size_t bytes) {
  connection_window_manager_.MarkDataBuffered(bytes);
  auto it = stream_map_.find(stream_id);
  if (it != stream_map_.end()) {
    it->second.window_manager.MarkDataBuffered(bytes);
  }
}

OgHttp2Session::StreamStateMap::iterator OgHttp2Session::CreateStream(
    Http2StreamId stream_id) {
  WindowManager::WindowUpdateListener listener =
      [this, stream_id](size_t window_update_delta) {
        SendWindowUpdate(stream_id, window_update_delta);
      };
  absl::flat_hash_map<Http2StreamId, StreamState>::iterator iter;
  bool inserted;
  std::tie(iter, inserted) = stream_map_.try_emplace(
      stream_id,
      StreamState(stream_receive_window_limit_, std::move(listener)));
  if (inserted) {
    // Add the stream to the write scheduler.
    const WriteScheduler::StreamPrecedenceType precedence(3);
    write_scheduler_.RegisterStream(stream_id, precedence);

    highest_processed_stream_id_ =
        std::max(highest_processed_stream_id_, stream_id);
  }
  return iter;
}

void OgHttp2Session::StartRequest(Http2StreamId stream_id,
                                  spdy::SpdyHeaderBlock headers,
                                  std::unique_ptr<DataFrameSource> data_source,
                                  void* user_data) {
  auto iter = CreateStream(stream_id);
  const bool end_stream = data_source == nullptr;
  if (!end_stream) {
    iter->second.outbound_body = std::move(data_source);
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  iter->second.user_data = user_data;
  SendHeaders(stream_id, std::move(headers), end_stream);
}

void OgHttp2Session::CloseStream(Http2StreamId stream_id,
                                 Http2ErrorCode error_code) {
  visitor_.OnCloseStream(stream_id, error_code);
  stream_map_.erase(stream_id);
  if (write_scheduler_.StreamRegistered(stream_id)) {
    write_scheduler_.UnregisterStream(stream_id);
  }

  if (!pending_streams_.empty() && CanCreateStream()) {
    PendingStreamState& pending_stream = pending_streams_.front();
    StartRequest(pending_stream.stream_id, std::move(pending_stream.headers),
                 std::move(pending_stream.data_source),
                 pending_stream.user_data);
    pending_streams_.pop_front();
  }
}

bool OgHttp2Session::CanCreateStream() const {
  return stream_map_.size() < max_outbound_concurrent_streams_;
}

HeaderType OgHttp2Session::NextHeaderType(
    absl::optional<HeaderType> current_type) {
  if (IsServerSession()) {
    return HeaderType::REQUEST;
  } else if (!current_type ||
             current_type.value() == HeaderType::RESPONSE_100) {
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
  if (IsServerSession()) {
    EnqueueFrame(absl::make_unique<spdy::SpdyGoAwayIR>(
        highest_processed_stream_id_, TranslateErrorCode(error_code),
        ConnectionErrorToString(error)));
  }
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

}  // namespace adapter
}  // namespace http2
