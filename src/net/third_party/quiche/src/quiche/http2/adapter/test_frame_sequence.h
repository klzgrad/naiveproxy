#ifndef QUICHE_HTTP2_ADAPTER_TEST_FRAME_SEQUENCE_H_
#define QUICHE_HTTP2_ADAPTER_TEST_FRAME_SEQUENCE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {
namespace test {

std::vector<Header> QUICHE_NO_EXPORT ToHeaders(
    absl::Span<const std::pair<absl::string_view, absl::string_view>> headers);

class QUICHE_NO_EXPORT TestFrameSequence {
 public:
  TestFrameSequence() = default;

  TestFrameSequence(TestFrameSequence&& other) = default;
  TestFrameSequence& operator=(TestFrameSequence&& other) = default;

  TestFrameSequence& ClientPreface(
      absl::Span<const Http2Setting> settings = {});
  TestFrameSequence& ServerPreface(
      absl::Span<const Http2Setting> settings = {});
  TestFrameSequence& Data(Http2StreamId stream_id, absl::string_view payload,
                          bool fin = false,
                          std::optional<int> padding_length = std::nullopt);
  TestFrameSequence& RstStream(Http2StreamId stream_id, Http2ErrorCode error);
  TestFrameSequence& Settings(absl::Span<const Http2Setting> settings);
  TestFrameSequence& SettingsAck();
  TestFrameSequence& PushPromise(Http2StreamId stream_id,
                                 Http2StreamId promised_stream_id,
                                 absl::Span<const Header> headers);
  TestFrameSequence& Ping(Http2PingId id);
  TestFrameSequence& PingAck(Http2PingId id);
  TestFrameSequence& GoAway(Http2StreamId last_good_stream_id,
                            Http2ErrorCode error,
                            absl::string_view payload = "");
  TestFrameSequence& Headers(
      Http2StreamId stream_id,
      absl::Span<const std::pair<absl::string_view, absl::string_view>> headers,
      bool fin = false, bool add_continuation = false);
  TestFrameSequence& Headers(Http2StreamId stream_id,
                             spdy::Http2HeaderBlock block, bool fin = false,
                             bool add_continuation = false);
  TestFrameSequence& Headers(Http2StreamId stream_id,
                             absl::Span<const Header> headers, bool fin = false,
                             bool add_continuation = false);
  TestFrameSequence& WindowUpdate(Http2StreamId stream_id, int32_t delta);
  TestFrameSequence& Priority(Http2StreamId stream_id,
                              Http2StreamId parent_stream_id, int weight,
                              bool exclusive);
  TestFrameSequence& Metadata(Http2StreamId stream_id,
                              absl::string_view payload,
                              bool multiple_frames = false);

  std::string Serialize();

 private:
  std::string preface_;
  std::vector<std::unique_ptr<spdy::SpdyFrameIR>> frames_;
};

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_TEST_FRAME_SEQUENCE_H_
