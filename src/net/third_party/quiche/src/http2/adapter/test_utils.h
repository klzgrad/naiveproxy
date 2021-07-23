#ifndef QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
#define QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/mock_http2_visitor.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"
#include "common/platform/api/quiche_test.h"
#include "spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {
namespace test {

class DataSavingVisitor : public testing::StrictMock<MockHttp2Visitor> {
 public:
  void Save(absl::string_view data) { absl::StrAppend(&data_, data); }

  const std::string& data() { return data_; }
  void Clear() { data_.clear(); }

 private:
  std::string data_;
};

// These matchers check whether a string consists entirely of HTTP/2 frames of
// the specified ordered sequence. This is useful in tests where we want to show
// that one or more particular frame types are serialized for sending to the
// peer. The match will fail if there are input bytes not consumed by the
// matcher.

// Requires that frames match both types and lengths.
testing::Matcher<absl::string_view> EqualsFrames(
    std::vector<std::pair<spdy::SpdyFrameType, absl::optional<size_t>>>
        types_and_lengths);

// Requires that frames match the specified types.
testing::Matcher<absl::string_view> EqualsFrames(
    std::vector<spdy::SpdyFrameType> types);

testing::Matcher<const nghttp2_frame_hd*> HasFrameHeader(
    uint32_t streamid,
    uint8_t type,
    const testing::Matcher<int> flags);

testing::Matcher<const nghttp2_frame*> IsData(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<size_t> length,
    const testing::Matcher<int> flags);

testing::Matcher<const nghttp2_frame*> IsHeaders(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<int> flags,
    const testing::Matcher<int> category);

testing::Matcher<const nghttp2_frame*> IsRstStream(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<uint32_t> error_code);

testing::Matcher<const nghttp2_frame*> IsSettings(
    const testing::Matcher<std::vector<Http2Setting>> values);

testing::Matcher<const nghttp2_frame*> IsPing(
    const testing::Matcher<uint64_t> id);

testing::Matcher<const nghttp2_frame*> IsPingAck(
    const testing::Matcher<uint64_t> id);

testing::Matcher<const nghttp2_frame*> IsGoAway(
    const testing::Matcher<uint32_t> last_stream_id,
    const testing::Matcher<uint32_t> error_code,
    const testing::Matcher<absl::string_view> opaque_data);

testing::Matcher<const nghttp2_frame*> IsWindowUpdate(
    const testing::Matcher<uint32_t> delta);

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
