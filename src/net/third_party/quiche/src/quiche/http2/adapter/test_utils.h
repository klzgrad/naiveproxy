#ifndef QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
#define QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/chunked_buffer.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/mock_http2_visitor.h"
#include "quiche/http2/core/spdy_protocol.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

class QUICHE_NO_EXPORT TestVisitor
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

  DataFrameHeaderInfo OnReadyToSendDataForStream(Http2StreamId stream_id,
                                                 size_t max_length) override;
  bool SendDataFrame(Http2StreamId stream_id, absl::string_view frame_header,
                     size_t payload_bytes) override;

  // Test methods to manipulate the data frame payload to send for a stream.
  void AppendPayloadForStream(Http2StreamId stream_id,
                              absl::string_view payload);
  void SetEndData(Http2StreamId stream_id, bool end_stream);
  void SimulateError(Http2StreamId stream_id);

  std::pair<int64_t, bool> PackMetadataForStream(Http2StreamId stream_id,
                                                 uint8_t* dest,
                                                 size_t dest_len) override;

  // Test methods to manipulate the metadata payload to send for a stream.
  void AppendMetadataForStream(Http2StreamId stream_id,
                               const quiche::HttpHeaderBlock& payload);

  const std::string& data() { return data_; }
  void Clear() { data_.clear(); }

  void set_send_limit(size_t limit) { send_limit_ = limit; }

  bool is_write_blocked() const { return is_write_blocked_; }
  void set_is_write_blocked(bool value) { is_write_blocked_ = value; }

  void set_has_write_error() { has_write_error_ = true; }

 private:
  struct DataPayload {
    ChunkedBuffer data;
    bool end_data = false;
    bool end_stream = false;
    bool return_error = false;
  };
  std::string data_;
  absl::flat_hash_map<Http2StreamId, std::vector<std::string>> metadata_map_;
  absl::flat_hash_map<Http2StreamId, DataPayload> data_map_;
  absl::flat_hash_map<Http2StreamId, std::string> outbound_metadata_map_;
  size_t send_limit_ = std::numeric_limits<size_t>::max();
  bool is_write_blocked_ = false;
  bool has_write_error_ = false;
};

// A DataFrameSource that invokes visitor methods.
class QUICHE_NO_EXPORT VisitorDataSource : public DataFrameSource {
 public:
  VisitorDataSource(Http2VisitorInterface& visitor, Http2StreamId stream_id);

  std::pair<int64_t, bool> SelectPayloadLength(size_t max_length) override;
  bool Send(absl::string_view frame_header, size_t payload_length) override;
  bool send_fin() const override;

 private:
  Http2VisitorInterface& visitor_;
  const Http2StreamId stream_id_;
  // Whether the stream should end with the final frame of data.
  bool has_fin_ = false;
};

class QUICHE_NO_EXPORT TestMetadataSource : public MetadataSource {
 public:
  explicit TestMetadataSource(const quiche::HttpHeaderBlock& entries);

  size_t NumFrames(size_t max_frame_size) const override {
    // Round up to the next frame.
    return (encoded_entries_.size() + max_frame_size - 1) / max_frame_size;
  }
  std::pair<int64_t, bool> Pack(uint8_t* dest, size_t dest_len) override;
  void OnFailure() override {}

  void InjectFailure() { fail_when_packing_ = true; }

 private:
  const std::string encoded_entries_;
  absl::string_view remaining_;
  bool fail_when_packing_ = false;
};

// These matchers check whether a string consists entirely of HTTP/2 frames of
// the specified ordered sequence. This is useful in tests where we want to show
// that one or more particular frame types are serialized for sending to the
// peer. The match will fail if there are input bytes not consumed by the
// matcher.

// Requires that frames match both types and lengths.
testing::Matcher<absl::string_view> EqualsFrames(
    std::vector<std::pair<spdy::SpdyFrameType, std::optional<size_t>>>
        types_and_lengths);

// Requires that frames match the specified types.
testing::Matcher<absl::string_view> EqualsFrames(
    std::vector<spdy::SpdyFrameType> types);

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_TEST_UTILS_H_
