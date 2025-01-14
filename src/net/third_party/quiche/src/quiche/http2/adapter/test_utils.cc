#include "quiche/http2/adapter/test_utils.h"

#include <cstring>
#include <optional>
#include <ostream>
#include <vector>

#include "absl/strings/str_format.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/core/spdy_protocol.h"
#include "quiche/http2/hpack/hpack_encoder.h"
#include "quiche/common/quiche_data_reader.h"

namespace http2 {
namespace adapter {
namespace test {
namespace {

using ConnectionError = Http2VisitorInterface::ConnectionError;

std::string EncodeHeaders(const quiche::HttpHeaderBlock& entries) {
  spdy::HpackEncoder encoder;
  encoder.DisableCompression();
  return encoder.EncodeHeaderBlock(entries);
}

}  // anonymous namespace

TestVisitor::DataFrameHeaderInfo TestVisitor::OnReadyToSendDataForStream(
    Http2StreamId stream_id, size_t max_length) {
  auto it = data_map_.find(stream_id);
  if (it == data_map_.end()) {
    QUICHE_DVLOG(1) << "Source not in map; returning blocked.";
    return {0, false, false};
  }
  DataPayload& payload = it->second;
  if (payload.return_error) {
    QUICHE_DVLOG(1) << "Simulating error response for stream " << stream_id;
    return {DataFrameSource::kError, false, false};
  }
  const absl::string_view prefix = payload.data.GetPrefix();
  const size_t frame_length = std::min(max_length, prefix.size());
  const bool is_final_fragment = payload.data.Read().size() <= 1;
  const bool end_data =
      payload.end_data && is_final_fragment && frame_length == prefix.size();
  const bool end_stream = payload.end_stream && end_data;
  return {static_cast<int64_t>(frame_length), end_data, end_stream};
}

bool TestVisitor::SendDataFrame(Http2StreamId stream_id,
                                absl::string_view frame_header,
                                size_t payload_bytes) {
  // Sends the frame header.
  const int64_t frame_result = OnReadyToSend(frame_header);
  if (frame_result < 0 ||
      static_cast<size_t>(frame_result) != frame_header.size()) {
    return false;
  }
  auto it = data_map_.find(stream_id);
  if (it == data_map_.end()) {
    if (payload_bytes > 0) {
      // No bytes available to send; error condition.
      return false;
    } else {
      return true;
    }
  }
  DataPayload& payload = it->second;
  absl::string_view frame_payload = payload.data.GetPrefix();
  if (frame_payload.size() < payload_bytes) {
    // Not enough bytes available to send; error condition.
    return false;
  }
  frame_payload = frame_payload.substr(0, payload_bytes);
  // Sends the frame payload.
  const int64_t payload_result = OnReadyToSend(frame_payload);
  if (payload_result < 0 ||
      static_cast<size_t>(payload_result) != frame_payload.size()) {
    return false;
  }
  payload.data.RemovePrefix(payload_bytes);
  return true;
}

void TestVisitor::AppendPayloadForStream(Http2StreamId stream_id,
                                         absl::string_view payload) {
  // Allocates and appends a chunk of memory to hold `payload`, in case the test
  // is depending on specific DATA frame boundaries.
  auto char_data = std::unique_ptr<char[]>(new char[payload.size()]);
  std::copy(payload.begin(), payload.end(), char_data.get());
  data_map_[stream_id].data.Append(std::move(char_data), payload.size());
}

void TestVisitor::SetEndData(Http2StreamId stream_id, bool end_stream) {
  DataPayload& payload = data_map_[stream_id];
  payload.end_data = true;
  payload.end_stream = end_stream;
}

void TestVisitor::SimulateError(Http2StreamId stream_id) {
  DataPayload& payload = data_map_[stream_id];
  payload.return_error = true;
}

std::pair<int64_t, bool> TestVisitor::PackMetadataForStream(
    Http2StreamId stream_id, uint8_t* dest, size_t dest_len) {
  auto it = outbound_metadata_map_.find(stream_id);
  if (it == outbound_metadata_map_.end()) {
    return {-1, false};
  }
  const size_t to_copy = std::min(it->second.size(), dest_len);
  auto* src = reinterpret_cast<uint8_t*>(it->second.data());
  std::copy(src, src + to_copy, dest);
  it->second = it->second.substr(to_copy);
  if (it->second.empty()) {
    outbound_metadata_map_.erase(it);
    return {to_copy, true};
  }
  return {to_copy, false};
}

void TestVisitor::AppendMetadataForStream(
    Http2StreamId stream_id, const quiche::HttpHeaderBlock& payload) {
  outbound_metadata_map_.insert({stream_id, EncodeHeaders(payload)});
}

VisitorDataSource::VisitorDataSource(Http2VisitorInterface& visitor,
                                     Http2StreamId stream_id)
    : visitor_(visitor), stream_id_(stream_id) {}

bool VisitorDataSource::send_fin() const { return has_fin_; }

std::pair<int64_t, bool> VisitorDataSource::SelectPayloadLength(
    size_t max_length) {
  auto [payload_length, end_data, end_stream] =
      visitor_.OnReadyToSendDataForStream(stream_id_, max_length);
  has_fin_ = end_stream;
  return {payload_length, end_data};
}

bool VisitorDataSource::Send(absl::string_view frame_header,
                             size_t payload_length) {
  return visitor_.SendDataFrame(stream_id_, frame_header, payload_length);
}

TestMetadataSource::TestMetadataSource(const quiche::HttpHeaderBlock& entries)
    : encoded_entries_(EncodeHeaders(entries)) {
  remaining_ = encoded_entries_;
}

std::pair<int64_t, bool> TestMetadataSource::Pack(uint8_t* dest,
                                                  size_t dest_len) {
  if (fail_when_packing_) {
    return {-1, false};
  }
  const size_t copied = std::min(dest_len, remaining_.size());
  std::memcpy(dest, remaining_.data(), copied);
  remaining_.remove_prefix(copied);
  return std::make_pair(copied, remaining_.empty());
}

namespace {

using TypeAndOptionalLength =
    std::pair<spdy::SpdyFrameType, std::optional<size_t>>;

std::ostream& operator<<(
    std::ostream& os,
    const std::vector<TypeAndOptionalLength>& types_and_lengths) {
  for (const auto& type_and_length : types_and_lengths) {
    os << "(" << spdy::FrameTypeToString(type_and_length.first) << ", "
       << (type_and_length.second ? absl::StrCat(type_and_length.second.value())
                                  : "<unspecified>")
       << ") ";
  }
  return os;
}

std::string FrameTypeToString(uint8_t frame_type) {
  if (spdy::IsDefinedFrameType(frame_type)) {
    return spdy::FrameTypeToString(spdy::ParseFrameType(frame_type));
  } else {
    return absl::StrFormat("0x%x", static_cast<int>(frame_type));
  }
}

// Custom gMock matcher, used to implement EqualsFrames().
class SpdyControlFrameMatcher
    : public testing::MatcherInterface<absl::string_view> {
 public:
  explicit SpdyControlFrameMatcher(
      std::vector<TypeAndOptionalLength> types_and_lengths)
      : expected_types_and_lengths_(std::move(types_and_lengths)) {}

  bool MatchAndExplain(absl::string_view s,
                       testing::MatchResultListener* listener) const override {
    quiche::QuicheDataReader reader(s.data(), s.size());

    for (TypeAndOptionalLength expected : expected_types_and_lengths_) {
      if (!MatchAndExplainOneFrame(expected.first, expected.second, &reader,
                                   listener)) {
        return false;
      }
    }
    if (!reader.IsDoneReading()) {
      *listener << "; " << reader.BytesRemaining() << " bytes left to read!";
      return false;
    }
    return true;
  }

  bool MatchAndExplainOneFrame(spdy::SpdyFrameType expected_type,
                               std::optional<size_t> expected_length,
                               quiche::QuicheDataReader* reader,
                               testing::MatchResultListener* listener) const {
    uint32_t payload_length;
    if (!reader->ReadUInt24(&payload_length)) {
      *listener << "; unable to read length field for expected_type "
                << FrameTypeToString(expected_type) << ". data too short!";
      return false;
    }

    if (expected_length && payload_length != expected_length.value()) {
      *listener << "; actual length: " << payload_length
                << " but expected length: " << expected_length.value();
      return false;
    }

    uint8_t raw_type;
    if (!reader->ReadUInt8(&raw_type)) {
      *listener << "; unable to read type field for expected_type "
                << FrameTypeToString(expected_type) << ". data too short!";
      return false;
    }

    if (raw_type != static_cast<uint8_t>(expected_type)) {
      *listener << "; actual type: " << FrameTypeToString(raw_type)
                << " but expected type: " << FrameTypeToString(expected_type);
      return false;
    }

    // Seek past flags (1B), stream ID (4B), and payload. Reach the next frame.
    reader->Seek(5 + payload_length);
    return true;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "Data contains frames of types in sequence "
        << expected_types_and_lengths_;
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "Data does not contain frames of types in sequence "
        << expected_types_and_lengths_;
  }

 private:
  const std::vector<TypeAndOptionalLength> expected_types_and_lengths_;
};

}  // namespace

testing::Matcher<absl::string_view> EqualsFrames(
    std::vector<std::pair<spdy::SpdyFrameType, std::optional<size_t>>>
        types_and_lengths) {
  return MakeMatcher(new SpdyControlFrameMatcher(std::move(types_and_lengths)));
}

testing::Matcher<absl::string_view> EqualsFrames(
    std::vector<spdy::SpdyFrameType> types) {
  std::vector<std::pair<spdy::SpdyFrameType, std::optional<size_t>>>
      types_and_lengths;
  types_and_lengths.reserve(types.size());
  for (spdy::SpdyFrameType type : types) {
    types_and_lengths.push_back({type, std::nullopt});
  }
  return MakeMatcher(new SpdyControlFrameMatcher(std::move(types_and_lengths)));
}

}  // namespace test
}  // namespace adapter
}  // namespace http2
