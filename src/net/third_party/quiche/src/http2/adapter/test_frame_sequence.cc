#include "http2/adapter/test_frame_sequence.h"

#include "http2/adapter/http2_util.h"
#include "spdy/core/spdy_framer.h"

namespace http2 {
namespace adapter {
namespace test {

TestFrameSequence& TestFrameSequence::ClientPreface() {
  preface_ = spdy::kHttp2ConnectionHeaderPrefix;
  frames_.push_back(absl::make_unique<spdy::SpdySettingsIR>());
  return *this;
}

TestFrameSequence& TestFrameSequence::ServerPreface() {
  frames_.push_back(absl::make_unique<spdy::SpdySettingsIR>());
  return *this;
}

TestFrameSequence& TestFrameSequence::Data(Http2StreamId stream_id,
                                           absl::string_view payload,
                                           bool fin,
                                           absl::optional<int> padding_length) {
  auto data = absl::make_unique<spdy::SpdyDataIR>(stream_id, payload);
  data->set_fin(fin);
  if (padding_length) {
    data->set_padding_len(padding_length.value());
  }
  frames_.push_back(std::move(data));
  return *this;
}

TestFrameSequence& TestFrameSequence::RstStream(Http2StreamId stream_id,
                                                Http2ErrorCode error) {
  frames_.push_back(absl::make_unique<spdy::SpdyRstStreamIR>(
      stream_id, TranslateErrorCode(error)));
  return *this;
}

TestFrameSequence& TestFrameSequence::Settings(
    absl::Span<Http2Setting> values) {
  auto settings = absl::make_unique<spdy::SpdySettingsIR>();
  for (const Http2Setting& setting : values) {
    settings->AddSetting(setting.id, setting.value);
  }
  frames_.push_back(std::move(settings));
  return *this;
}

TestFrameSequence& TestFrameSequence::SettingsAck() {
  auto settings = absl::make_unique<spdy::SpdySettingsIR>();
  settings->set_is_ack(true);
  frames_.push_back(std::move(settings));
  return *this;
}

TestFrameSequence& TestFrameSequence::Ping(Http2PingId id) {
  frames_.push_back(absl::make_unique<spdy::SpdyPingIR>(id));
  return *this;
}

TestFrameSequence& TestFrameSequence::PingAck(Http2PingId id) {
  auto ping = absl::make_unique<spdy::SpdyPingIR>(id);
  ping->set_is_ack(true);
  frames_.push_back(std::move(ping));
  return *this;
}

TestFrameSequence& TestFrameSequence::GoAway(Http2StreamId last_good_stream_id,
                                             Http2ErrorCode error,
                                             absl::string_view payload) {
  frames_.push_back(absl::make_unique<spdy::SpdyGoAwayIR>(
      last_good_stream_id, TranslateErrorCode(error), std::string(payload)));
  return *this;
}

TestFrameSequence& TestFrameSequence::Headers(Http2StreamId stream_id,
                                              spdy::Http2HeaderBlock block,
                                              bool fin) {
  auto headers =
      absl::make_unique<spdy::SpdyHeadersIR>(stream_id, std::move(block));
  headers->set_fin(fin);
  frames_.push_back(std::move(headers));
  return *this;
}

TestFrameSequence& TestFrameSequence::Headers(Http2StreamId stream_id,
                                              absl::Span<const Header> headers,
                                              bool fin) {
  spdy::SpdyHeaderBlock block;
  for (const Header& header : headers) {
    block[header.first] = header.second;
  }
  return Headers(stream_id, std::move(block), fin);
}

TestFrameSequence& TestFrameSequence::WindowUpdate(Http2StreamId stream_id,
                                                   int32_t delta) {
  frames_.push_back(
      absl::make_unique<spdy::SpdyWindowUpdateIR>(stream_id, delta));
  return *this;
}

TestFrameSequence& TestFrameSequence::Priority(Http2StreamId stream_id,
                                               Http2StreamId parent_stream_id,
                                               int weight,
                                               bool exclusive) {
  frames_.push_back(absl::make_unique<spdy::SpdyPriorityIR>(
      stream_id, parent_stream_id, weight, exclusive));
  return *this;
}

std::string TestFrameSequence::Serialize() {
  std::string result;
  if (!preface_.empty()) {
    result = preface_;
  }
  spdy::SpdyFramer framer(spdy::SpdyFramer::ENABLE_COMPRESSION);
  for (const auto& frame : frames_) {
    spdy::SpdySerializedFrame f = framer.SerializeFrame(*frame);
    absl::StrAppend(&result, absl::string_view(f));
  }
  return result;
}

}  // namespace test
}  // namespace adapter
}  // namespace http2
