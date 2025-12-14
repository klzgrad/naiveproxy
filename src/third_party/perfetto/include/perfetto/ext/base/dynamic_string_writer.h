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

  void AppendUnsignedInt(uint64_t value) {
    constexpr size_t STACK_BUFFER_SIZE = 32;
    StackString<STACK_BUFFER_SIZE> buf("%" PRIu64, value);
    AppendString(buf.string_view());
  }

  // Appends a hex integer to the buffer.
  template <typename IntType>
  void AppendHexInt(IntType value) {
    constexpr size_t STACK_BUFFER_SIZE = 64;
    StackString<STACK_BUFFER_SIZE> buf("%" PRIx64, value);
    AppendString(buf.string_view());
  }

  // Appends a double to the buffer.
  void AppendDouble(double value) {
    constexpr size_t STACK_BUFFER_SIZE = 32;
    StackString<STACK_BUFFER_SIZE> buf("%lf", value);
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

  void Clear() { buffer_.clear(); }

 private:
  std::string buffer_;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_DYNAMIC_STRING_WRITER_H_
