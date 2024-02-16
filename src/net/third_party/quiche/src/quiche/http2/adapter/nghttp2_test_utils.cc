#include "quiche/http2/adapter/nghttp2_test_utils.h"

#include "quiche/http2/adapter/nghttp2_util.h"
#include "quiche/common/quiche_endian.h"

namespace http2 {
namespace adapter {
namespace test {

namespace {

// Custom gMock matcher, used to implement HasFrameHeader().
class FrameHeaderMatcher {
 public:
  FrameHeaderMatcher(int32_t streamid, uint8_t type,
                     const testing::Matcher<int> flags)
      : stream_id_(streamid), type_(type), flags_(flags) {}

  bool Match(const nghttp2_frame_hd& frame,
             testing::MatchResultListener* listener) const {
    bool matched = true;
    if (stream_id_ != frame.stream_id) {
      *listener << "; expected stream " << stream_id_ << ", saw "
                << frame.stream_id;
      matched = false;
    }
    if (type_ != frame.type) {
      *listener << "; expected frame type " << type_ << ", saw "
                << static_cast<int>(frame.type);
      matched = false;
    }
    if (!flags_.MatchAndExplain(frame.flags, listener)) {
      matched = false;
    }
    return matched;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "contains a frame header with stream " << stream_id_ << ", type "
        << type_ << ", ";
    flags_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not contain a frame header with stream " << stream_id_
        << ", type " << type_ << ", ";
    flags_.DescribeNegationTo(os);
  }

