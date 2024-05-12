#include "quiche/http2/adapter/test_utils.h"

#include <optional>
#include <ostream>

#include "absl/strings/str_format.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/spdy/core/hpack/hpack_encoder.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {
namespace test {
namespace {

using ConnectionError = Http2VisitorInterface::ConnectionError;

}  // anonymous namespace

TestDataFrameSource::TestDataFrameSource(Http2VisitorInterface& visitor,
                                         bool has_fin)
    : visitor_(visitor), has_fin_(has_fin) {}

void TestDataFrameSource::AppendPayload(absl::string_view payload) {
  QUICHE_CHECK(!end_data_);
  if (!payload.empty()) {
    payload_fragments_.push_back(std::string(payload));
    current_fragment_ = payload_fragments_.front();
  }
}

void TestDataFrameSource::EndData() { end_data_ = true; }

std::pair<int64_t, bool> TestDataFrameSource::SelectPayloadLength(
    size_t max_length) {
  if (return_error_) {
    return {DataFrameSource::kError, false};
  }
  // The stream is done if there's no more data, or if |max_length| is at least
  // as large as the remaining data.
  const bool end_data = end_data_ && (current_fragment_.empty() ||
                                      (payload_fragments_.size() == 1 &&
                                       max_length >= current_fragment_.size()));
  const int64_t length = std::min(max_length, current_fragment_.size());
  return {length, end_data};
}

bool TestDataFrameSource::Send(absl::string_view frame_header,
                               size_t payload_length) {
  QUICHE_LOG_IF(DFATAL, payload_length > current_fragment_.size())
      << "payload_length: " << payload_length
      << " current_fragment_size: " << current_fragment_.size();
  const std::string concatenated =
      absl::StrCat(frame_header, current_fragment_.substr(0, payload_length));
  const int64_t result = visitor_.OnReadyToSend(concatenated);
  if (result < 0) {
    // Write encountered error.
    visitor_.OnConnectionError(ConnectionError::kSendError);
    current_fragment_ = {};
    payload_fragments_.clear();
    return false;
  } else if (result == 0) {
    // Write blocked.
    return false;
  } else if (static_cast<size_t>(result) < concatenated.size()) {
    // Probably need to handle this better within this test class.
    QUICHE_LOG(DFATAL)
        << "DATA frame not fully flushed. Connection will be corrupt!";
    visitor_.OnConnectionError(ConnectionError::kSendError);
    current_fragment_ = {};
    payload_fragments_.clear();
    return false;
  }
  if (payload_length > 0) {
    current_fragment_.remove_prefix(payload_length);
  }
  if (current_fragment_.empty() && !payload_fragments_.empty()) {
    payload_fragments_.erase(payload_fragments_.begin());
    if (!payload_fragments_.empty()) {
      current_fragment_ = payload_fragments_.front();
    }
  }
  return true;
}

std::string EncodeHeaders(const spdy::Http2HeaderBlock& entries) {
  spdy::HpackEncoder encoder;
  encoder.DisableCompression();
  return encoder.EncodeHeaderBlock(entries);
}

TestMetadataSource::TestMetadataSource(const spdy::Http2HeaderBlock& entries)
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
