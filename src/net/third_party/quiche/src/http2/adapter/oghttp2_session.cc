#include "http2/adapter/oghttp2_session.h"

#include <tuple>

#include "absl/strings/escaping.h"
#include "http2/adapter/oghttp2_util.h"

namespace http2 {
namespace adapter {

namespace {

const size_t kMaxMetadataFrameSize = 16384;

// TODO(birenroy): Consider incorporating spdy::FlagsSerializionVisitor here.
class FrameAttributeCollector : public spdy::SpdyFrameVisitor {
 public:
  FrameAttributeCollector() = default;
  void VisitData(const spdy::SpdyDataIR& data) override {
    frame_type_ = static_cast<uint8_t>(data.frame_type());
    stream_id_ = data.stream_id();
    length_ =
        data.data_len() + (data.padded() ? 1 : 0) + data.padding_payload_len();
    flags_ = (data.fin() ? 0x1 : 0) | (data.padded() ? 0x8 : 0);
  }
  void VisitHeaders(const spdy::SpdyHeadersIR& headers) override {
    frame_type_ = static_cast<uint8_t>(headers.frame_type());
    stream_id_ = headers.stream_id();
    length_ = headers.size() - spdy::kFrameHeaderSize;
    flags_ = 0x4 | (headers.fin() ? 0x1 : 0) | (headers.padded() ? 0x8 : 0) |
             (headers.has_priority() ? 0x20 : 0);
  }
  void VisitPriority(const spdy::SpdyPriorityIR& priority) override {
    frame_type_ = static_cast<uint8_t>(priority.frame_type());
    frame_type_ = 2;
    length_ = 5;
    stream_id_ = priority.stream_id();
  }
  void VisitRstStream(const spdy::SpdyRstStreamIR& rst_stream) override {
    frame_type_ = static_cast<uint8_t>(rst_stream.frame_type());
    frame_type_ = 3;
    length_ = 4;
    stream_id_ = rst_stream.stream_id();
    error_code_ = rst_stream.error_code();
  }
  void VisitSettings(const spdy::SpdySettingsIR& settings) override {
    frame_type_ = static_cast<uint8_t>(settings.frame_type());
    frame_type_ = 4;
    length_ = 6 * settings.values().size();
    flags_ = (settings.is_ack() ? 0x1 : 0);
  }
  void VisitPushPromise(const spdy::SpdyPushPromiseIR& push_promise) override {
    frame_type_ = static_cast<uint8_t>(push_promise.frame_type());
    frame_type_ = 5;
    length_ = push_promise.size() - spdy::kFrameHeaderSize;
    stream_id_ = push_promise.stream_id();
    flags_ = (push_promise.padded() ? 0x8 : 0);
  }
  void VisitPing(const spdy::SpdyPingIR& ping) override {
    frame_type_ = static_cast<uint8_t>(ping.frame_type());
    frame_type_ = 6;
    length_ = 8;
    flags_ = (ping.is_ack() ? 0x1 : 0);
  }
  void VisitGoAway(const spdy::SpdyGoAwayIR& goaway) override {
    frame_type_ = static_cast<uint8_t>(goaway.frame_type());
    frame_type_ = 7;
    length_ = goaway.size() - spdy::kFrameHeaderSize;
    error_code_ = goaway.error_code();
  }
  void VisitWindowUpdate(
      const spdy::SpdyWindowUpdateIR& window_update) override {
    frame_type_ = static_cast<uint8_t>(window_update.frame_type());
    frame_type_ = 8;
    length_ = 4;
    stream_id_ = window_update.stream_id();
  }
  void VisitContinuation(
      const spdy::SpdyContinuationIR& continuation) override {
    frame_type_ = static_cast<uint8_t>(continuation.frame_type());
    stream_id_ = continuation.stream_id();
    flags_ = continuation.end_headers() ? 0x4 : 0;
    length_ = continuation.size() - spdy::kFrameHeaderSize;
  }
  void VisitUnknown(const spdy::SpdyUnknownIR& unknown) override {
    frame_type_ = static_cast<uint8_t>(unknown.frame_type());
    stream_id_ = unknown.stream_id();
    flags_ = unknown.flags();
    length_ = unknown.size() - spdy::kFrameHeaderSize;
  }
  void VisitAltSvc(const spdy::SpdyAltSvcIR& /*altsvc*/) override {}
  void VisitPriorityUpdate(
      const spdy::SpdyPriorityUpdateIR& /*priority_update*/) override {}
  void VisitAcceptCh(const spdy::SpdyAcceptChIR& /*accept_ch*/) override {}

