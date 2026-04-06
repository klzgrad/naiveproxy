
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

#include "src/trace_processor/importers/proto/string_encoding_utils.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace {

using CodePoint = uint32_t;

struct Utf8 {
  static constexpr uint8_t kContinuationHeader = 0x80;
  static constexpr uint8_t kContinuationValueMask = 0x3F;
  static constexpr uint8_t kContinuationBits = 6;

  static constexpr uint8_t k2ByteHeader = 0xC0;
  static constexpr uint8_t k2ByteValueMask = 0x1F;

  static constexpr uint8_t k3ByteHeader = 0xE0;
  static constexpr uint8_t k3ByteValueMask = 0x0F;

  static constexpr uint8_t k4ByteHeader = 0xF0;
  static constexpr uint8_t k4ByteValueMask = 0x07;

  static constexpr CodePoint k1ByteMaxCodepoint = 0x7F;
  static constexpr CodePoint k2ByteMaxCodepoint = 0x7FF;
  static constexpr CodePoint k3ByteMaxCodepoint = 0xFFFF;
  static constexpr CodePoint k4ByteMaxCodepoint = 0x10FFFF;

  static void Append(CodePoint code_point, std::string& out) {
    if (code_point <= k1ByteMaxCodepoint) {
      out.push_back(static_cast<char>(code_point));
      return;
    }

    if (code_point <= k2ByteMaxCodepoint) {
      uint8_t byte_2 =
          kContinuationHeader + (code_point & kContinuationValueMask);
      code_point >>= kContinuationBits;
      uint8_t byte_1 = k2ByteHeader + (code_point & k2ByteValueMask);
      out.push_back(static_cast<char>(byte_1));
      out.push_back(static_cast<char>(byte_2));
      return;
    }

    if (code_point <= k3ByteMaxCodepoint) {
      uint8_t byte_3 =
          kContinuationHeader + (code_point & kContinuationValueMask);
      code_point >>= kContinuationBits;
      uint8_t byte_2 =
          kContinuationHeader + (code_point & kContinuationValueMask);
      code_point >>= kContinuationBits;
      uint8_t byte_1 = k3ByteHeader + (code_point & k3ByteValueMask);
      out.push_back(static_cast<char>(byte_1));
      out.push_back(static_cast<char>(byte_2));
      out.push_back(static_cast<char>(byte_3));
      return;
    }

    if (code_point <= k4ByteMaxCodepoint) {
      uint8_t byte_4 =
          kContinuationHeader + (code_point & kContinuationValueMask);
      code_point >>= kContinuationBits;
      uint8_t byte_3 =
          kContinuationHeader + (code_point & kContinuationValueMask);
      code_point >>= kContinuationBits;
      uint8_t byte_2 =
          kContinuationHeader + (code_point & kContinuationValueMask);
      code_point >>= kContinuationBits;
      uint8_t byte_1 = k4ByteHeader + (code_point & k4ByteValueMask);
      out.push_back(static_cast<char>(byte_1));
      out.push_back(static_cast<char>(byte_2));
      out.push_back(static_cast<char>(byte_3));
      out.push_back(static_cast<char>(byte_4));
      return;
    }

    PERFETTO_FATAL("Invalid code point for UTF8 conversion: %" PRIu32,
                   code_point);
  }
};

enum class Endianness {
  kBigEndian,
  kLittleEndian,
};

template <Endianness endianness>
class Utf16Iterator {
 public:
  using Utf16CodeUnit = uint16_t;

  static constexpr Utf16CodeUnit kSurrogateMask = 0xFC00;
  static constexpr Utf16CodeUnit kHighSurrogate = 0xD800;
  static constexpr Utf16CodeUnit kLowSurrogate = 0xDC00;

  static constexpr CodePoint kSurrogateCodepointOffset = 0x10000;
  static constexpr uint32_t kSurrogateCodepointBits = 10;
  static constexpr CodePoint kSurrogateCodepointMask =
      (1u << kSurrogateCodepointBits) - 1;

