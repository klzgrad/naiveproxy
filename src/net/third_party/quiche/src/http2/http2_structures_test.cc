// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/http2_structures.h"

// Tests are focused on Http2FrameHeader because it has by far the most
// methods of any of the structures.
// Note that EXPECT.*DEATH tests are slow (a fork is probably involved).

// And in case you're wondering, yes, these are ridiculously thorough tests,
// but believe it or not, I've found stupid bugs this way.

#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/http2_structures_test_util.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"

using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::Combine;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;
using ::testing::Not;
using ::testing::Values;
using ::testing::ValuesIn;

namespace http2 {
namespace test {
namespace {

template <typename E>
E IncrementEnum(E e) {
  using I = typename std::underlying_type<E>::type;
  return static_cast<E>(1 + static_cast<I>(e));
}

template <class T>
AssertionResult VerifyRandomCalls() {
  T t1;
  Http2Random seq1;
  Randomize(&t1, &seq1);

  T t2;
  Http2Random seq2(seq1.Key());
  Randomize(&t2, &seq2);

  // The two Randomize calls should have made the same number of calls into
  // the Http2Random implementations.
  VERIFY_EQ(seq1.Rand64(), seq2.Rand64());

  // And because Http2Random implementation is returning the same sequence, and
  // Randomize should have been consistent in applying those results, the two
  // Ts should have the same value.
  VERIFY_EQ(t1, t2);

  Randomize(&t2, &seq2);
  VERIFY_NE(t1, t2);

  Randomize(&t1, &seq1);
  VERIFY_EQ(t1, t2);

  VERIFY_EQ(seq1.Rand64(), seq2.Rand64());

  return AssertionSuccess();
}

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
std::vector<Http2FrameType> ValidFrameTypes() {
  std::vector<Http2FrameType> valid_types{Http2FrameType::DATA};
  while (valid_types.back() != Http2FrameType::ALTSVC) {
    valid_types.push_back(IncrementEnum(valid_types.back()));
  }
  return valid_types;
}
#endif  // GTEST_HAS_DEATH_TEST && !defined(NDEBUG)

TEST(Http2FrameHeaderTest, Constructor) {
  Http2Random random;
  uint8_t frame_type = 0;
  do {
    // Only the payload length is DCHECK'd in the constructor, so we need to
    // make sure it is a "uint24".
    uint32_t payload_length = random.Rand32() & 0xffffff;
    Http2FrameType type = static_cast<Http2FrameType>(frame_type);
    uint8_t flags = random.Rand8();
    uint32_t stream_id = random.Rand32();

    Http2FrameHeader v(payload_length, type, flags, stream_id);

    EXPECT_EQ(payload_length, v.payload_length);
    EXPECT_EQ(type, v.type);
    EXPECT_EQ(flags, v.flags);
    EXPECT_EQ(stream_id, v.stream_id);
  } while (frame_type++ == 255);

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
  EXPECT_DEBUG_DEATH(Http2FrameHeader(0x01000000, Http2FrameType::DATA, 0, 1),
                     "payload_length");
#endif  // GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
}

TEST(Http2FrameHeaderTest, Eq) {
  Http2Random random;
  uint32_t payload_length = random.Rand32() & 0xffffff;
  Http2FrameType type = static_cast<Http2FrameType>(random.Rand8());

  uint8_t flags = random.Rand8();
  uint32_t stream_id = random.Rand32();

  Http2FrameHeader v(payload_length, type, flags, stream_id);

  EXPECT_EQ(payload_length, v.payload_length);
  EXPECT_EQ(type, v.type);
  EXPECT_EQ(flags, v.flags);
  EXPECT_EQ(stream_id, v.stream_id);

  Http2FrameHeader u(0, type, ~flags, stream_id);

  EXPECT_NE(u, v);
  EXPECT_NE(v, u);
  EXPECT_FALSE(u == v);
  EXPECT_FALSE(v == u);
  EXPECT_TRUE(u != v);
  EXPECT_TRUE(v != u);

  u = v;

  EXPECT_EQ(u, v);
  EXPECT_EQ(v, u);
  EXPECT_TRUE(u == v);
  EXPECT_TRUE(v == u);
  EXPECT_FALSE(u != v);
  EXPECT_FALSE(v != u);

  EXPECT_TRUE(VerifyRandomCalls<Http2FrameHeader>());
}

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
// The tests of the valid frame types include EXPECT_DEBUG_DEATH, which is
// quite slow, so using value parameterized tests in order to allow sharding.
class Http2FrameHeaderTypeAndFlagTest
    : public ::testing::TestWithParam<
          std::tuple<Http2FrameType, Http2FrameFlag>> {
 protected:
  Http2FrameHeaderTypeAndFlagTest()
      : type_(std::get<0>(GetParam())), flags_(std::get<1>(GetParam())) {
    HTTP2_LOG(INFO) << "Frame type: " << type_;
    HTTP2_LOG(INFO) << "Frame flags: "
                    << Http2FrameFlagsToString(type_, flags_);
  }

  const Http2FrameType type_;
  const Http2FrameFlag flags_;
};

class IsEndStreamTest : public Http2FrameHeaderTypeAndFlagTest {};
INSTANTIATE_TEST_SUITE_P(IsEndStream,
                         IsEndStreamTest,
                         Combine(ValuesIn(ValidFrameTypes()),
                                 Values(~Http2FrameFlag::END_STREAM, 0xff)));
TEST_P(IsEndStreamTest, IsEndStream) {
  const bool is_set =
      (flags_ & Http2FrameFlag::END_STREAM) == Http2FrameFlag::END_STREAM;
  std::string flags_string;
  Http2FrameHeader v(0, type_, flags_, 0);
  switch (type_) {
    case Http2FrameType::DATA:
    case Http2FrameType::HEADERS:
      EXPECT_EQ(is_set, v.IsEndStream()) << v;
      flags_string = v.FlagsToString();
      if (is_set) {
        EXPECT_THAT(flags_string, MatchesRegex(".*\\|?END_STREAM\\|.*"));
      } else {
        EXPECT_THAT(flags_string, Not(HasSubstr("END_STREAM")));
      }
      v.RetainFlags(Http2FrameFlag::END_STREAM);
      EXPECT_EQ(is_set, v.IsEndStream()) << v;
      {
        std::stringstream s;
        s << v;
        EXPECT_EQ(v.ToString(), s.str());
        if (is_set) {
          EXPECT_THAT(s.str(), HasSubstr("flags=END_STREAM,"));
        } else {
          EXPECT_THAT(s.str(), HasSubstr("flags=,"));
        }
      }
      break;
    default:
      EXPECT_DEBUG_DEATH(v.IsEndStream(), "DATA.*HEADERS") << v;
  }
}

class IsACKTest : public Http2FrameHeaderTypeAndFlagTest {};
INSTANTIATE_TEST_SUITE_P(IsAck,
                         IsACKTest,
                         Combine(ValuesIn(ValidFrameTypes()),
                                 Values(~Http2FrameFlag::ACK, 0xff)));
TEST_P(IsACKTest, IsAck) {
  const bool is_set = (flags_ & Http2FrameFlag::ACK) == Http2FrameFlag::ACK;
  std::string flags_string;
  Http2FrameHeader v(0, type_, flags_, 0);
  switch (type_) {
    case Http2FrameType::SETTINGS:
    case Http2FrameType::PING:
      EXPECT_EQ(is_set, v.IsAck()) << v;
      flags_string = v.FlagsToString();
      if (is_set) {
        EXPECT_THAT(flags_string, MatchesRegex(".*\\|?ACK\\|.*"));
      } else {
        EXPECT_THAT(flags_string, Not(HasSubstr("ACK")));
      }
      v.RetainFlags(Http2FrameFlag::ACK);
      EXPECT_EQ(is_set, v.IsAck()) << v;
      {
        std::stringstream s;
        s << v;
        EXPECT_EQ(v.ToString(), s.str());
        if (is_set) {
          EXPECT_THAT(s.str(), HasSubstr("flags=ACK,"));
        } else {
          EXPECT_THAT(s.str(), HasSubstr("flags=,"));
        }
      }
      break;
    default:
      EXPECT_DEBUG_DEATH(v.IsAck(), "SETTINGS.*PING") << v;
  }
}

class IsEndHeadersTest : public Http2FrameHeaderTypeAndFlagTest {};
INSTANTIATE_TEST_SUITE_P(IsEndHeaders,
                         IsEndHeadersTest,
                         Combine(ValuesIn(ValidFrameTypes()),
                                 Values(~Http2FrameFlag::END_HEADERS, 0xff)));
TEST_P(IsEndHeadersTest, IsEndHeaders) {
  const bool is_set =
      (flags_ & Http2FrameFlag::END_HEADERS) == Http2FrameFlag::END_HEADERS;
  std::string flags_string;
  Http2FrameHeader v(0, type_, flags_, 0);
  switch (type_) {
    case Http2FrameType::HEADERS:
    case Http2FrameType::PUSH_PROMISE:
    case Http2FrameType::CONTINUATION:
      EXPECT_EQ(is_set, v.IsEndHeaders()) << v;
      flags_string = v.FlagsToString();
      if (is_set) {
        EXPECT_THAT(flags_string, MatchesRegex(".*\\|?END_HEADERS\\|.*"));
      } else {
        EXPECT_THAT(flags_string, Not(HasSubstr("END_HEADERS")));
      }
      v.RetainFlags(Http2FrameFlag::END_HEADERS);
      EXPECT_EQ(is_set, v.IsEndHeaders()) << v;
      {
        std::stringstream s;
        s << v;
        EXPECT_EQ(v.ToString(), s.str());
        if (is_set) {
          EXPECT_THAT(s.str(), HasSubstr("flags=END_HEADERS,"));
        } else {
          EXPECT_THAT(s.str(), HasSubstr("flags=,"));
        }
      }
      break;
    default:
      EXPECT_DEBUG_DEATH(v.IsEndHeaders(),
                         "HEADERS.*PUSH_PROMISE.*CONTINUATION")
          << v;
  }
}

class IsPaddedTest : public Http2FrameHeaderTypeAndFlagTest {};
INSTANTIATE_TEST_SUITE_P(IsPadded,
                         IsPaddedTest,
                         Combine(ValuesIn(ValidFrameTypes()),
                                 Values(~Http2FrameFlag::PADDED, 0xff)));
TEST_P(IsPaddedTest, IsPadded) {
  const bool is_set =
      (flags_ & Http2FrameFlag::PADDED) == Http2FrameFlag::PADDED;
  std::string flags_string;
  Http2FrameHeader v(0, type_, flags_, 0);
  switch (type_) {
    case Http2FrameType::DATA:
    case Http2FrameType::HEADERS:
    case Http2FrameType::PUSH_PROMISE:
      EXPECT_EQ(is_set, v.IsPadded()) << v;
      flags_string = v.FlagsToString();
      if (is_set) {
        EXPECT_THAT(flags_string, MatchesRegex(".*\\|?PADDED\\|.*"));
      } else {
        EXPECT_THAT(flags_string, Not(HasSubstr("PADDED")));
      }
      v.RetainFlags(Http2FrameFlag::PADDED);
      EXPECT_EQ(is_set, v.IsPadded()) << v;
      {
        std::stringstream s;
        s << v;
        EXPECT_EQ(v.ToString(), s.str());
        if (is_set) {
          EXPECT_THAT(s.str(), HasSubstr("flags=PADDED,"));
        } else {
          EXPECT_THAT(s.str(), HasSubstr("flags=,"));
        }
      }
      break;
    default:
      EXPECT_DEBUG_DEATH(v.IsPadded(), "DATA.*HEADERS.*PUSH_PROMISE") << v;
  }
}

class HasPriorityTest : public Http2FrameHeaderTypeAndFlagTest {};
INSTANTIATE_TEST_SUITE_P(HasPriority,
                         HasPriorityTest,
                         Combine(ValuesIn(ValidFrameTypes()),
                                 Values(~Http2FrameFlag::PRIORITY, 0xff)));
TEST_P(HasPriorityTest, HasPriority) {
  const bool is_set =
      (flags_ & Http2FrameFlag::PRIORITY) == Http2FrameFlag::PRIORITY;
  std::string flags_string;
  Http2FrameHeader v(0, type_, flags_, 0);
  switch (type_) {
    case Http2FrameType::HEADERS:
      EXPECT_EQ(is_set, v.HasPriority()) << v;
      flags_string = v.FlagsToString();
      if (is_set) {
        EXPECT_THAT(flags_string, MatchesRegex(".*\\|?PRIORITY\\|.*"));
      } else {
        EXPECT_THAT(flags_string, Not(HasSubstr("PRIORITY")));
      }
      v.RetainFlags(Http2FrameFlag::PRIORITY);
      EXPECT_EQ(is_set, v.HasPriority()) << v;
      {
        std::stringstream s;
        s << v;
        EXPECT_EQ(v.ToString(), s.str());
        if (is_set) {
          EXPECT_THAT(s.str(), HasSubstr("flags=PRIORITY,"));
        } else {
          EXPECT_THAT(s.str(), HasSubstr("flags=,"));
        }
      }
      break;
    default:
      EXPECT_DEBUG_DEATH(v.HasPriority(), "HEADERS") << v;
  }
}

TEST(Http2PriorityFieldsTest, Constructor) {
  Http2Random random;
  uint32_t stream_dependency = random.Rand32() & StreamIdMask();
  uint32_t weight = 1 + random.Rand8();
  bool is_exclusive = random.OneIn(2);

  Http2PriorityFields v(stream_dependency, weight, is_exclusive);

  EXPECT_EQ(stream_dependency, v.stream_dependency);
  EXPECT_EQ(weight, v.weight);
  EXPECT_EQ(is_exclusive, v.is_exclusive);

  // The high-bit must not be set on the stream id.
  EXPECT_DEBUG_DEATH(
      Http2PriorityFields(stream_dependency | 0x80000000, weight, is_exclusive),
      "31-bit");

  // The weight must be in the range 1-256.
  EXPECT_DEBUG_DEATH(Http2PriorityFields(stream_dependency, 0, is_exclusive),
                     "too small");
  EXPECT_DEBUG_DEATH(
      Http2PriorityFields(stream_dependency, weight + 256, is_exclusive),
      "too large");

  EXPECT_TRUE(VerifyRandomCalls<Http2PriorityFields>());
}
#endif  // GTEST_HAS_DEATH_TEST && !defined(NDEBUG)

TEST(Http2RstStreamFieldsTest, IsSupported) {
  Http2RstStreamFields v{Http2ErrorCode::HTTP2_NO_ERROR};
  EXPECT_TRUE(v.IsSupportedErrorCode()) << v;

  Http2RstStreamFields u{static_cast<Http2ErrorCode>(~0)};
  EXPECT_FALSE(u.IsSupportedErrorCode()) << v;

  EXPECT_TRUE(VerifyRandomCalls<Http2RstStreamFields>());
}

TEST(Http2SettingFieldsTest, Misc) {
  Http2Random random;
  Http2SettingsParameter parameter =
      static_cast<Http2SettingsParameter>(random.Rand16());
  uint32_t value = random.Rand32();

  Http2SettingFields v(parameter, value);

  EXPECT_EQ(v, v);
  EXPECT_EQ(parameter, v.parameter);
  EXPECT_EQ(value, v.value);

  if (static_cast<uint16_t>(parameter) < 7) {
    EXPECT_TRUE(v.IsSupportedParameter()) << v;
  } else {
    EXPECT_FALSE(v.IsSupportedParameter()) << v;
  }

  Http2SettingFields u(parameter, ~value);
  EXPECT_NE(v, u);
  EXPECT_EQ(v.parameter, u.parameter);
  EXPECT_NE(v.value, u.value);

  Http2SettingFields w(IncrementEnum(parameter), value);
  EXPECT_NE(v, w);
  EXPECT_NE(v.parameter, w.parameter);
  EXPECT_EQ(v.value, w.value);

  Http2SettingFields x(Http2SettingsParameter::MAX_FRAME_SIZE, 123);
  std::stringstream s;
  s << x;
  EXPECT_EQ("parameter=MAX_FRAME_SIZE, value=123", s.str());

  EXPECT_TRUE(VerifyRandomCalls<Http2SettingFields>());
}

TEST(Http2PushPromiseTest, Misc) {
  Http2Random random;
  uint32_t promised_stream_id = random.Rand32() & StreamIdMask();

  Http2PushPromiseFields v{promised_stream_id};
  EXPECT_EQ(promised_stream_id, v.promised_stream_id);
  EXPECT_EQ(v, v);

  std::stringstream s;
  s << v;
  EXPECT_EQ(Http2StrCat("promised_stream_id=", promised_stream_id), s.str());

  // High-bit is reserved, but not used, so we can set it.
  promised_stream_id |= 0x80000000;
  Http2PushPromiseFields w{promised_stream_id};
  EXPECT_EQ(w, w);
  EXPECT_NE(v, w);

  v.promised_stream_id = promised_stream_id;
  EXPECT_EQ(v, w);

  EXPECT_TRUE(VerifyRandomCalls<Http2PushPromiseFields>());
}

TEST(Http2PingFieldsTest, Misc) {
  Http2PingFields v{{'8', ' ', 'b', 'y', 't', 'e', 's', '\0'}};
  std::stringstream s;
  s << v;
  EXPECT_EQ("opaque_bytes=0x3820627974657300", s.str());

  EXPECT_TRUE(VerifyRandomCalls<Http2PingFields>());
}

TEST(Http2GoAwayFieldsTest, Misc) {
  Http2Random random;
  uint32_t last_stream_id = random.Rand32() & StreamIdMask();
  Http2ErrorCode error_code = static_cast<Http2ErrorCode>(random.Rand32());

  Http2GoAwayFields v(last_stream_id, error_code);
  EXPECT_EQ(v, v);
  EXPECT_EQ(last_stream_id, v.last_stream_id);
  EXPECT_EQ(error_code, v.error_code);

  if (static_cast<uint32_t>(error_code) < 14) {
    EXPECT_TRUE(v.IsSupportedErrorCode()) << v;
  } else {
    EXPECT_FALSE(v.IsSupportedErrorCode()) << v;
  }

  Http2GoAwayFields u(~last_stream_id, error_code);
  EXPECT_NE(v, u);
  EXPECT_NE(v.last_stream_id, u.last_stream_id);
  EXPECT_EQ(v.error_code, u.error_code);

  EXPECT_TRUE(VerifyRandomCalls<Http2GoAwayFields>());
}

TEST(Http2WindowUpdateTest, Misc) {
  Http2Random random;
  uint32_t window_size_increment = random.Rand32() & UInt31Mask();

  Http2WindowUpdateFields v{window_size_increment};
  EXPECT_EQ(window_size_increment, v.window_size_increment);
  EXPECT_EQ(v, v);

  std::stringstream s;
  s << v;
  EXPECT_EQ(Http2StrCat("window_size_increment=", window_size_increment),
            s.str());

  // High-bit is reserved, but not used, so we can set it.
  window_size_increment |= 0x80000000;
  Http2WindowUpdateFields w{window_size_increment};
  EXPECT_EQ(w, w);
  EXPECT_NE(v, w);

  v.window_size_increment = window_size_increment;
  EXPECT_EQ(v, w);

  EXPECT_TRUE(VerifyRandomCalls<Http2WindowUpdateFields>());
}

TEST(Http2AltSvcTest, Misc) {
  Http2Random random;
  uint16_t origin_length = random.Rand16();

  Http2AltSvcFields v{origin_length};
  EXPECT_EQ(origin_length, v.origin_length);
  EXPECT_EQ(v, v);

  std::stringstream s;
  s << v;
  EXPECT_EQ(Http2StrCat("origin_length=", origin_length), s.str());

  Http2AltSvcFields w{++origin_length};
  EXPECT_EQ(w, w);
  EXPECT_NE(v, w);

  v.origin_length = w.origin_length;
  EXPECT_EQ(v, w);

  EXPECT_TRUE(VerifyRandomCalls<Http2AltSvcFields>());
}

}  // namespace
}  // namespace test
}  // namespace http2