  uint32_t stream_id() { return stream_id_; }
  uint32_t length() { return length_; }
  uint32_t error_code() { return error_code_; }
  uint8_t frame_type() { return frame_type_; }
  uint8_t flags() { return flags_; }

 private:
  uint32_t stream_id_ = 0;
  uint32_t length_ = 0;
  uint32_t error_code_ = 0;
  uint8_t frame_type_ = 0;
  uint8_t flags_ = 0;
};

}  // namespace

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockStart() {
  const bool status = visitor_.OnBeginHeadersForStream(stream_id_);
  if (!status) {
    result_ = Http2VisitorInterface::HEADER_CONNECTION_ERROR;
  }
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeader(
    absl::string_view key,
    absl::string_view value) {
  if (result_ == Http2VisitorInterface::HEADER_OK) {
    result_ = visitor_.OnHeaderForStream(stream_id_, key, value);
  }
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockEnd(
    size_t /* uncompressed_header_bytes */,
    size_t /* compressed_header_bytes */) {
  if (result_ == Http2VisitorInterface::HEADER_OK) {
    visitor_.OnEndHeadersForStream(stream_id_);
  } else {
    session_.OnHeaderStatus(stream_id_, result_);
  }
}

OgHttp2Session::OgHttp2Session(Http2VisitorInterface& visitor, Options options)
    : visitor_(visitor),
      headers_handler_(*this, visitor),
      connection_window_manager_(kInitialFlowControlWindowSize,
                                 [this](size_t window_update_delta) {
                                   SendWindowUpdate(kConnectionStreamId,
                                                    window_update_delta);
                                 }),
      options_(options) {
  decoder_.set_visitor(this);
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
  if (it->second.outbound_body == nullptr ||
      !write_scheduler_.StreamRegistered(stream_id)) {
    return false;
  }
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

int OgHttp2Session::GetHpackDecoderDynamicTableSize() const {
  const spdy::HpackDecoderAdapter* decoder = decoder_.GetHpackDecoder();
  return decoder == nullptr ? 0 : decoder->GetDynamicTableSize();
}

ssize_t OgHttp2Session::ProcessBytes(absl::string_view bytes) {
  ssize_t preface_consumed = 0;
  if (!remaining_preface_.empty()) {
    QUICHE_VLOG(2) << "Preface bytes remaining: " << remaining_preface_.size();
    // decoder_ does not understand the client connection preface.
    size_t min_size = std::min(remaining_preface_.size(), bytes.size());
    if (!absl::StartsWith(remaining_preface_, bytes.substr(0, min_size))) {
      // Preface doesn't match!
      QUICHE_DLOG(INFO) << "Preface doesn't match! Expected: ["
                        << absl::CEscape(remaining_preface_) << "], actual: ["
                        << absl::CEscape(bytes) << "]";
      visitor_.OnConnectionError();
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
  ssize_t result = decoder_.ProcessInput(bytes.data(), bytes.size());
  return result < 0 ? result : result + preface_consumed;
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
  if (frame->frame_type() == spdy::SpdyFrameType::GOAWAY) {
    queued_goaway_ = true;
  } else if (frame->frame_type() == spdy::SpdyFrameType::RST_STREAM) {
    streams_reset_.insert(frame->stream_id());
    auto iter = stream_map_.find(frame->stream_id());
    if (iter != stream_map_.end()) {
      iter->second.half_closed_local = true;
    }
  }
  frames_.push_back(std::move(frame));
}

int OgHttp2Session::Send() {
  MaybeSetupPreface();
  ssize_t result = std::numeric_limits<ssize_t>::max();
  // Flush any serialized prefix.
  while (result > 0 && !serialized_prefix_.empty()) {
    result = visitor_.OnReadyToSend(serialized_prefix_);
    if (result > 0) {
      serialized_prefix_.erase(0, result);
    }
  }
  if (!serialized_prefix_.empty()) {
    return result < 0 ? result : 0;
  }
  bool continue_writing = SendQueuedFrames();
  while (continue_writing && !connection_metadata_.empty()) {
    continue_writing = SendMetadata(0, connection_metadata_);
  }
  // Wake streams for writes.
  while (continue_writing && write_scheduler_.HasReadyStreams() &&
         connection_send_window_ > 0) {
    const Http2StreamId stream_id = write_scheduler_.PopNextReadyStream();
    // TODO(birenroy): Add a return value to indicate write blockage, so streams
    // aren't woken unnecessarily.
    continue_writing = WriteForStream(stream_id);
  }
  if (continue_writing) {
    SendQueuedFrames();
  }
  return 0;
}

bool OgHttp2Session::SendQueuedFrames() {
  // Serialize and send frames in the queue.
  while (!frames_.empty()) {
    const auto& frame_ptr = frames_.front();
    FrameAttributeCollector c;
    frame_ptr->Visit(&c);
    visitor_.OnBeforeFrameSent(c.frame_type(), c.stream_id(), c.length(),
                               c.flags());
    spdy::SpdySerializedFrame frame = framer_.SerializeFrame(*frame_ptr);
    const ssize_t result = visitor_.OnReadyToSend(absl::string_view(frame));
    if (result < 0) {
      visitor_.OnConnectionError();
      return false;
    } else if (result == 0) {
      // Write blocked.
      return false;
    } else {
      visitor_.OnFrameSent(c.frame_type(), c.stream_id(), c.length(), c.flags(),
                           c.error_code());
      if (static_cast<FrameType>(c.frame_type()) == FrameType::RST_STREAM) {
        // If this endpoint is resetting the stream, the stream should be
        // closed. This endpoint is already aware of the outbound RST_STREAM and
        // its error code, so close with NO_ERROR.
        visitor_.OnCloseStream(c.stream_id(), Http2ErrorCode::NO_ERROR);
      }

      frames_.pop_front();
      if (static_cast<size_t>(result) < frame.size()) {
        // The frame was partially written, so the rest must be buffered.
        serialized_prefix_.assign(frame.data() + result, frame.size() - result);
        return false;
      }
    }
  }
  return true;
}

bool OgHttp2Session::WriteForStream(Http2StreamId stream_id) {
  auto it = stream_map_.find(stream_id);
  if (it == stream_map_.end()) {
    QUICHE_LOG(ERROR) << "Can't find stream " << stream_id
                      << " which is ready to write!";
    return true;
  }
  StreamState& state = it->second;
  bool connection_can_write = true;
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
        MaybeCloseWithRstStream(stream_id, state);
      }
    }
    return true;
  }
  bool source_can_produce = true;
  int32_t available_window = std::min(
      std::min(connection_send_window_, state.send_window), max_frame_payload_);
  while (connection_can_write && available_window > 0 &&
         state.outbound_body != nullptr) {
    ssize_t length;
    bool end_data;
    std::tie(length, end_data) =
        state.outbound_body->SelectPayloadLength(available_window);
    if (length == 0 && !end_data) {
      source_can_produce = false;
      break;
    } else if (length == DataFrameSource::kError) {
      source_can_produce = false;
      visitor_.OnCloseStream(stream_id, Http2ErrorCode::INTERNAL_ERROR);
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
      QUICHE_DCHECK(serialized_prefix_.empty() && frames_.empty());
      const bool success =
          state.outbound_body->Send(absl::string_view(header), length);
      if (!success) {
        connection_can_write = false;
        break;
      }
      visitor_.OnFrameSent(/* DATA */ 0, stream_id, length, fin ? 0x1 : 0x0, 0);
      connection_send_window_ -= length;
      state.send_window -= length;
      available_window =
          std::min(std::min(connection_send_window_, state.send_window),
                   max_frame_payload_);
    }
    if (end_data) {
      bool sent_trailers = false;
      if (state.trailers != nullptr) {
        auto block_ptr = std::move(state.trailers);
        if (fin) {
          QUICHE_LOG(ERROR) << "Sent fin; can't send trailers.";
        } else {
          SendTrailers(stream_id, std::move(*block_ptr));
          sent_trailers = true;
        }
      }
      state.outbound_body = nullptr;
      if (fin || sent_trailers) {
        MaybeCloseWithRstStream(stream_id, state);
      }
    }
  }
  // If the stream still has data to send, it should be marked as ready in the
  // write scheduler.
  if (source_can_produce && state.send_window > 0 &&
      state.outbound_body != nullptr) {
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  // Streams can continue writing as long as the connection is not write-blocked
  // and there is additional flow control quota available.
  return connection_can_write && available_window > 0;
}

bool OgHttp2Session::SendMetadata(Http2StreamId stream_id,
                                  OgHttp2Session::MetadataSequence& sequence) {
  auto payload_buffer = absl::make_unique<uint8_t[]>(kMaxMetadataFrameSize);
  while (!sequence.empty()) {
    MetadataSource& source = *sequence.front();

    ssize_t written;
    bool end_metadata;
    std::tie(written, end_metadata) =
        source.Pack(payload_buffer.get(), kMaxMetadataFrameSize);
    if (written < 0) {
      // Did not touch the connection, so perhaps writes are still possible.
      return true;
    }
    QUICHE_DCHECK_LE(static_cast<size_t>(written), kMaxMetadataFrameSize);
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
  // Convert headers to header block, create headers frame.
  auto frame =
      absl::make_unique<spdy::SpdyHeadersIR>(stream_id, ToHeaderBlock(headers));
  // Add data source and user data to stream state
  auto iter = CreateStream(stream_id);
  write_scheduler_.MarkStreamReady(stream_id, false);
  if (data_source == nullptr) {
    frame->set_fin(true);
    iter->second.half_closed_local = true;
  } else {
    iter->second.outbound_body = std::move(data_source);
  }
  iter->second.user_data = user_data;
  // Enqueue headers frame
  EnqueueFrame(std::move(frame));
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
  // Convert headers to header block, create headers frame
  auto frame =
      absl::make_unique<spdy::SpdyHeadersIR>(stream_id, ToHeaderBlock(headers));
  if (data_source == nullptr) {
    frame->set_fin(true);
    if (iter->second.half_closed_remote) {
      visitor_.OnCloseStream(stream_id, Http2ErrorCode::NO_ERROR);
    }
    // TODO(birenroy): the server adapter should probably delete stream state
    // when calling visitor_.OnCloseStream.
  } else {
    // Add data source to stream state
    iter->second.outbound_body = std::move(data_source);
    write_scheduler_.MarkStreamReady(stream_id, false);
  }
  EnqueueFrame(std::move(frame));
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
    MaybeCloseWithRstStream(stream_id, state);
  } else {
    QUICHE_LOG_IF(ERROR, state.outbound_body->send_fin())
        << "DataFrameSource will send fin, preventing trailers!";
    // Save trailers so they can be written once data is done.
    state.trailers =
        absl::make_unique<spdy::SpdyHeaderBlock>(ToHeaderBlock(trailers));
    write_scheduler_.MarkStreamReady(stream_id, false);
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

void OgHttp2Session::OnError(http2::Http2DecoderAdapter::SpdyFramerError error,
                             std::string detailed_error) {
  QUICHE_VLOG(1) << "Error: "
                 << http2::Http2DecoderAdapter::SpdyFramerErrorToString(error)
                 << " details: " << detailed_error;
  visitor_.OnConnectionError();
}

void OgHttp2Session::OnCommonHeader(spdy::SpdyStreamId stream_id,
                                    size_t length,
                                    uint8_t type,
                                    uint8_t flags) {
  highest_received_stream_id_ = std::max(static_cast<Http2StreamId>(stream_id),
                                         highest_received_stream_id_);
  visitor_.OnFrameHeader(stream_id, length, type, flags);
}

void OgHttp2Session::OnDataFrameHeader(spdy::SpdyStreamId stream_id,
                                       size_t length, bool /*fin*/) {
  visitor_.OnBeginDataForStream(stream_id, length);
}

void OgHttp2Session::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                       const char* data,
                                       size_t len) {
  MarkDataBuffered(stream_id, len);
  visitor_.OnDataForStream(stream_id, absl::string_view(data, len));
}

void OgHttp2Session::OnStreamEnd(spdy::SpdyStreamId stream_id) {
  auto iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second.half_closed_remote = true;
  }
  visitor_.OnEndStream(stream_id);
  if (iter != stream_map_.end() && iter->second.half_closed_local &&
      options_.perspective == Perspective::kClient) {
    // From the client's perspective, the stream can be closed if it's already
    // half_closed_local.
    visitor_.OnCloseStream(stream_id, Http2ErrorCode::NO_ERROR);
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
  headers_handler_.set_stream_id(stream_id);
  return &headers_handler_;
}

void OgHttp2Session::OnHeaderFrameEnd(spdy::SpdyStreamId /*stream_id*/) {
  headers_handler_.set_stream_id(0);
}

void OgHttp2Session::OnRstStream(spdy::SpdyStreamId stream_id,
                                 spdy::SpdyErrorCode error_code) {
  auto iter = stream_map_.find(stream_id);
  if (iter != stream_map_.end()) {
    iter->second.half_closed_remote = true;
    iter->second.outbound_body = nullptr;
    write_scheduler_.UnregisterStream(stream_id);
  }
  visitor_.OnRstStream(stream_id, TranslateErrorCode(error_code));
  // TODO(birenroy): Consider bundling "close stream" behavior into a dedicated
  // method that also cleans up the stream map.
  visitor_.OnCloseStream(stream_id, TranslateErrorCode(error_code));
}

void OgHttp2Session::OnSettings() {
  visitor_.OnSettingsStart();
}

void OgHttp2Session::OnSetting(spdy::SpdySettingsId id, uint32_t value) {
  visitor_.OnSetting({id, value});
  if (id == kMetadataExtensionId) {
    peer_supports_metadata_ = (value != 0);
  }
}

void OgHttp2Session::OnSettingsEnd() {
  visitor_.OnSettingsEnd();
  auto settings = absl::make_unique<spdy::SpdySettingsIR>();
  settings->set_is_ack(true);
  EnqueueFrame(std::move(settings));
}

void OgHttp2Session::OnSettingsAck() {
  visitor_.OnSettingsAck();
}

void OgHttp2Session::OnPing(spdy::SpdyPingId unique_id, bool is_ack) {
  visitor_.OnPing(unique_id, is_ack);
}

void OgHttp2Session::OnGoAway(spdy::SpdyStreamId last_accepted_stream_id,
                              spdy::SpdyErrorCode error_code) {
  received_goaway_ = true;
  visitor_.OnGoAway(last_accepted_stream_id, TranslateErrorCode(error_code),
                    "");
}

bool OgHttp2Session::OnGoAwayFrameData(const char* /*goaway_data*/, size_t
                                       /*len*/) {
  // Opaque data is currently ignored.
  return true;
}

void OgHttp2Session::OnHeaders(spdy::SpdyStreamId stream_id,
                               bool /*has_priority*/, int /*weight*/,
                               spdy::SpdyStreamId /*parent_stream_id*/,
                               bool /*exclusive*/, bool /*fin*/, bool /*end*/) {
  if (options_.perspective == Perspective::kServer) {
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
                                   bool /*end*/) {}

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
  if (result == Http2VisitorInterface::HEADER_RST_STREAM) {
    auto it = streams_reset_.find(stream_id);
    if (it == streams_reset_.end()) {
      EnqueueFrame(absl::make_unique<spdy::SpdyRstStreamIR>(
          stream_id, spdy::ERROR_CODE_INTERNAL_ERROR));
    }
  } else if (result == Http2VisitorInterface::HEADER_CONNECTION_ERROR) {
    visitor_.OnConnectionError();
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
    visitor_.OnMetadataForStream(metadata_stream_id_,
                                 absl::string_view(data, len));
    metadata_length_ -= len;
    if (metadata_length_ == 0 && end_metadata_) {
      visitor_.OnMetadataEndForStream(metadata_stream_id_);
      metadata_stream_id_ = 0;
      end_metadata_ = false;
    }
  } else {
    QUICHE_DLOG(INFO) << "Unexpected metadata payload for stream "
                      << metadata_stream_id_;
  }
}

void OgHttp2Session::MaybeSetupPreface() {
  if (!queued_preface_) {
    if (options_.perspective == Perspective::kClient) {
      serialized_prefix_.assign(spdy::kHttp2ConnectionHeaderPrefix,
                                spdy::kHttp2ConnectionHeaderPrefixSize);
    }
    // First frame must be a non-ack SETTINGS.
    if (frames_.empty() ||
        frames_.front()->frame_type() != spdy::SpdyFrameType::SETTINGS ||
        reinterpret_cast<spdy::SpdySettingsIR*>(frames_.front().get())
            ->is_ack()) {
      frames_.push_front(absl::make_unique<spdy::SpdySettingsIR>());
    }
    queued_preface_ = true;
  }
}

void OgHttp2Session::SendWindowUpdate(Http2StreamId stream_id,
                                      size_t update_delta) {
  EnqueueFrame(
      absl::make_unique<spdy::SpdyWindowUpdateIR>(stream_id, update_delta));
}

void OgHttp2Session::SendTrailers(Http2StreamId stream_id,
                                  spdy::SpdyHeaderBlock trailers) {
  auto frame =
      absl::make_unique<spdy::SpdyHeadersIR>(stream_id, std::move(trailers));
  frame->set_fin(true);
  EnqueueFrame(std::move(frame));
}

void OgHttp2Session::MaybeCloseWithRstStream(Http2StreamId stream_id,
                                             StreamState& state) {
  state.half_closed_local = true;
  if (options_.perspective == Perspective::kServer) {
    if (state.half_closed_remote) {
      visitor_.OnCloseStream(stream_id, Http2ErrorCode::NO_ERROR);
    } else {
      // Since the peer has not yet ended the stream, this endpoint should
      // send a RST_STREAM NO_ERROR. See RFC 7540 Section 8.1.
      EnqueueFrame(absl::make_unique<spdy::SpdyRstStreamIR>(
          stream_id, spdy::SpdyErrorCode::ERROR_CODE_NO_ERROR));
      // Enqueuing the RST_STREAM also invokes OnCloseStream.
    }
    // TODO(birenroy): the server adapter should probably delete stream state
    // when calling visitor_.OnCloseStream.
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
  }
  return iter;
}

}  // namespace adapter
}  // namespace http2
