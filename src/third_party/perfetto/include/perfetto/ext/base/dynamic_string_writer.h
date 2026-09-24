/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_DYNAMIC_STRING_WRITER_H_
#define INCLUDE_PERFETTO_EXT_BASE_DYNAMIC_STRING_WRITER_H_

#include <string.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto {
namespace base {

// A helper class which writes formatted data to a string buffer.
// This is used in the trace processor where we write O(GBs) of strings and
// sprintf is too slow.
class DynamicStringWriter {
 public:
  using ScopedCString = std::unique_ptr<char, void (*)(void*)>;

  // Creates a string buffer from a char buffer and length.
  DynamicStringWriter() {}

  // Appends n instances of a char to the buffer.
  void AppendChar(char in, size_t n = 1) { buffer_.append(n, in); }

  // Appends a length delimited string to the buffer.
  void AppendString(const char* in, size_t n) { buffer_.append(in, n); }

  void AppendStringView(StringView sv) { AppendString(sv.data(), sv.size()); }

  // Appends a null-terminated string literal to the buffer.
  template <size_t N>
  inline void AppendLiteral(const char (&in)[N]) {
    AppendString(in, N - 1);
  }

  // Appends a StringView to the buffer.
  void AppendString(StringView data) {
    buffer_.append(data.data(), data.size());
  }

  // Appends an integer to the buffer.
  void AppendInt(int64_t value) {
    constexpr size_t STACK_BUFFER_SIZE = 32;
    StackString<STACK_BUFFER_SIZE> buf("%" PRId64, value);
    AppendString(buf.string_view());
  }

  // Appends an integer to the buffer, padding with |padchar| if the number of
  // digits of the integer is less than |padding|.
  template <char padchar, uint64_t padding>
  void AppendPaddedInt(int64_t sign_value) {
    const bool negate = std::signbit(static_cast<double>(sign_value));
    uint64_t absolute_value;
    if (sign_value == std::numeric_limits<int64_t>::min()) {
      absolute_value =
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
    } else {
      absolute_value = static_cast<uint64_t>(std::abs(sign_value));
    }
    AppendPaddedIntImpl<padchar, padding>(absolute_value, negate);
  }

  void AppendUnsignedInt(uint64_t value) {
    constexpr size_t STACK_BUFFER_SIZE = 32;
    StackString<STACK_BUFFER_SIZE> buf("%" PRIu64, value);
    AppendString(buf.string_view());
  }

  template <char padchar, uint64_t padding>
  void AppendPaddedUnsignedInt(uint64_t value) {
    AppendPaddedIntImpl<padchar, padding>(value, false);
  }

  template <typename IntType>
  void AppendPaddedHexInt(IntType value, char padchar, uint64_t padding) {
    using UnsignedType = std::make_unsigned_t<IntType>;
    constexpr size_t kMaxHexDigits = sizeof(IntType) * 2;
    constexpr size_t kBufferSize = 32;
    auto size_needed =
        kMaxHexDigits > padding ? kMaxHexDigits : static_cast<size_t>(padding);
    PERFETTO_DCHECK(size_needed <= kBufferSize);

    std::array<char, kBufferSize> data;
    constexpr char hex_asc[] = "0123456789abcdef";

    size_t idx = size_needed - 1;
    auto uvalue = static_cast<UnsignedType>(value);
    do {
      data[idx--] = hex_asc[uvalue & 0xF];
      uvalue >>= 4;
    } while (uvalue != 0);

    if (padding > 0) {
      const auto num_digits = static_cast<uint64_t>(size_needed - 1 - idx);
      // std::max() needed to work around GCC not being able to tell that
      // padding > 0.
      for (auto i = num_digits; i < std::max(uint64_t{1u}, padding); i++) {
        data[idx--] = padchar;
      }
    }
    AppendString(&data[idx + 1], size_needed - idx - 1);
  }

  // Appends a hex integer to the buffer.
  template <typename IntType>
  void AppendHexInt(IntType value) {
    constexpr size_t STACK_BUFFER_SIZE = 64;
    StackString<STACK_BUFFER_SIZE> buf("%" PRIx64, value);
    AppendString(buf.string_view());
  }

  void AppendHexString(const uint8_t* data, size_t size, char separator);

  void AppendHexString(StringView data, char separator) {
    AppendHexString(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                    separator);
  }

  // Appends a double to the buffer.
  void AppendDouble(double value) {
    constexpr size_t STACK_BUFFER_SIZE = 32;
    StackString<STACK_BUFFER_SIZE> buf("%.16g", value);
    AppendString(buf.string_view());
  }

  void AppendBool(bool value) {
    if (value) {
      AppendLiteral("true");
      return;
    }
    AppendLiteral("false");
  }

  StringView GetStringView() {
    return StringView(buffer_.c_str(), buffer_.size());
  }

  ScopedCString CreateStringCopy() const {
    size_t n = buffer_.size();
    char* dup = reinterpret_cast<char*>(malloc(n + 1));
    if (dup) {
      memcpy(dup, buffer_.data(), n);
      dup[n] = '\0';
    }
    return {dup, free};
  }

  size_t pos() const { return buffer_.size(); }

  void Clear() { buffer_.clear(); }

 private:
  template <char padchar, uint64_t padding>
  void AppendPaddedIntImpl(uint64_t absolute_value, bool negate) {
    // Need to add 2 to the number of digits to account for minus sign and
    // rounding down of digits10.
    constexpr auto kMaxDigits = std::numeric_limits<uint64_t>::digits10 + 2;
    constexpr auto kSizeNeeded = kMaxDigits > padding ? kMaxDigits : padding;

    char data[kSizeNeeded];

    size_t idx;
    for (idx = kSizeNeeded - 1; absolute_value >= 10;) {
      char digit = absolute_value % 10;
      absolute_value /= 10;
      data[idx--] = digit + '0';
    }
    data[idx--] = static_cast<char>(absolute_value) + '0';

    if (padding > 0) {
      size_t num_digits = kSizeNeeded - 1 - idx;
      // std::max() needed to work around GCC not being able to tell that
      // padding > 0.
      for (size_t i = num_digits; i < std::max(uint64_t{1u}, padding); i++) {
        data[idx--] = padchar;
      }
    }

    if (negate)
      AppendChar('-');
    AppendString(&data[idx + 1], kSizeNeeded - idx - 1);
  }

  std::string buffer_;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_DYNAMIC_STRING_WRITER_H_
