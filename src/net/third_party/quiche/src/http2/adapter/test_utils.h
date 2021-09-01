#ifndef QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
#define QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "http2/adapter/data_source.h"
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
  ssize_t OnReadyToSend(absl::string_view data) override {
    if (is_write_blocked_) {
      return 0;
    }
    const size_t to_accept = std::min(send_limit_, data.size());
    absl::StrAppend(&data_, data.substr(0, to_accept));
    return to_accept;
  }

  const std::string& data() { return data_; }
  void Clear() { data_.clear(); }

  void set_send_limit(size_t limit) { send_limit_ = limit; }

  bool is_write_blocked() const { return is_write_blocked_; }
  void set_is_write_blocked(bool value) { is_write_blocked_ = value; }

 private:
  std::string data_;
  size_t send_limit_ = std::numeric_limits<size_t>::max();
  bool is_write_blocked_ = false;
};

// A test DataFrameSource that can be initialized with a single string payload,
// or a chunked payload.
class TestDataFrameSource : public DataFrameSource {
 public:
  TestDataFrameSource(Http2VisitorInterface& visitor,
                      absl::string_view data_payload,
                      bool has_fin = true);

  TestDataFrameSource(Http2VisitorInterface& visitor,
                      absl::Span<absl::string_view> payload_fragments,
                      bool has_fin = true);

  std::pair<ssize_t, bool> SelectPayloadLength(size_t max_length) override;
  bool Send(absl::string_view frame_header, size_t payload_length) override;
  bool send_fin() const override { return has_fin_; }

  void set_is_data_available(bool value) { is_data_available_ = value; }

 private:
  Http2VisitorInterface& visitor_;
  std::vector<std::string> payload_fragments_;
  absl::string_view current_fragment_;
  const bool has_fin_;
  bool is_data_available_ = true;
};

// A simple class that can easily be adapted to act as a nghttp2_data_source.
class TestDataSource {
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
    return nghttp2_data_provider{
        .source = {.ptr = this},
        .read_callback = [](nghttp2_session*, int32_t, uint8_t*, size_t length,
                            uint32_t* data_flags, nghttp2_data_source* source,
                            void*) -> ssize_t {
          *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
          auto* s = static_cast<TestDataSource*>(source->ptr);
          if (!s->is_data_available()) {
            return NGHTTP2_ERR_DEFERRED;
          }
          const ssize_t ret = s->SelectPayloadLength(length);
          if (ret < length) {
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