 private:
  const int32_t stream_id_;
  const int type_;
  const testing::Matcher<int> flags_;
};

class PointerToFrameHeaderMatcher
    : public FrameHeaderMatcher,
      public testing::MatcherInterface<const nghttp2_frame_hd*> {
 public:
  PointerToFrameHeaderMatcher(int32_t streamid, uint8_t type,
                              const testing::Matcher<int> flags)
      : FrameHeaderMatcher(streamid, type, flags) {}

  bool MatchAndExplain(const nghttp2_frame_hd* frame,
                       testing::MatchResultListener* listener) const override {
    return FrameHeaderMatcher::Match(*frame, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    FrameHeaderMatcher::DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    FrameHeaderMatcher::DescribeNegationTo(os);
  }
};

class ReferenceToFrameHeaderMatcher
    : public FrameHeaderMatcher,
      public testing::MatcherInterface<const nghttp2_frame_hd&> {
 public:
  ReferenceToFrameHeaderMatcher(int32_t streamid, uint8_t type,
                                const testing::Matcher<int> flags)
      : FrameHeaderMatcher(streamid, type, flags) {}

  bool MatchAndExplain(const nghttp2_frame_hd& frame,
                       testing::MatchResultListener* listener) const override {
    return FrameHeaderMatcher::Match(frame, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    FrameHeaderMatcher::DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    FrameHeaderMatcher::DescribeNegationTo(os);
  }
};

class DataMatcher : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  DataMatcher(const testing::Matcher<uint32_t> stream_id,
              const testing::Matcher<size_t> length,
              const testing::Matcher<int> flags,
              const testing::Matcher<size_t> padding)
      : stream_id_(stream_id),
        length_(length),
        flags_(flags),
        padding_(padding) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_DATA) {
      *listener << "; expected DATA frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    bool matched = true;
    if (!stream_id_.MatchAndExplain(frame->hd.stream_id, listener)) {
      matched = false;
    }
    if (!length_.MatchAndExplain(frame->hd.length, listener)) {
      matched = false;
    }
    if (!flags_.MatchAndExplain(frame->hd.flags, listener)) {
      matched = false;
    }
    if (!padding_.MatchAndExplain(frame->data.padlen, listener)) {
      matched = false;
    }
    return matched;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a DATA frame, ";
    stream_id_.DescribeTo(os);
    length_.DescribeTo(os);
    flags_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a DATA frame, ";
    stream_id_.DescribeNegationTo(os);
    length_.DescribeNegationTo(os);
    flags_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<uint32_t> stream_id_;
  const testing::Matcher<size_t> length_;
  const testing::Matcher<int> flags_;
  const testing::Matcher<size_t> padding_;
};

class HeadersMatcher : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  HeadersMatcher(const testing::Matcher<uint32_t> stream_id,
                 const testing::Matcher<int> flags,
                 const testing::Matcher<int> category)
      : stream_id_(stream_id), flags_(flags), category_(category) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_HEADERS) {
      *listener << "; expected HEADERS frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    bool matched = true;
    if (!stream_id_.MatchAndExplain(frame->hd.stream_id, listener)) {
      matched = false;
    }
    if (!flags_.MatchAndExplain(frame->hd.flags, listener)) {
      matched = false;
    }
    if (!category_.MatchAndExplain(frame->headers.cat, listener)) {
      matched = false;
    }
    return matched;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a HEADERS frame, ";
    stream_id_.DescribeTo(os);
    flags_.DescribeTo(os);
    category_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a HEADERS frame, ";
    stream_id_.DescribeNegationTo(os);
    flags_.DescribeNegationTo(os);
    category_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<uint32_t> stream_id_;
  const testing::Matcher<int> flags_;
  const testing::Matcher<int> category_;
};

class RstStreamMatcher
    : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  RstStreamMatcher(const testing::Matcher<uint32_t> stream_id,
                   const testing::Matcher<uint32_t> error_code)
      : stream_id_(stream_id), error_code_(error_code) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_RST_STREAM) {
      *listener << "; expected RST_STREAM frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    bool matched = true;
    if (!stream_id_.MatchAndExplain(frame->hd.stream_id, listener)) {
      matched = false;
    }
    if (!error_code_.MatchAndExplain(frame->rst_stream.error_code, listener)) {
      matched = false;
    }
    return matched;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a RST_STREAM frame, ";
    stream_id_.DescribeTo(os);
    error_code_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a RST_STREAM frame, ";
    stream_id_.DescribeNegationTo(os);
    error_code_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<uint32_t> stream_id_;
  const testing::Matcher<uint32_t> error_code_;
};

class SettingsMatcher : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  SettingsMatcher(const testing::Matcher<std::vector<Http2Setting>> values)
      : values_(values) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_SETTINGS) {
      *listener << "; expected SETTINGS frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    std::vector<Http2Setting> settings;
    settings.reserve(frame->settings.niv);
    for (size_t i = 0; i < frame->settings.niv; ++i) {
      const auto& p = frame->settings.iv[i];
      settings.push_back({static_cast<uint16_t>(p.settings_id), p.value});
    }
    return values_.MatchAndExplain(settings, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a SETTINGS frame, ";
    values_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a SETTINGS frame, ";
    values_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<std::vector<Http2Setting>> values_;
};

class PingMatcher : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  PingMatcher(const testing::Matcher<uint64_t> id, bool is_ack)
      : id_(id), is_ack_(is_ack) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_PING) {
      *listener << "; expected PING frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    bool matched = true;
    bool frame_ack = frame->hd.flags & NGHTTP2_FLAG_ACK;
    if (is_ack_ != frame_ack) {
      *listener << "; expected is_ack=" << is_ack_ << ", saw " << frame_ack;
      matched = false;
    }
    uint64_t data;
    std::memcpy(&data, frame->ping.opaque_data, sizeof(data));
    data = quiche::QuicheEndian::HostToNet64(data);
    if (!id_.MatchAndExplain(data, listener)) {
      matched = false;
    }
    return matched;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a PING frame, ";
    id_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a PING frame, ";
    id_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<uint64_t> id_;
  const bool is_ack_;
};

class GoAwayMatcher : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  GoAwayMatcher(const testing::Matcher<uint32_t> last_stream_id,
                const testing::Matcher<uint32_t> error_code,
                const testing::Matcher<absl::string_view> opaque_data)
      : last_stream_id_(last_stream_id),
        error_code_(error_code),
        opaque_data_(opaque_data) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_GOAWAY) {
      *listener << "; expected GOAWAY frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    bool matched = true;
    if (!last_stream_id_.MatchAndExplain(frame->goaway.last_stream_id,
                                         listener)) {
      matched = false;
    }
    if (!error_code_.MatchAndExplain(frame->goaway.error_code, listener)) {
      matched = false;
    }
    auto opaque_data =
        ToStringView(frame->goaway.opaque_data, frame->goaway.opaque_data_len);
    if (!opaque_data_.MatchAndExplain(opaque_data, listener)) {
      matched = false;
    }
    return matched;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a GOAWAY frame, ";
    last_stream_id_.DescribeTo(os);
    error_code_.DescribeTo(os);
    opaque_data_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a GOAWAY frame, ";
    last_stream_id_.DescribeNegationTo(os);
    error_code_.DescribeNegationTo(os);
    opaque_data_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<uint32_t> last_stream_id_;
  const testing::Matcher<uint32_t> error_code_;
  const testing::Matcher<absl::string_view> opaque_data_;
};

class WindowUpdateMatcher
    : public testing::MatcherInterface<const nghttp2_frame*> {
 public:
  WindowUpdateMatcher(const testing::Matcher<uint32_t> delta) : delta_(delta) {}

  bool MatchAndExplain(const nghttp2_frame* frame,
                       testing::MatchResultListener* listener) const override {
    if (frame->hd.type != NGHTTP2_WINDOW_UPDATE) {
      *listener << "; expected WINDOW_UPDATE frame, saw frame of type "
                << static_cast<int>(frame->hd.type);
      return false;
    }
    return delta_.MatchAndExplain(frame->window_update.window_size_increment,
                                  listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "contains a WINDOW_UPDATE frame, ";
    delta_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not contain a WINDOW_UPDATE frame, ";
    delta_.DescribeNegationTo(os);
  }

 private:
  const testing::Matcher<uint32_t> delta_;
};

}  // namespace

testing::Matcher<const nghttp2_frame_hd*> HasFrameHeader(
    uint32_t streamid, uint8_t type, const testing::Matcher<int> flags) {
  return MakeMatcher(new PointerToFrameHeaderMatcher(streamid, type, flags));
}

testing::Matcher<const nghttp2_frame_hd&> HasFrameHeaderRef(
    uint32_t streamid, uint8_t type, const testing::Matcher<int> flags) {
  return MakeMatcher(new ReferenceToFrameHeaderMatcher(streamid, type, flags));
}

testing::Matcher<const nghttp2_frame*> IsData(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<size_t> length, const testing::Matcher<int> flags,
    const testing::Matcher<size_t> padding) {
  return MakeMatcher(new DataMatcher(stream_id, length, flags, padding));
}

testing::Matcher<const nghttp2_frame*> IsHeaders(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<int> flags, const testing::Matcher<int> category) {
  return MakeMatcher(new HeadersMatcher(stream_id, flags, category));
}

testing::Matcher<const nghttp2_frame*> IsRstStream(
    const testing::Matcher<uint32_t> stream_id,
    const testing::Matcher<uint32_t> error_code) {
  return MakeMatcher(new RstStreamMatcher(stream_id, error_code));
}

testing::Matcher<const nghttp2_frame*> IsSettings(
    const testing::Matcher<std::vector<Http2Setting>> values) {
  return MakeMatcher(new SettingsMatcher(values));
}

testing::Matcher<const nghttp2_frame*> IsPing(
    const testing::Matcher<uint64_t> id) {
  return MakeMatcher(new PingMatcher(id, false));
}

testing::Matcher<const nghttp2_frame*> IsPingAck(
    const testing::Matcher<uint64_t> id) {
  return MakeMatcher(new PingMatcher(id, true));
}

testing::Matcher<const nghttp2_frame*> IsGoAway(
    const testing::Matcher<uint32_t> last_stream_id,
    const testing::Matcher<uint32_t> error_code,
    const testing::Matcher<absl::string_view> opaque_data) {
  return MakeMatcher(
      new GoAwayMatcher(last_stream_id, error_code, opaque_data));
}

testing::Matcher<const nghttp2_frame*> IsWindowUpdate(
    const testing::Matcher<uint32_t> delta) {
  return MakeMatcher(new WindowUpdateMatcher(delta));
}

}  // namespace test
}  // namespace adapter
}  // namespace http2
