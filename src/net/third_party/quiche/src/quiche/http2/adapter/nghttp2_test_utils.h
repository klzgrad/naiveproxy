#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_TEST_UTILS_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_TEST_UTILS_H_

#include <cstdint>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/nghttp2.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

// A simple class that can easily be adapted to act as a nghttp2_data_source.
class QUICHE_NO_EXPORT TestDataSource {
 public:
  explicit TestDataSource(absl::string_view data) : data_(std::string(data)) {}

  absl::string_view ReadNext(size_t size) {
    const size_t to_send = std::min(size, remaining_.size());
    auto ret = remaining_.substr(0, to_send);
    remaining_.remove_prefix(to_send);
    return ret;
  }

  size_t SelectPayloadLength(size_t max_length) {
    return std::min(max_length, remaining_.size());
  }

  nghttp2_data_provider MakeDataProvider() {
    nghttp2_data_source s;
    s.ptr = this;
    return nghttp2_data_provider{
        s,
        [](nghttp2_session*, int32_t, uint8_t*, size_t length,
           uint32_t* data_flags, nghttp2_data_source* source,
           void*) -> ssize_t {
          *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
          auto* s = static_cast<TestDataSource*>(source->ptr);
          if (!s->is_data_available()) {
            return NGHTTP2_ERR_DEFERRED;
          }
          const ssize_t ret = s->SelectPayloadLength(length);
          if (ret < static_cast<ssize_t>(length)) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
          }
          return ret;
        }};
  }

  bool is_data_available() const { return is_data_available_; }
  void set_is_data_available(bool value) { is_data_available_ = value; }

 private:
  const std::string data_;
  absl::string_view remaining_ = data_;
  bool is_data_available_ = true;
};

// Matchers for nghttp2 data types.
testing::Matcher<const nghttp2_frame_hd*> HasFrameHeader(
    uint32_t streamid, uint8_t type, const testing::Matcher<int> flags);
testing::Matcher<const nghttp2_frame_hd&> HasFrameHeaderRef(
    uint32_t streamid, uint8_t type, const testing::Matcher<int> flags);

testing::Matcher<const nghttp2_frame*> IsData(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<size_t> length, const testing::Matcher<int> flags,
    const testing::Matcher<size_t> padding = testing::_);

testing::Matcher<const nghttp2_frame*> IsHeaders(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<int> flags, const testing::Matcher<int> category);

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

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_TEST_UTILS_H_
