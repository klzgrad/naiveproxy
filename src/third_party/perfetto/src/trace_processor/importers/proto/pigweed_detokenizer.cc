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

#include "src/trace_processor/importers/proto/pigweed_detokenizer.h"

#include <array>
#include <cctype>
#include <cstring>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"

// Removed date for an entry that is live.
static constexpr uint32_t kDateRemovedNever = 0xFFFFFFFF;

static constexpr uint32_t kFormatBufferSize = 32;

static constexpr std::array<uint8_t, 8> kHeaderPrefix = {'T', 'O', 'K',  'E',
                                                         'N', 'S', '\0', '\0'};

struct Header {
  std::array<char, 6> magic;
  uint16_t version;
  uint32_t entry_count;
  uint32_t reserved;
};

struct Entry {
  uint32_t token;
  uint32_t date_removed;
};

static constexpr uint32_t ReadUint32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         static_cast<uint32_t>(bytes[1]) << 8 |
         static_cast<uint32_t>(bytes[2]) << 16 |
         static_cast<uint32_t>(bytes[3]) << 24;
}

namespace perfetto::trace_processor::pigweed {

PigweedDetokenizer CreateNullDetokenizer() {
  return PigweedDetokenizer{base::FlatHashMap<uint32_t, FormatString>()};
}

base::StatusOr<PigweedDetokenizer> CreateDetokenizer(
    const protozero::ConstBytes& bytes) {
  base::FlatHashMap<uint32_t, FormatString> tokens;
  // See Pigweed's token_database.h for a description of the format,
  // but tl;dr we have:
  //
  // * Header.
  // * Array of {token, date_removed} structs.
  // * Matching table of null-terminated strings.

  if (bytes.size < sizeof(Header)) {
    return base::ErrStatus("Truncated Pigweed database (no header)");
  }

  for (size_t i = 0; i < kHeaderPrefix.size(); ++i) {
    if (bytes.data[i] != kHeaderPrefix[i]) {
      return base::ErrStatus("Pigweed database has wrong magic");
    }
  }

  size_t entry_count = ReadUint32(bytes.data + offsetof(Header, entry_count));

  size_t entry_ix = sizeof(Header);
  size_t string_ix = sizeof(Header) + entry_count * sizeof(Entry);

  if (string_ix > bytes.size) {
    return base::ErrStatus("Truncated Pigweed database (no string table)");
  }

  for (size_t i = 0; i < entry_count; ++i) {
    uint32_t token = ReadUint32(bytes.data + entry_ix);
    uint32_t date_removed =
        ReadUint32(bytes.data + entry_ix + offsetof(Entry, date_removed));

    const uint8_t* next_null_char = static_cast<const uint8_t*>(
        memchr(bytes.data + string_ix, '\0', bytes.size - string_ix));
    const size_t next_string_ix =
        static_cast<size_t>(next_null_char - bytes.data) + 1;
    if (next_string_ix > bytes.size) {
      return base::ErrStatus(
          "Truncated Pigweed database (string table not terminated)");
    }

    if (date_removed == kDateRemovedNever) {
      std::string str(reinterpret_cast<const char*>(bytes.data + string_ix));

      tokens[token] = FormatString(str);
    }

    entry_ix += sizeof(Entry);
    string_ix = next_string_ix;
  }

  return PigweedDetokenizer(std::move(tokens));
}

PigweedDetokenizer::PigweedDetokenizer(
    base::FlatHashMap<uint32_t, FormatString> tokens)
    : tokens_(std::move(tokens)) {}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif  // defined(__GNUC__) || defined(__clang__)

base::StatusOr<DetokenizedString> PigweedDetokenizer::Detokenize(
    const protozero::ConstBytes& bytes) const {
  if (bytes.size < sizeof(uint32_t)) {
    return base::ErrStatus("Truncated Pigweed payload");
  }

  const uint32_t token = ReadUint32(bytes.data);

  FormatString* format = tokens_.Find(token);
  if (!format) {
    return DetokenizedString(token,
                             FormatString(std::string("Token not found")));
  }

  const uint8_t* ptr = bytes.data + sizeof(uint32_t);

  std::vector<std::variant<int64_t, uint64_t, double>> args;
  std::vector<std::string> args_formatted;
  for (Arg arg : format->args()) {
    char buffer[kFormatBufferSize];
    const char* fmt = arg.format.c_str();
    size_t formatted_size;

    if (arg.type == kFloat) {
      if (ptr + sizeof(float) > bytes.data + bytes.size) {
        return base::ErrStatus("Truncated Pigweed float");
      }

      float value_float;
      memcpy(&value_float, ptr, sizeof(value_float));
      ptr += sizeof(value_float);
      double value = static_cast<double>(value_float);
      args.push_back(value);
      formatted_size =
          perfetto::base::SprintfTrunc(buffer, kFormatBufferSize, fmt, value);
    } else {
      uint64_t raw;
      auto old_ptr = ptr;
      ptr = protozero::proto_utils::ParseVarInt(ptr, bytes.data + bytes.size,
                                                &raw);
      if (old_ptr == ptr) {
        return base::ErrStatus("Truncated Pigweed varint");
      }
      // All Pigweed integers (including unsigned) are zigzag encoded.
      int64_t value = ::protozero::proto_utils::ZigZagDecode(raw);
      if (arg.type == kSignedInt) {
        args.push_back(value);
        formatted_size =
            perfetto::base::SprintfTrunc(buffer, kFormatBufferSize, fmt, value);
      } else {
        uint64_t value_unsigned;
        memcpy(&value_unsigned, &value, sizeof(value_unsigned));
        if (arg.type == kUnsigned32) {
          value_unsigned &= 0xFFFFFFFFu;
        }
        args.push_back(value_unsigned);
        formatted_size = perfetto::base::SprintfTrunc(buffer, kFormatBufferSize,
                                                      fmt, value_unsigned);
      }
    }
    if (formatted_size == kFormatBufferSize - 1) {
      return base::ErrStatus("Exceeded buffer size for number");
    }
    args_formatted.push_back(std::string(buffer, formatted_size));
    if (ptr >= bytes.data + bytes.size) {
      break;
    }
  }

  return DetokenizedString(token, *format, args, args_formatted);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif  // defined(__GNUC__) || defined(__clang__)

DetokenizedString::DetokenizedString(const uint32_t token,
                                     FormatString format_string)
    : token_(token), format_string_(std::move(format_string)) {}

DetokenizedString::DetokenizedString(
    const uint32_t token,
    FormatString format_string,
    std::vector<std::variant<int64_t, uint64_t, double>> args,
    std::vector<std::string> args_formatted)
    : token_(token),
      format_string_(format_string),
      args_(args),
      args_formatted_(args_formatted) {}

std::string DetokenizedString::Format() const {
  const auto args = format_string_.args();
  const auto fmt = format_string_.template_str();
  if (args.size() == 0) {
    return fmt;
  }

  std::string result;

  result.append(fmt.substr(0, args[0].begin));

  for (size_t i = 0; i < args.size(); i++) {
    result.append(args_formatted_[i]);
    if (i < args.size() - 1) {
      result.append(fmt.substr(args[i].end, args[i + 1].begin - args[i].end));
    } else {
      result.append(fmt.substr(args[i].end, fmt.size() - args[i].end));
    }
  }

  return result;
}

static size_t SkipFlags(std::string fmt, size_t ix) {
  while (fmt[ix] == '-' || fmt[ix] == '+' || fmt[ix] == '#' || fmt[ix] == ' ' ||
         fmt[ix] == '0') {
    ix += 1;
  }
  return ix;
}

static size_t SkipAsteriskOrInteger(std::string fmt, size_t ix) {
  if (fmt[ix] == '*') {
    return ix + 1;
  }

  ix = (fmt[ix] == '-' || fmt[ix] == '+') ? ix + 1 : ix;

  while (std::isdigit(fmt[ix])) {
    ix += 1;
  }
  return ix;
}

static std::array<char, 2> ReadLengthModifier(std::string fmt, size_t ix) {
  // Check for ll or hh.
  if (fmt[ix] == fmt[ix + 1] && (fmt[ix] == 'l' || fmt[ix] == 'h')) {
    return {fmt[ix], fmt[ix + 1]};
  }
  if (std::strchr("hljztL", fmt[ix]) != nullptr) {
    return {fmt[ix]};
  }
  return {};
}

FormatString::FormatString(std::string format) : template_str_(format) {
  size_t fmt_start = 0;
  for (size_t i = 0; i < format.size(); i++) {
    if (format[i] == '%') {
      fmt_start = i;
      i += 1;

      i = SkipFlags(format, i);

      // Field width.
      i = SkipAsteriskOrInteger(format, i);

      // Precision.
      if (format[i] == '.') {
        i += 1;
        i = SkipAsteriskOrInteger(format, i);
      }

      // Length modifier
      const std::array<char, 2> length = ReadLengthModifier(format, i);
      i += (length[0] == '\0' ? 0 : 1) + (length[1] == '\0' ? 0 : 1);

      const char spec = format[i];
      const std::string arg_format =
          format.substr(fmt_start, i - fmt_start + 1);
      if (spec == 'c' || spec == 'd' || spec == 'i') {
        args_.push_back(Arg{kSignedInt, arg_format, fmt_start, i + 1});
      } else if (strchr("oxXup", spec) != nullptr) {
        // Size matters for unsigned integers.
        if (length[0] == 'j' || length[1] == 'l') {
          args_.push_back(Arg{kUnsigned64, arg_format, fmt_start, i + 1});
        } else {
          args_.push_back(Arg{kUnsigned32, arg_format, fmt_start, i + 1});
        }
      } else if (strchr("fFeEaAgG", spec) != nullptr) {
        args_.push_back(Arg{kFloat, arg_format, fmt_start, i + 1});
      } else {
        // Parsing failed.
        // We ignore this silently for now.
      }
    }
  }
}

}  // namespace perfetto::trace_processor::pigweed
