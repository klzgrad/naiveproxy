#include "http2/adapter/oghttp2_session.h"

#include "absl/strings/escaping.h"

namespace http2 {
namespace adapter {

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockStart() {
  visitor_.OnBeginHeadersForStream(stream_id_);
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeader(
    absl::string_view key,
    absl::string_view value) {
  visitor_.OnHeaderForStream(stream_id_, key, value);
}

void OgHttp2Session::PassthroughHeadersHandler::OnHeaderBlockEnd(
    size_t /* uncompressed_header_bytes */,
    size_t /* compressed_header_bytes */) {
  visitor_.OnEndHeadersForStream(stream_id_);
}

OgHttp2Session::OgHttp2Session(Http2VisitorInterface& visitor, Options options)
    : visitor_(visitor), headers_handler_(visitor), options_(options) {
  decoder_.set_visitor(this);
  if (options_.perspective == Perspective::kServer) {
    remaining_preface_ = {spdy::kHttp2ConnectionHeaderPrefix,
                          spdy::kHttp2ConnectionHeaderPrefixSize};
  }
}

OgHttp2Session::~OgHttp2Session() {}

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
  return 0;  // Remove?
}

void OgHttp2Session::EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame) {
  frames_.push_back(std::move(frame));
}

std::string OgHttp2Session::GetBytesToWrite(absl::optional<size_t> max_bytes) {
  const size_t serialized_max =
      max_bytes ? max_bytes.value() : std::numeric_limits<size_t>::max();
  std::string serialized = std::move(serialized_prefix_);
  while (serialized.size() < serialized_max && !frames_.empty()) {
    spdy::SpdySerializedFrame frame = framer_.SerializeFrame(*frames_.front());
    absl::StrAppend(&serialized, absl::string_view(frame));
    frames_.pop_front();
  }
  if (serialized.size() > serialized_max) {
    serialized_prefix_ = serialized.substr(serialized_max);
    serialized.resize(serialized_max);
  }
  return serialized;
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
  visitor_.OnFrameHeader(stream_id, length, type, flags);
}

void OgHttp2Session::OnDataFrameHeader(spdy::SpdyStreamId stream_id,
                                       size_t length,
                                       bool fin) {
  visitor_.OnBeginDataForStream(stream_id, length);
}

void OgHttp2Session::OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                       const char* data,
                                       size_t len) {
  visitor_.OnDataForStream(stream_id, absl::string_view(data, len));
}

void OgHttp2Session::OnStreamEnd(spdy::SpdyStreamId stream_id) {
  visitor_.OnEndStream(stream_id);
}

void OgHttp2Session::OnStreamPadLength(spdy::SpdyStreamId /*stream_id*/,
                                       size_t /*value*/) {
  // TODO(181586191): handle padding
}

void OgHttp2Session::OnStreamPadding(spdy::SpdyStreamId stream_id, size_t len) {
  // TODO(181586191): handle padding
}

spdy::SpdyHeadersHandlerInterface* OgHttp2Session::OnHeaderFrameStart(
    spdy::SpdyStreamId stream_id) {
  headers_handler_.set_stream_id(stream_id);
  return &headers_handler_;
}

void OgHttp2Session::OnHeaderFrameEnd(spdy::SpdyStreamId stream_id) {
  headers_handler_.set_stream_id(0);
}

void OgHttp2Session::OnRstStream(spdy::SpdyStreamId stream_id,
                                 spdy::SpdyErrorCode error_code) {
  visitor_.OnRstStream(stream_id, TranslateErrorCode(error_code));
  visitor_.OnCloseStream(stream_id, TranslateErrorCode(error_code));
}

void OgHttp2Session::OnSettings() {
  visitor_.OnSettingsStart();
}

void OgHttp2Session::OnSetting(spdy::SpdySettingsId id, uint32_t value) {
  visitor_.OnSetting({id, value});
}

void OgHttp2Session::OnSettingsEnd() {
  visitor_.OnSettingsEnd();
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

bool OgHttp2Session::OnGoAwayFrameData(const char* goaway_data, size_t len) {
  // Opaque data is currently ignored.
  return true;
}

void OgHttp2Session::OnHeaders(spdy::SpdyStreamId stream_id,
                               bool has_priority,
                               int weight,
                               spdy::SpdyStreamId parent_stream_id,
                               bool exclusive,
                               bool fin,
                               bool end) {}

void OgHttp2Session::OnWindowUpdate(spdy::SpdyStreamId stream_id,
                                    int delta_window_size) {
  if (stream_id == 0) {
    peer_window_ += delta_window_size;
  } else {
    auto it = stream_map_.find(stream_id);
    if (it == stream_map_.end()) {
      QUICHE_VLOG(1) << "Stream " << stream_id << " not found!";
    } else {
      it->second.send_window += delta_window_size;
    }
  }
  visitor_.OnWindowUpdate(stream_id, delta_window_size);
}

void OgHttp2Session::OnPushPromise(spdy::SpdyStreamId stream_id,
                                   spdy::SpdyStreamId promised_stream_id,
                                   bool end) {}

void OgHttp2Session::OnContinuation(spdy::SpdyStreamId stream_id, bool end) {}

void OgHttp2Session::OnAltSvc(spdy::SpdyStreamId /*stream_id*/,
                              absl::string_view /*origin*/,
                              const spdy::SpdyAltSvcWireFormat::
                                  AlternativeServiceVector& /*altsvc_vector*/) {
}

void OgHttp2Session::OnPriority(spdy::SpdyStreamId stream_id,
                                spdy::SpdyStreamId parent_stream_id,
                                int weight,
                                bool exclusive) {}

void OgHttp2Session::OnPriorityUpdate(spdy::SpdyStreamId prioritized_stream_id,
                                      absl::string_view priority_field_value) {}

bool OgHttp2Session::OnUnknownFrame(spdy::SpdyStreamId stream_id,
                                    uint8_t frame_type) {
  return true;
}

}  // namespace adapter
}  // namespace http2
