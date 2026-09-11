
/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/proto/string_encoding_utils.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "perfetto/ext/base/string_view.h"
#include "test/gtest_and_gmock.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::protozero::ConstBytes;
using ::testing::Eq;
using ::testing::SizeIs;

TEST(ConvertLatin1ToUtf8, FullCodePage) {
  std::vector<uint8_t> latin1;
  latin1.reserve(256 / 5);
  for (uint16_t i = 0; i <= std::numeric_limits<uint8_t>::max(); i += 5) {
    latin1.push_back(static_cast<uint8_t>(i));
  }

  std::string uft8 = ConvertLatin1ToUtf8({latin1.data(), latin1.size()});

  //  Obtained via:
  //  for i in $(seq 0 5 255); do printf '\\\\x%x' $i ; done | xargs echo -en |
  //     iconv -f latin1 -t utf8| hexdump -e '1/1 "0x%02x,\n"'
  const uint8_t kExpected[] = {
      0x00, 0x05, 0x0a, 0x0f, 0x14, 0x19, 0x1e, 0x23, 0x28, 0x2d, 0x32, 0x37,
      0x3c, 0x41, 0x46, 0x4b, 0x50, 0x55, 0x5a, 0x5f, 0x64, 0x69, 0x6e, 0x73,
      0x78, 0x7d, 0xc2, 0x82, 0xc2, 0x87, 0xc2, 0x8c, 0xc2, 0x91, 0xc2, 0x96,
      0xc2, 0x9b, 0xc2, 0xa0, 0xc2, 0xa5, 0xc2, 0xaa, 0xc2, 0xaf, 0xc2, 0xb4,
      0xc2, 0xb9, 0xc2, 0xbe, 0xc3, 0x83, 0xc3, 0x88, 0xc3, 0x8d, 0xc3, 0x92,
      0xc3, 0x97, 0xc3, 0x9c, 0xc3, 0xa1, 0xc3, 0xa6, 0xc3, 0xab, 0xc3, 0xb0,
      0xc3, 0xb5, 0xc3, 0xba, 0xc3, 0xbf};

  EXPECT_THAT(uft8, Eq(std::string(reinterpret_cast<const char*>(kExpected),
                                   sizeof(kExpected))));
}

// The following strings are different encodings of the following code points:
//     \u0000, \u0001, \u0002, \u0005, \u000A, \u0015, \u002A, \u0055, \u00AA,
//     \u0155, \u02AA, \u0555, \u0AAA, \u1555, \u2AAA, \u5555, \uAAAA,
//     \U00015555, \U0002AAAA, \U00055555, \U000AAAAA, \U0010AAAA
// This gives a reasonable coverage of the entire code point range so that we
// force all types of encoding, ie utf8: 1-4 bytes, utf16: with and without
// surrogate pairs
const uint8_t kUtf16Le[] = {
    0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x05, 0x00, 0x0a, 0x00, 0x15,
    0x00, 0x2a, 0x00, 0x55, 0x00, 0xaa, 0x00, 0x55, 0x01, 0xaa, 0x02,
    0x55, 0x05, 0xaa, 0x0a, 0x55, 0x15, 0xaa, 0x2a, 0x55, 0x55, 0xaa,
    0xaa, 0x15, 0xd8, 0x55, 0xdd, 0x6a, 0xd8, 0xaa, 0xde, 0x15, 0xd9,
    0x55, 0xdd, 0x6a, 0xda, 0xaa, 0xde, 0xea, 0xdb, 0xaa, 0xde};

const uint8_t kUtf16Be[] = {
    0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x05, 0x00, 0x0a, 0x00,
    0x15, 0x00, 0x2a, 0x00, 0x55, 0x00, 0xaa, 0x01, 0x55, 0x02, 0xaa,
    0x05, 0x55, 0x0a, 0xaa, 0x15, 0x55, 0x2a, 0xaa, 0x55, 0x55, 0xaa,
    0xaa, 0xd8, 0x15, 0xdd, 0x55, 0xd8, 0x6a, 0xde, 0xaa, 0xd9, 0x15,
    0xdd, 0x55, 0xda, 0x6a, 0xde, 0xaa, 0xdb, 0xea, 0xde, 0xaa};

const uint8_t kExpectedUtf8[] = {
    0x00, 0x01, 0x02, 0x05, 0x0a, 0x15, 0x2a, 0x55, 0xc2, 0xaa, 0xc5,
    0x95, 0xca, 0xaa, 0xd5, 0x95, 0xe0, 0xaa, 0xaa, 0xe1, 0x95, 0x95,
    0xe2, 0xaa, 0xaa, 0xe5, 0x95, 0x95, 0xea, 0xaa, 0xaa, 0xf0, 0x95,
    0x95, 0x95, 0xf0, 0xaa, 0xaa, 0xaa, 0xf1, 0x95, 0x95, 0x95, 0xf2,
    0xaa, 0xaa, 0xaa, 0xf4, 0x8a, 0xaa, 0xaa};

// Collection of invalid bytes: High surrogate followed by non low surrogate,
// low surrogate, 1 random byte (not enough to read one code unit which is 2
// bytes)
const uint8_t kInvalidUtf16Le[] = {0xea, 0xdb, 0x00, 0x00, 0xaa, 0xde, 0x00};
const uint8_t kInvalidUtf16Be[] = {0xdb, 0xea, 0x00, 0x00, 0xde, 0xaa, 0x00};

// We expect 3 invalid char code points.
const uint8_t kExpectedUtf8ForInvalidUtf16[] = {
    0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
};

TEST(ConvertUtf16LeToUtf8, ValidInput) {
  std::string utf8 = ConvertUtf16LeToUtf8({kUtf16Le, sizeof(kUtf16Le)});
  EXPECT_THAT(utf8, Eq(std::string(reinterpret_cast<const char*>(kExpectedUtf8),
                                   sizeof(kExpectedUtf8))));
}

TEST(ConvertUtf16BeToUtf8, ValidInput) {
  std::string utf8 = ConvertUtf16BeToUtf8({kUtf16Be, sizeof(kUtf16Be)});
  EXPECT_THAT(utf8, Eq(std::string(reinterpret_cast<const char*>(kExpectedUtf8),
                                   sizeof(kExpectedUtf8))));
}

TEST(ConvertUtf16LeToUtf8, InvalidValidInput) {
  std::string utf8 =
      ConvertUtf16LeToUtf8({kInvalidUtf16Le, sizeof(kInvalidUtf16Le)});
  EXPECT_THAT(utf8, Eq(std::string(reinterpret_cast<const char*>(
                                       kExpectedUtf8ForInvalidUtf16),
                                   sizeof(kExpectedUtf8ForInvalidUtf16))));
}

TEST(ConvertUtf16BeToUtf8, InvalidValidInput) {
  std::string utf8 =
      ConvertUtf16BeToUtf8({kInvalidUtf16Be, sizeof(kInvalidUtf16Be)});
  EXPECT_THAT(utf8, Eq(std::string(reinterpret_cast<const char*>(
                                       kExpectedUtf8ForInvalidUtf16),
                                   sizeof(kExpectedUtf8ForInvalidUtf16))));
}

}  // namespace

}  // namespace trace_processor
}  // namespace perfetto
