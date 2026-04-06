/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_UTIL_JSON_PARSER_H_
#define SRC_TRACE_PROCESSOR_UTIL_JSON_PARSER_H_

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"

namespace perfetto::trace_processor::json {

// Represents a JSON null value.
struct Null {};
// Represents a JSON object, holding its raw string content.
struct Object {
  std::string_view contents;
};
// Represents a JSON array, holding its raw string content.
struct Array {
  std::string_view contents;
};
// A variant type representing any valid JSON value.
using JsonValue =
    std::variant<Null, bool, int64_t, double, std::string_view, Object, Array>;

namespace internal {

// Internal return codes for parsing functions.
enum class ReturnCode : uint8_t {
  kOk,
  kError,
  kIncompleteInput,
};

// Advances the |cur| pointer past any JSON whitespace characters.
// Returns false if |end| is reached before any non-whitespace character.
inline bool SkipWhitespace(const char*& cur, const char* end) {
  while (cur != end &&
         (*cur == ' ' || *cur == '\t' || *cur == '\n' || *cur == '\r')) {
    ++cur;
  }
  return cur != end;
}

// Processes escape sequences within a string segment and appends the unescaped
// result to |res|.
// |start| and |end| define the string segment (excluding initial/final quotes).
// Sets |status| on error.
inline ReturnCode UnescapeString(const char* start,
                                 const char* end,
                                 std::string& res,
                                 base::Status& status) {
  PERFETTO_DCHECK(start != end);
  // Pre-allocate string capacity, assuming most characters are not escaped.
  res.reserve(static_cast<size_t>(end - start));
  for (const char* it = start; it != end; ++it) {
    if (*it == '\\') {
      ++it;
      PERFETTO_DCHECK(it != end);
      switch (*it) {
        case '"':
          res += '"';
          break;
        case '\\':
          res += '\\';
          break;
        case '/':
          res += '/';
          break;
        case 'b':
          res += '\b';
          break;
        case 'f':
          res += '\f';
          break;
        case 'n':
          res += '\n';
          break;
        case 'r':
          res += '\r';
          break;
        case 't':
          res += '\t';
          break;
        case 'u': {
          // Ensure 4 hex digits follow.
          PERFETTO_DCHECK(it + 4 != end);
          uint32_t cp = 0;
          // Parse the 4 hex digits into a code point.
          for (int j = 0; j < 4; ++j) {
            char hex = *++it;
            cp <<= 4;
            if (hex >= '0' && hex <= '9') {
              cp += static_cast<uint32_t>(hex - '0');
            } else if (hex >= 'a' && hex <= 'f') {
              cp += static_cast<uint32_t>(hex - 'a' + 10);
            } else if (hex >= 'A' && hex <= 'F') {
              cp += static_cast<uint32_t>(hex - 'A' + 10);
            } else {
              status = base::ErrStatus("Invalid escape sequence: \\u%c%c%c%c",
                                       it[-3], it[-2], it[-1], hex);
              return ReturnCode::kError;
            }
          }
          // Encode the code point as UTF-8.
          if (cp <= 0x7F) {
            // 1-byte sequence
            res += static_cast<char>(cp);
          } else if (cp <= 0x7FF) {
            // 2-byte sequence
            res += static_cast<char>(0xC0 | (cp >> 6));
            res += static_cast<char>(0x80 | (cp & 0x3F));
          } else if (cp <= 0xFFFF) {
            // 3-byte sequence
            // Check for surrogate pairs, which are not supported directly.
            if (cp >= 0xD800 && cp <= 0xDFFF) {
              status = base::ErrStatus(
                  "Invalid escape sequence: \\u%c%c%c%c (code point %u is "
                  "reserved for surrogate pairs)",
                  it[-3], it[-2], it[-1], *it, cp);
              return ReturnCode::kError;
            }
            res += static_cast<char>(0xE0 | (cp >> 12));
            res += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            res += static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            // Code points > 0xFFFF are not supported by \uXXXX in JSON
            // (they require surrogate pairs).
            status = base::ErrStatus(
                "Invalid escape sequence: \\u%c%c%c%c (code point %u > 0xFFFF)",
                it[-3], it[-2], it[-1], *it, cp);
            return ReturnCode::kError;
          }
          break;
        }
        default:
          // As per JSON spec, other escaped characters are themselves.
          // However, strict parsers might error here. This one is lenient.
          // res += *it; // This line was effectively a no-op as it was inside
          // `default: break;`
          break;
      }
    } else {
      res += *it;
    }
  }
  return ReturnCode::kOk;
}

// Scans a JSON string from |start| to |end|, updating |out| to point after the
// closing quote.
// |str| will view the content of the string (without quotes).
// |has_escapes| is set if escape sequences are present.
// Sets |err| on parsing errors.
inline ReturnCode ScanString(const char* start,
                             const char* end,
                             const char*& out,
                             std::string_view& str,
                             bool& has_escapes,
                             base::Status& err) {
  const char* cur = start;
  PERFETTO_DCHECK(cur != end);
  // Expect a string to start with a double quote.
  if (PERFETTO_UNLIKELY(*cur != '"')) {
    err = base::ErrStatus("Expected '\"' at the start of string. Got '%c'",
                          *start);
    return ReturnCode::kError;
  }
  // Start searching for the closing quote from the character after the opening
  // quote.
  const char* str_start = ++cur;
  for (;;) {
    // Find the next double quote.
    cur = static_cast<const char*>(
        memchr(cur, '"', static_cast<size_t>(end - cur)));
    // If no quote is found, the input is incomplete.
    if (PERFETTO_UNLIKELY(!cur)) {
      return ReturnCode::kIncompleteInput;
    }
    // Fast path: if the character before the quote is not a backslash, we
    // found the closing quote.
    if (PERFETTO_LIKELY(cur[-1] != '\\')) {
      break;
    }
    // Slow path: count consecutive backslashes before the quote. If the count
    // is even, the quote is not escaped and closes the string. If odd, the
    // quote is escaped and we continue searching.
    size_t backslash_count = 1;  // We already know cur[-1] == '\\'
    const char* p = cur - 2;
    while (p >= str_start && *p == '\\') {
      ++backslash_count;
      --p;
    }
    // If the backslash count is even, the quote closes the string.
    if ((backslash_count & 1) == 0) {
      break;
    }
    // The quote is escaped, continue searching.
    ++cur;
  }
  // Check if there are any backslashes in the string (indicating escapes).
  has_escapes =
      memchr(str_start, '\\', static_cast<size_t>(cur - str_start)) != nullptr;
  str = std::string_view(str_start, static_cast<size_t>(cur - str_start));
  out = cur + 1;
  return ReturnCode::kOk;
}

// Parses a JSON string, handling escape sequences if necessary.
// |start| and |end| define the input buffer. |out| is updated to point after
// the string. |str| receives the string_view of the parsed string (potentially
// unescaped). |unescaped_str| is used as a buffer if unescaping is needed. Sets
// |status| on error.
inline ReturnCode ParseString(const char* start,
                              const char* end,
                              const char*& out,
                              std::string_view& str,
                              std::string& unescaped_str,
                              base::Status& status) {
  const char* cur = start;
  PERFETTO_DCHECK(start != end);

  bool key_has_escapes = false;
  // First, scan the string to identify its boundaries and check for escapes.
  if (auto e = ScanString(cur, end, cur, str, key_has_escapes, status);
      e != ReturnCode::kOk) {
    return e;
  }
  // If escape sequences were found, unescape the string.
  if (PERFETTO_UNLIKELY(key_has_escapes)) {
    unescaped_str.clear();  // Clear previous unescaped content.
    if (auto e = internal::UnescapeString(str.data(), str.data() + str.size(),
                                          unescaped_str, status);
        e != ReturnCode::kOk) {
      return e;
    }
    // Update |str| to point to the unescaped version.
    str = unescaped_str;
  }
  out = cur;
  return ReturnCode::kOk;
}

// Scans input from |start| to |end| to find the end of a block delimited by
// |open_delim| and |close_delim| (e.g., '{' and '}').
// Handles nested delimiters and strings correctly.
// |out| is updated to point after the |close_delim|.
// Sets |status| on error.
inline ReturnCode ScanToEndOfDelimitedBlock(const char* start,
                                            const char* end,
                                            char open_delim,
                                            char close_delim,
                                            const char*& out,
                                            base::Status& status) {
  PERFETTO_DCHECK(start != end);
  PERFETTO_DCHECK(*start == open_delim);

  // Start scanning after the opening delimiter.
  const char* cur = start + 1;
  // Balance of open/close delimiters.
  uint32_t bal = 1;
  // Dummy for ScanString.
  std::string_view sv;
  bool has_escapes;
  while (cur != end) {
    char c = *cur;
    if (c == '"') {
      // If a string starts, scan past it.
      if (auto e = ScanString(cur, end, cur, sv, has_escapes, status);
          e != ReturnCode::kOk) {
        return e;
      }
    } else if (c == open_delim) {
      // Nested opening delimiter.
      ++cur;
      ++bal;
    } else if (c == close_delim) {
      // Closing delimiter.
      ++cur;
      if (PERFETTO_LIKELY(--bal == 0)) {
        // If balance is zero, block end is found.
        out = cur;
        return ReturnCode::kOk;
      }
    } else {
      // Other characters, just advance.
      ++cur;
    }
  }
  // Reached end without closing delimiter.
  return ReturnCode::kIncompleteInput;
}

// Converts a string representation of an integer to int64_t.
// |start| and |end| define the string segment for the number.
// |out| stores the parsed integer.
// Sets |status| on overflow or invalid format.
inline ReturnCode StringToInt64(const char* start,
                                const char* end,
                                int64_t& out,
                                base::Status& status) {
  const char* cur = start;
  PERFETTO_DCHECK(start != end);

  bool negative = false;
  if (*cur == '-') {
    negative = true;
    cur++;
  }
  // After a potential sign, there must be at least one digit.
  PERFETTO_DCHECK(cur != end);

  out = 0;
  // Precompute limits for overflow checking.
  const int64_t kAbsMaxDiv10 = std::numeric_limits<int64_t>::max() / 10;
  const int kAbsMaxMod10 = std::numeric_limits<int64_t>::max() % 10;
  for (; cur != end; ++cur) {
    // Should only be called with valid digits.
    PERFETTO_DCHECK(std::isdigit(*cur));
    int digit = *cur - '0';
    // Check for overflow before multiplication and addition.
    if (out > kAbsMaxDiv10 || (out == kAbsMaxDiv10 && digit > kAbsMaxMod10)) {
      // Special case for INT64_MIN, which is -(INT64_MAX + 1).
      if (negative && out == kAbsMaxDiv10 && digit == kAbsMaxMod10 + 1) {
        // This sequence of operations correctly forms INT64_MIN when negated
        // later.
      } else {
        status = base::ErrStatus("Integer overflow parsing '%.*s'",
                                 int(end - start), start);
        return ReturnCode::kError;
      }
    }
    out = out * 10 + digit;
  }
  if (negative) {
    out = -out;
  }
  return ReturnCode::kOk;
}

// Converts a string representation of a floating-point number to double.
// |start| and |end| define the string segment for the number.
// |out| stores the parsed double.
// Sets |status| on overflow, underflow (to 0.0), NaN, or invalid format.
inline ReturnCode StringToDouble(const char* start,
                                 const char* end,
                                 double& out,
                                 base::Status& status) {
  const char* cur = start;
  PERFETTO_DCHECK(cur != end);

  bool negative = false;
  if (*cur == '-') {
    negative = true;
    ++cur;
  }
  PERFETTO_DCHECK(cur != end);

  // Parse integer part.
  int64_t int_part = 0;
  for (; cur != end && std::isdigit(*cur); ++cur) {
    int_part = int_part * 10 + (*cur - '0');
  }
  // Parse fractional part.
  double fraction = 0;
  if (cur != end && *cur == '.') {
    ++cur;
    int64_t fract_int = 0;
    uint64_t divisor = 1;
    for (; cur != end && std::isdigit(*cur); ++cur) {
      fract_int = (*cur - '0') + fract_int * 10;
      divisor *= 10;
    }
    fraction = static_cast<double>(fract_int) / static_cast<double>(divisor);
  }
  // Parse exponent part.
  int64_t exponent_part = 0;
  bool exp_neg = false;
  if (cur != end && (*cur == 'e' || *cur == 'E')) {
    ++cur;
    if (cur != end && (*cur == '+' || *cur == '-')) {
      exp_neg = *cur++ == '-';
    }
    PERFETTO_DCHECK(cur != end);
    for (; cur != end && std::isdigit(*cur); ++cur) {
      exponent_part = exponent_part * 10 + (*cur - '0');
    }
  }
  // Combine parts.
  out = static_cast<double>(int_part) + fraction;
  if (exp_neg) {
    out /= std::pow(10, static_cast<double>(exponent_part));
  } else if (exponent_part > 0) {
    out *= std::pow(10, static_cast<double>(exponent_part));
  }
  // Check for infinity or NaN, which indicates an overflow/underflow during pow
  // or multiplication.
  if (std::isinf(out) || std::isnan(out)) {
    status = base::ErrStatus("Double overflow/underflow parsing '%.*s'",
                             int(end - start), start);
    return ReturnCode::kError;
  }
  out = negative ? -out : out;
  return ReturnCode::kOk;
}

// Parses a JSON number, which can be an integer or a double.
// |start| and |end| define the input buffer, |cur| points to the start of the
// number. |out| is updated to point after the parsed number. |out_num| stores
// the parsed JsonValue (either int64_t or double). Sets |status| on error.
inline ReturnCode ParseNumber(const char* start,
                              const char* end,
                              const char*& out,
                              JsonValue& out_num,
                              base::Status& status) {
  const char* cur = start;
  PERFETTO_DCHECK(cur != end);

  bool is_int_like = true;
  // Skip optional minus sign.
  cur += *cur == '-';
  // Handle leading zero: only allowed if it's the only digit before '.', 'e',
  // or end.
  if (cur != end && *cur == '0') {
    ++cur;
    // "01" is invalid.
    if (cur != end && std::isdigit(*cur)) {
      status = base::ErrStatus("Invalid number: leading zero in '%.*s'",
                               int(end - start), start);
      return ReturnCode::kError;
    }
  } else if (cur != end && *cur >= '1' && *cur <= '9') {
    ++cur;
    while (cur != end && std::isdigit(*cur)) {
      ++cur;
    }
  } else if (cur != end) {
    status = base::ErrStatus("Invalid number: expected digit in '%.*s'",
                             int(end - start), start);
    return ReturnCode::kError;
  }
  // Check for fractional part.
  if (cur != end && *cur == '.') {
    is_int_like = false;
    const char* frac_start_pos = ++cur;
    while (cur != end && std::isdigit(*cur)) {
      ++cur;
    }
    // Must have at least one digit after '.'.
    if (cur != end && cur == frac_start_pos) {
      status =
          base::ErrStatus("Invalid number: expected digit after '.' in '%.*s'",
                          int(end - start), start);
      return ReturnCode::kError;
    }
  }
  // Check for exponent part.
  if (cur != end && (*cur == 'e' || *cur == 'E')) {
    is_int_like = false;
    ++cur;
    // Optional sign for exponent.
    cur += cur != end && (*cur == '+' || *cur == '-');
    const char* exp_start_pos = cur;
    while (cur != end && std::isdigit(*cur)) {
      ++cur;
    }
    // Must have at least one digit after 'e' or 'E' (and optional sign).
    if (cur != end && cur == exp_start_pos) {
      status =
          base::ErrStatus("Invalid number: expected digit after 'e' in '%.*s'",
                          int(end - start), start);
      return ReturnCode::kError;
    }
  }
  // If end is reached before any non-numeric character, input is incomplete.
  if (PERFETTO_UNLIKELY(cur == end)) {
    return ReturnCode::kIncompleteInput;
  }

  // Attempt to parse as int64_t if it looked like an integer.
  if (is_int_like) {
    int64_t i_val;
    // The segment [start, cur) contains the number string.
    if (auto e = StringToInt64(start, cur, i_val, status);
        e == ReturnCode::kOk) {
      out_num = i_val;
      out = cur;
      return ReturnCode::kOk;
    }
    // If StringToInt64 failed (e.g. overflow), status is already set.
    // We might still try to parse as double if it's a large integer.
    // JSON spec doesn't limit integer precision, but we store as int64 or
    // double. If it overflows int64, it MUST be parsed as double.
  }

  // Parse as double (either because it wasn't int-like or int parsing
  // failed/overflowed).
  double d_val;
  if (auto e = StringToDouble(start, cur, d_val, status);
      e != ReturnCode::kOk) {
    // If StringToInt64 failed AND StringToDouble failed, return the error from
    // StringToDouble.
    return e;
  }
  out_num = d_val;
  out = cur;
  return ReturnCode::kOk;
}

}  // namespace internal

// Public return codes for the Iterator.
enum class ReturnCode : uint8_t {
  kOk = uint8_t(internal::ReturnCode::kOk),
  kError = uint8_t(internal::ReturnCode::kError),
  kIncompleteInput = uint8_t(internal::ReturnCode::kIncompleteInput),
  // Indicates the end of the current JSON object or array scope.
  kEndOfScope = 3,
};

// Parses the next JSON value from the input stream.
// |cur| is an in/out parameter pointing to the current position in the buffer.
// |end| points to the end of the buffer.
// |value| stores the parsed JsonValue.
// |unescaped_str| is a buffer for unescaping strings.
// Sets |status| on error.
inline ReturnCode ParseValue(const char*& cur,
                             const char* end,
                             JsonValue& value,
                             std::string& unescaped_str,
                             base::Status& status) {
  const char* start = cur;
  PERFETTO_CHECK(start != end);
  switch (*cur) {
    case '{': {
      auto e = internal::ScanToEndOfDelimitedBlock(start, end, '{', '}', cur,
                                                   status);
      value = Object{std::string_view(start, static_cast<size_t>(cur - start))};
      return static_cast<ReturnCode>(e);
    }
    case '[': {
      auto e = internal::ScanToEndOfDelimitedBlock(start, end, '[', ']', cur,
                                                   status);
      value = Array{std::string_view(start, static_cast<size_t>(cur - start))};
      return static_cast<ReturnCode>(e);
    }
    case '"':
      value = std::string_view();
      return static_cast<ReturnCode>(internal::ParseString(
          start, end, cur, base::unchecked_get<std::string_view>(value),
          unescaped_str, status));
    case 't':
      if (static_cast<size_t>(end - start) < 4) {
        return ReturnCode::kIncompleteInput;
      }
      if (std::string_view(start, 4) != "true") {
        status =
            base::ErrStatus("Invalid token: expected 'true' but got '%.*s'",
                            std::min(4, static_cast<int>(end - start)), start);
        return ReturnCode::kError;
      }
      cur += 4;
      value = true;
      return ReturnCode::kOk;
    case 'f':
      if (static_cast<size_t>(end - start) < 5) {
        return ReturnCode::kIncompleteInput;
      }
      if (std::string_view(start, 5) != "false") {
        status =
            base::ErrStatus("Invalid token: expected 'false' but got '%.*s'",
                            std::min(5, static_cast<int>(end - start)), start);
        return ReturnCode::kError;
      }
      cur += 5;
      value = false;
      return ReturnCode::kOk;
    case 'n':
      if (static_cast<size_t>(end - start) < 4) {
        return ReturnCode::kIncompleteInput;
      }
      if (std::string_view(start, 4) != "null") {
        status =
            base::ErrStatus("Invalid token: expected 'null' but got '%.*s'",
                            std::min(4, static_cast<int>(end - start)), start);
        return ReturnCode::kError;
      }
      cur += 4;
      value = Null{};
      return ReturnCode::kOk;
    default:
      return static_cast<ReturnCode>(
          internal::ParseNumber(start, end, cur, value, status));
  }
}

// An iterator-style parser for JSON.
// Allows for token-by-token processing of a JSON structure.
class Iterator {
 public:
  // Type of JSON structure currently being parsed (object or array).
  enum class ParseType : uint8_t {
    kObject,
    kArray,
  };

