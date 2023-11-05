#ifndef QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
#define QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/mock_http2_visitor.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {
namespace test {

class QUICHE_NO_EXPORT DataSavingVisitor
    : public testing::StrictMock<MockHttp2Visitor> {
 public:
  int64_t OnReadyToSend(absl::string_view data) override {
    if (has_write_error_) {
      return kSendError;
    }
    if (is_write_blocked_) {
      return kSendBlocked;
    }
    const size_t to_accept = std::min(send_limit_, data.size());
    if (to_accept == 0) {
      return kSendBlocked;
    }
    absl::StrAppend(&data_, data.substr(0, to_accept));
    return to_accept;
  }

  bool OnMetadataForStream(Http2StreamId stream_id,
                           absl::string_view metadata) override {
    const bool ret = testing::StrictMock<MockHttp2Visitor>::OnMetadataForStream(
        stream_id, metadata);
    if (ret) {
      auto result =
          metadata_map_.try_emplace(stream_id, std::vector<std::string>());
      result.first->second.push_back(std::string(metadata));
    }
    return ret;
  }

  const std::vector<std::string> GetMetadata(Http2StreamId stream_id) {
    auto it = metadata_map_.find(stream_id);
    if (it == metadata_map_.end()) {
      return {};
    } else {
      return it->second;
    }
  }

  const std::string& data() { return data_; }
  void Clear() { data_.clear(); }

  void set_send_limit(size_t limit) { send_limit_ = limit; }

  bool is_write_blocked() const { return is_write_blocked_; }
  void set_is_write_blocked(bool value) { is_write_blocked_ = value; }

  void set_has_write_error() { has_write_error_ = true; }

 private:
  std::string data_;
  absl::flat_hash_map<Http2StreamId, std::vector<std::string>> metadata_map_;
  size_t send_limit_ = std::numeric_limits<size_t>::max();
  bool is_write_blocked_ = false;
  bool has_write_error_ = false;
};

// A test DataFrameSource. Starts out in the empty, blocked state.
class QUICHE_NO_EXPORT TestDataFrameSource : public DataFrameSource {
 public:
  TestDataFrameSource(Http2VisitorInterface& visitor, bool has_fin);

  void AppendPayload(absl::string_view payload);
  void EndData();
  void SimulateError() { return_error_ = true; }

  std::pair<int64_t, bool> SelectPayloadLength(size_t max_length) override;
  bool Send(absl::string_view frame_header, size_t payload_length) override;
  bool send_fin() const override { return has_fin_; }

 private:
  Http2VisitorInterface& visitor_;
  std::vector<std::string> payload_fragments_;
  absl::string_view current_fragment_;
  // Whether the stream should end with the final frame of data.
  const bool has_fin_;
  // Whether |payload_fragments_| contains the final segment of data.
  bool end_data_ = false;
  // Whether SelectPayloadLength() should return an error.
  bool return_error_ = false;
};

class QUICHE_NO_EXPORT TestMetadataSource : public MetadataSource {
 public:
  explicit TestMetadataSource(const spdy::Http2HeaderBlock& entries);

  size_t NumFrames(size_t max_frame_size) const override {
    // Round up to the next frame.
    return (encoded_entries_.size() + max_frame_size - 1) / max_frame_size;
  }
  std::pair<int64_t, bool> Pack(uint8_t* dest, size_t dest_len) override;
  void OnFailure() override {}

 private:
  const std::string encoded_entries_;
  absl::string_view remaining_;
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

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