  CodePoint kInvalidCodePoint = 0xFFFD;

  explicit Utf16Iterator(protozero::ConstBytes bytes)
      : current_(reinterpret_cast<const uint8_t*>(bytes.data)),
        end_(reinterpret_cast<const uint8_t*>(bytes.data + bytes.size)) {}

  bool HasMore() const { return current_ != end_; }

  CodePoint NextCodePoint() {
    std::optional<Utf16CodeUnit> maybe_surrogate = NextCodeUnit();
    if (!maybe_surrogate) {
      return kInvalidCodePoint;
    }

    if (PERFETTO_UNLIKELY(IsLowSurrogate(*maybe_surrogate))) {
      return kInvalidCodePoint;
    }

    if (PERFETTO_LIKELY(!IsHighSurrogate(*maybe_surrogate))) {
      return *maybe_surrogate;
    }

    Utf16CodeUnit high = *maybe_surrogate;

    maybe_surrogate = NextCodeUnit();
    if (!maybe_surrogate) {
      return kInvalidCodePoint;
    }

    if (PERFETTO_UNLIKELY(!IsLowSurrogate(*maybe_surrogate))) {
      return kInvalidCodePoint;
    }

    Utf16CodeUnit low = *maybe_surrogate;

    CodePoint code_point = (high & kSurrogateCodepointMask);
    code_point <<= kSurrogateCodepointBits;
    code_point += (low & kSurrogateCodepointMask);
    code_point += kSurrogateCodepointOffset;

    return code_point;
  }

 private:
  std::optional<Utf16CodeUnit> NextCodeUnit() {
    if (current_ == end_) {
      return std::nullopt;
    }
    uint16_t byte_0 = static_cast<uint16_t>(*current_);
    ++current_;

    if (current_ == end_) {
      return std::nullopt;
    }
    uint16_t byte_1 = static_cast<uint16_t>(*current_);
    ++current_;

    if (endianness == Endianness::kBigEndian) {
      return (byte_0 << 8) + byte_1;
    }

    return byte_0 + (byte_1 << 8);
  }

  static bool IsLowSurrogate(Utf16CodeUnit code_unit) {
    return (code_unit & kSurrogateMask) == kLowSurrogate;
  }

  static bool IsHighSurrogate(Utf16CodeUnit code_unit) {
    return (code_unit & kSurrogateMask) == kHighSurrogate;
  }

  const uint8_t* current_;
  const uint8_t* const end_;
};

using Utf16LeIterator = Utf16Iterator<Endianness::kLittleEndian>;
using Utf16BeIterator = Utf16Iterator<Endianness::kBigEndian>;

}  // namespace

std::string ConvertLatin1ToUtf8(protozero::ConstBytes latin1) {
  size_t res_size = latin1.size;
  for (size_t i = 0; i < latin1.size; ++i) {
    CodePoint code_point = latin1.data[i];
    if (code_point > Utf8::k1ByteMaxCodepoint) {
      ++res_size;
    }
  }

  std::string res;
  res.reserve(res_size);
  for (size_t i = 0; i < latin1.size; ++i) {
    CodePoint code_point = latin1.data[i];
    Utf8::Append(code_point, res);
  }
  return res;
}

std::string ConvertUtf16LeToUtf8(protozero::ConstBytes utf16_le) {
  std::string res;
  for (Utf16LeIterator iter(utf16_le); iter.HasMore();) {
    Utf8::Append(iter.NextCodePoint(), res);
  }
  return res;
}

std::string ConvertUtf16BeToUtf8(protozero::ConstBytes utf16_le) {
  std::string res;
  for (Utf16BeIterator iter(utf16_le); iter.HasMore();) {
    Utf8::Append(iter.NextCodePoint(), res);
  }
  return res;
}

}  // namespace trace_processor
}  // namespace perfetto