  // Resets the iterator to parse a new JSON string.
  // |begin| and |end| define the JSON string to be parsed.
  void Reset(const char* begin, const char* end) {
    cur_ = begin;
    end_ = end;
    parse_stack_.clear();
    status_ = base::OkStatus();
  }

  // Initializes parsing. Expects the input to start with '{' or '['.
  // Returns true on success, false on failure (e.g., not starting with { or [).
  bool ParseStart() {
    const char* cur = cur_;
    // Skip any leading whitespace.
    if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(cur, end_))) {
      // Reached end of input while expecting '{' or '['.
      status_ = base::ErrStatus(
          "Expected '{' or '[' at the start. Input is empty or whitespace "
          "only.");
      return false;
    }
    // Determine if it's an object or array and push to stack.
    if (*cur == '{') {
      parse_stack_.push_back(ParseType::kObject);
    } else if (*cur == '[') {
      parse_stack_.push_back(ParseType::kArray);
    } else {
      status_ =
          base::ErrStatus("Expected '{' or '[' at the start. Got '%c'", *cur);
      return false;
    }
    // Skip whitespace after the opening bracket.
    if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(++cur, end_))) {
      return false;
    }
    cur_ = cur;
    return true;
  }

  // Parses the next key-value field in an object without recursing into nested
  // objects/arrays. Assumes the iterator is currently inside an object. The
  // parsed key is available via `key()` and value via `value()`. Returns kOk on
  // success, kEndOfScope if '}' is found, or an error code.
  ReturnCode ParseObjectFieldWithoutRecursing() {
    PERFETTO_DCHECK(!parse_stack_.empty());
    PERFETTO_DCHECK(parse_stack_.back() == ParseType::kObject);
    const char* cur = cur_;
    // Check for the end of the object.
    if (PERFETTO_UNLIKELY(*cur == '}')) {
      if (auto e = OnEndOfScope(cur); e != ReturnCode::kOk) {
        return e;
      }
      cur_ = cur;
      return ReturnCode::kEndOfScope;
    }
    // Parse the field (key: value).
    if (auto e = ParseObjectFieldUntilValue(cur); e != ReturnCode::kOk) {
      return e;
    }
    // Parse the value itself.
    if (auto e = ParseValue(cur, end_, value_, unescaped_str_value_, status_);
        PERFETTO_UNLIKELY(e != ReturnCode::kOk)) {
      return e;
    }
    // Handle comma or closing brace after the value.
    if (auto e = OnPostValue(cur); e != ReturnCode::kOk) {
      return e;
    }
    cur_ = cur;
    return ReturnCode::kOk;
  }

  // Parses the next element. If it's an object or array, it recurses by pushing
  // onto the parse stack. Otherwise, it parses the primitive value.
  // The parsed key (if in an object) or value is available.
  // Returns kOk on success, kEndOfScope if '}' or ']' is found, or an error
  // code.
  ReturnCode ParseAndRecurse() {
    PERFETTO_DCHECK(!parse_stack_.empty());
    const char* cur = cur_;
    // Check for end of current scope (object or array).
    if (PERFETTO_UNLIKELY(*cur == '}' || *cur == ']')) {
      if (auto e = OnEndOfScope(cur); e != ReturnCode::kOk) {
        return e;
      }
      cur_ = cur;
      return ReturnCode::kEndOfScope;
    }
    // If current scope is an object, parse the key first.
    if (PERFETTO_LIKELY(parse_stack_.back() == ParseType::kObject)) {
      if (auto e = ParseObjectFieldUntilValue(cur);
          PERFETTO_UNLIKELY(e != ReturnCode::kOk)) {
        return e;
      }
    } else {
      if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(cur, end_))) {
        return ReturnCode::kIncompleteInput;
      }
    }

    // If the value is a new object or array, push to stack.
    if (*cur == '{') {
      parse_stack_.push_back(ParseType::kObject);
      // Value becomes an empty Object marker; its content isn't scanned yet
      // here.
      value_ = Object{std::string_view()};
      if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(++cur, end_))) {
        return ReturnCode::kIncompleteInput;
      }
      cur_ = cur;
      return ReturnCode::kOk;
    }
    if (*cur == '[') {
      parse_stack_.push_back(ParseType::kArray);
      // Value becomes an empty Array marker.
      value_ = Array{std::string_view()};
      if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(++cur, end_))) {
        return ReturnCode::kIncompleteInput;
      }
      cur_ = cur;
      return ReturnCode::kOk;
    }
    // Otherwise, parse the primitive value.
    if (auto e = ParseValue(cur, end_, value_, unescaped_str_value_, status_);
        PERFETTO_UNLIKELY(e != ReturnCode::kOk)) {
      return e;
    }
    // Handle comma or closing brace/bracket after the value.
    if (auto e = OnPostValue(cur); e != ReturnCode::kOk) {
      return e;
    }
    cur_ = cur;
    return ReturnCode::kOk;
  }

  // Returns the key of the last parsed object field.
  std::string_view key() const { return key_; }
  // Returns the value of the last parsed field or array element.
  const JsonValue& value() const { return value_; }
  // Returns the current parsing position in the input buffer.
  const char* cur() const { return cur_; }
  // Returns the status of the last operation (Ok or an error).
  const base::Status& status() const { return status_; }

  // Returns true if the entire JSON structure has been parsed (parse stack is
  // empty).
  bool eof() const { return parse_stack_.empty(); }
  // Returns the current parse stack (e.g., for debugging or context).
  const std::vector<ParseType>& parse_stack() const { return parse_stack_; }

 private:
  // Parses an object field up to the value (i.e., "key": ).
  // |cur| is advanced past the ':'.
  ReturnCode ParseObjectFieldUntilValue(const char*& cur) {
    // Skip whitespace before the key.
    if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(cur, end_))) {
      return ReturnCode::kIncompleteInput;
    }
    // Expect a string key.
    if (PERFETTO_UNLIKELY(*cur != '"')) {
      status_ =
          base::ErrStatus("Expected '\"' at the start of key. Got '%c'", *cur);
      return ReturnCode::kError;
    }
    if (auto e = internal::ParseString(cur, end_, cur, key_, unescaped_key_,
                                       status_);
        PERFETTO_UNLIKELY(e != internal::ReturnCode::kOk)) {
      return static_cast<ReturnCode>(e);
    }
    // Skip whitespace after the key.
    if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(cur, end_))) {
      return ReturnCode::kIncompleteInput;
    }
    // Expect a colon separator.
    if (PERFETTO_UNLIKELY(*cur != ':')) {
      status_ = base::ErrStatus("Expected ':' after key '%.*s'. Got '%c'",
                                int(key_.size()), key_.data(), *cur);
      return ReturnCode::kError;
    }
    // Skip whitespace after the colon.
    if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(++cur, end_))) {
      return ReturnCode::kIncompleteInput;
    }
    return ReturnCode::kOk;
  }

  // Handles characters after a parsed value (',' or closing '}' or ']').
  // |cur| is advanced past the delimiter and subsequent whitespace.
  ReturnCode OnPostValue(const char*& cur) {
    PERFETTO_DCHECK(!parse_stack_.empty());
    // Skip whitespace after the value.
    if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(cur, end_))) {
      return ReturnCode::kIncompleteInput;
    }
    // Determine expected end character based on current scope.
    char end_char = parse_stack_.back() == ParseType::kObject ? '}' : ']';
    // If comma, consume it and skip whitespace.
    if (PERFETTO_LIKELY(*cur == ',')) {
      ++cur;
      if (PERFETTO_UNLIKELY(!internal::SkipWhitespace(cur, end_))) {
        return ReturnCode::kIncompleteInput;
      }
    } else if (PERFETTO_UNLIKELY(*cur != end_char)) {
      // If not a comma, it must be the end character for the current scope.
      status_ = base::ErrStatus("Expected ',' or '%c' after value. Got '%c'",
                                end_char, *cur);
      // If we are in an object, the key_ context is relevant.
      if (parse_stack_.back() == ParseType::kObject && !key_.empty()) {
        status_ = base::ErrStatus(
            "Expected ',' or '%c' after value for key '%.*s'. Got '%c'",
            end_char, int(key_.size()), key_.data(), *cur);
      }
      return ReturnCode::kError;
    }
    // If it was end_char, it will be handled by ParseAndRecurse or
    // ParseObjectFieldWithoutRecursing in the next iteration, or by
    // OnEndOfScope.
    return ReturnCode::kOk;
  }

  // Handles the end of a scope ('}' or ']'), pops from parse stack.
  // |cur| is advanced past the closing delimiter.
  ReturnCode OnEndOfScope(const char*& cur) {
    if (PERFETTO_UNLIKELY(parse_stack_.empty())) {
      status_ = base::ErrStatus("Parse stack is empty on end of scope");
      return ReturnCode::kError;
    }
    ++cur;  // Consume '}' or ']'.
    parse_stack_.pop_back();
    // If not at the end of the entire JSON (i.e., stack is not empty),
    // then this scope was nested. We need to handle post-value for the parent.
    if (!parse_stack_.empty()) {
      if (auto e = OnPostValue(cur); e != ReturnCode::kOk) {
        return e;
      }
    }
    return ReturnCode::kOk;
  }

  // Pointer to the current parsing position in the input buffer.
  const char* cur_;
  // Pointer to the end of the input buffer.
  const char* end_;
  // Holds the most recently parsed object key.
  std::string_view key_;
  // Buffer for unescaped key string, if key_ contains escapes.
  std::string unescaped_key_;
  // Buffer for unescaped value string, if value_ (as string_view) contains
  // escapes.
  std::string unescaped_str_value_;
  // Holds the most recently parsed JSON value.
  JsonValue value_;
  // Stores the success/failure status of parsing operations.
  base::Status status_;
  // Stack to keep track of nested JSON structures (objects/arrays).
  std::vector<ParseType> parse_stack_;
};

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_JSON_PARSER_H_
