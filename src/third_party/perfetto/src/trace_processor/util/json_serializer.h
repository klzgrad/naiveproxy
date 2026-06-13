/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_JSON_SERIALIZER_H_
#define SRC_TRACE_PROCESSOR_UTIL_JSON_SERIALIZER_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "perfetto/ext/base/dynamic_string_writer.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto::trace_processor::json {

// Low-level JSON serializer with state tracking.
// Handles comma insertion, nesting, and optional pretty-printing.
//
// For a higher-level callback-based API, see simple_json_serializer.h.
//
// Usage:
//   JsonSerializer s;
//   s.OpenObject();
//   s.Key("name");
//   s.StringValue("hello");
//   s.Key("values");
//   s.OpenArray();
//   s.NumberValue(1);
//   s.NumberValue(2);
//   s.CloseArray();
//   s.CloseObject();
//   std::string json = s.ToString();
class JsonSerializer {
 public:
  enum Flags {
    kNone = 0,
    kPretty = 1 << 0,
  };

  explicit JsonSerializer(int flags = kNone) : flags_(flags) {}

  void OpenObject() {
    if (is_array_scope()) {
      if (!is_empty_scope()) {
        writer_.AppendChar(',');
      }
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    writer_.AppendChar('{');
    stack_.push_back(Scope{ScopeContext::kObject});
  }

  void CloseObject() {
    bool needs_newline = !is_empty_scope();
    stack_.pop_back();
    if (needs_newline) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    MarkScopeAsNonEmpty();
    writer_.AppendChar('}');
  }

  void OpenArray() {
    if (is_array_scope()) {
      if (!is_empty_scope()) {
        writer_.AppendChar(',');
      }
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    writer_.AppendChar('[');
    stack_.push_back(Scope{ScopeContext::kArray});
  }

  void CloseArray() {
    bool needs_newline = !is_empty_scope();
    stack_.pop_back();
    if (needs_newline) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    writer_.AppendChar(']');
    MarkScopeAsNonEmpty();
  }

  void Key(std::string_view key) {
    if (is_object_scope() && !is_empty_scope()) {
      writer_.AppendChar(',');
    }
    MaybeAppendNewline();
    MaybeAppendIndent();
    AppendEscapedString(key);
    writer_.AppendChar(':');
    MaybeAppendSpace();
    MarkScopeAsNonEmpty();
  }

  template <typename T>
  void NumberValue(T v) {
    if (is_array_scope() && !is_empty_scope()) {
      writer_.AppendChar(',');
    }
    if (is_array_scope()) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    if constexpr (std::is_floating_point_v<T>) {
      writer_.AppendDouble(static_cast<double>(v));
    } else if constexpr (std::is_signed_v<T>) {
      writer_.AppendInt(static_cast<int64_t>(v));
    } else {
      writer_.AppendUnsignedInt(static_cast<uint64_t>(v));
    }
    MarkScopeAsNonEmpty();
  }

  void BoolValue(bool v) {
    if (is_array_scope() && !is_empty_scope()) {
      writer_.AppendChar(',');
    }
    if (is_array_scope()) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    writer_.AppendBool(v);
    MarkScopeAsNonEmpty();
  }

  void FloatValue(float v) { DoubleValue(static_cast<double>(v)); }

  void DoubleValue(double v) {
    if (is_array_scope() && !is_empty_scope()) {
      writer_.AppendChar(',');
    }
    if (is_array_scope()) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    // Handle special values that JSON doesn't support - output as strings
    if (std::isnan(v)) {
      writer_.AppendLiteral("\"NaN\"");
    } else if (std::isinf(v)) {
      if (v > 0) {
        writer_.AppendLiteral("\"Infinity\"");
      } else {
        writer_.AppendLiteral("\"-Infinity\"");
      }
    } else {
      writer_.AppendDouble(v);
    }
    MarkScopeAsNonEmpty();
  }

  void StringValue(std::string_view v) {
    if (is_array_scope() && !is_empty_scope()) {
      writer_.AppendChar(',');
    }
    if (is_array_scope()) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    AppendEscapedString(v);
    MarkScopeAsNonEmpty();
  }

  void NullValue() {
    if (is_array_scope() && !is_empty_scope()) {
      writer_.AppendChar(',');
    }
    if (is_array_scope()) {
      MaybeAppendNewline();
      MaybeAppendIndent();
    }
    writer_.AppendLiteral("null");
    MarkScopeAsNonEmpty();
  }

  std::string ToString() const {
    auto sv = writer_.GetStringView();
    return std::string(sv.data(), sv.size());
  }

  base::StringView GetStringView() const { return writer_.GetStringView(); }

  // Clears internal state for reuse, preserving allocated memory.
  void Clear() {
    writer_.Clear();
    stack_.clear();
  }

  bool is_empty_scope() const {
    return !stack_.empty() && stack_.back().is_empty;
  }

  bool is_pretty() const { return flags_ & Flags::kPretty; }

 private:
  enum class ScopeContext {
    kObject,
    kArray,
  };

  struct Scope {
    ScopeContext ctx;
    bool is_empty = true;
  };

  bool is_object_scope() const {
    return !stack_.empty() && stack_.back().ctx == ScopeContext::kObject;
  }

  bool is_array_scope() const {
    return !stack_.empty() && stack_.back().ctx == ScopeContext::kArray;
  }

  void MarkScopeAsNonEmpty() {
    if (!stack_.empty()) {
      stack_.back().is_empty = false;
    }
  }

  void MaybeAppendSpace() {
    if (is_pretty()) {
      writer_.AppendChar(' ');
    }
  }

  void MaybeAppendIndent() {
    if (is_pretty()) {
      writer_.AppendChar(' ', stack_.size() * 2);
    }
  }

  void MaybeAppendNewline() {
    if (is_pretty()) {
      writer_.AppendChar('\n');
    }
  }

  // Escapes a string for JSON output and appends it directly to the writer.
  // Includes the surrounding quotes and proper UTF-8 handling.
  void AppendEscapedString(std::string_view raw) {
    static const char hex_chars[] = "0123456789abcdef";
    writer_.AppendChar('"');
    for (size_t i = 0; i < raw.size(); ++i) {
      char c = raw[i];
      switch (c) {
        case '"':
        case '\\':
          writer_.AppendChar('\\');
          writer_.AppendChar(c);
          break;
        case '\n':
          writer_.AppendLiteral("\\n");
          break;
        case '\b':
          writer_.AppendLiteral("\\b");
          break;
        case '\f':
          writer_.AppendLiteral("\\f");
          break;
        case '\r':
          writer_.AppendLiteral("\\r");
          break;
        case '\t':
          writer_.AppendLiteral("\\t");
          break;
        default:
          // ASCII characters between 0x20 (space) and 0x7e (tilde) are
          // inserted directly. All others are escaped.
          if (c >= 0x20 && c <= 0x7e) {
            writer_.AppendChar(c);
          } else {
            unsigned char uc = static_cast<unsigned char>(c);
            uint32_t codepoint = 0;

            // Compute the number of bytes in this UTF-8 sequence.
            size_t extra = 1 + (uc >= 0xc0u) + (uc >= 0xe0u) + (uc >= 0xf0u);

            // Consume up to |extra| bytes but don't read out of bounds.
            size_t stop = std::min(raw.size(), i + extra);

            // Extract bits from the first byte.
            codepoint |= uc & (0xff >> (extra + 1));

            // Extract remaining bits from continuation bytes.
            for (size_t j = i + 1; j < stop; ++j) {
              uc = static_cast<unsigned char>(raw[j]);
              codepoint = (codepoint << 6) | (uc & 0x3f);
            }

            // Update i to account for consumed bytes.
            i = stop - 1;

            // JSON uses UTF-16 escapes. For BMP codepoints use \uXXXX.
            // For supplementary codepoints use a surrogate pair.
            if (codepoint <= 0xffff) {
              writer_.AppendLiteral("\\u");
              writer_.AppendChar(hex_chars[(codepoint >> 12) & 0xf]);
              writer_.AppendChar(hex_chars[(codepoint >> 8) & 0xf]);
              writer_.AppendChar(hex_chars[(codepoint >> 4) & 0xf]);
              writer_.AppendChar(hex_chars[(codepoint >> 0) & 0xf]);
            } else {
              uint32_t high = ((codepoint - 0x10000) >> 10) + 0xD800;
              uint32_t low = (codepoint & 0x3ff) + 0xDC00;
              writer_.AppendLiteral("\\u");
              writer_.AppendChar(hex_chars[(high >> 12) & 0xf]);
              writer_.AppendChar(hex_chars[(high >> 8) & 0xf]);
              writer_.AppendChar(hex_chars[(high >> 4) & 0xf]);
              writer_.AppendChar(hex_chars[(high >> 0) & 0xf]);
              writer_.AppendLiteral("\\u");
              writer_.AppendChar(hex_chars[(low >> 12) & 0xf]);
              writer_.AppendChar(hex_chars[(low >> 8) & 0xf]);
              writer_.AppendChar(hex_chars[(low >> 4) & 0xf]);
              writer_.AppendChar(hex_chars[(low >> 0) & 0xf]);
            }
          }
          break;
      }
    }
    writer_.AppendChar('"');
  }

  int flags_;
  mutable base::DynamicStringWriter writer_;
  std::vector<Scope> stack_;
};

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_JSON_SERIALIZER_H_
