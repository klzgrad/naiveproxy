/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "perfetto/ext/base/string_utils.h"

#include <locale.h>
#include <stdarg.h>
#include <string.h>

#include <algorithm>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <xlocale.h>
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>
#endif

#include <cinttypes>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"

namespace perfetto {
namespace base {

// Locale-independent as possible version of strtod.
double StrToD(const char* nptr, char** endptr) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) ||           \
    PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  static auto c_locale = newlocale(LC_ALL, "C", nullptr);
  return strtod_l(nptr, endptr, c_locale);
#else
  return strtod(nptr, endptr);
#endif
}

bool StartsWith(const std::string& str, const std::string& prefix) {
  return str.compare(0, prefix.length(), prefix) == 0;
}

bool StartsWithAny(const std::string& str,
                   const std::vector<std::string>& prefixes) {
  return std::any_of(
      prefixes.begin(), prefixes.end(),
      [&str](const std::string& prefix) { return StartsWith(str, prefix); });
}

bool EndsWith(const std::string& str, const std::string& suffix) {
  if (suffix.size() > str.size())
    return false;
  return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

bool Contains(const std::string& haystack, const char needle) {
  return haystack.find(needle) != std::string::npos;
}

bool Contains(const std::vector<std::string>& haystack,
              const std::string& needle) {
  return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

size_t Find(const StringView& needle, const StringView& haystack) {
  if (needle.empty())
    return 0;
  if (needle.size() > haystack.size())
    return std::string::npos;
  for (size_t i = 0; i < haystack.size() - (needle.size() - 1); ++i) {
    if (strncmp(haystack.data() + i, needle.data(), needle.size()) == 0)
      return i;
  }
  return std::string::npos;
}

bool CaseInsensitiveEqual(const std::string& first, const std::string& second) {
  return first.size() == second.size() &&
         std::equal(
             first.begin(), first.end(), second.begin(),
             [](char a, char b) { return Lowercase(a) == Lowercase(b); });
}

std::string Join(const std::vector<std::string>& parts,
                 const std::string& delim) {
  std::string acc;
  for (size_t i = 0; i < parts.size(); ++i) {
    acc += parts[i];
    if (i + 1 != parts.size()) {
      acc += delim;
    }
  }
  return acc;
}

std::vector<std::string> SplitString(const std::string& text,
                                     const std::string& delimiter) {
  PERFETTO_CHECK(!delimiter.empty());

  std::vector<std::string> output;
  size_t start = 0;
  size_t next;
  for (;;) {
    next = std::min(text.find(delimiter, start), text.size());
    if (next > start)
      output.emplace_back(&text[start], next - start);
    start = next + delimiter.size();
    if (start >= text.size())
      break;
  }
  return output;
}

std::string TrimWhitespace(const std::string& str) {
  std::string whitespaces = "\t\n ";

  size_t front_idx = str.find_first_not_of(whitespaces);
  std::string front_trimmed =
      front_idx == std::string::npos ? "" : str.substr(front_idx);

  size_t end_idx = front_trimmed.find_last_not_of(whitespaces);
  return end_idx == std::string::npos ? ""
                                      : front_trimmed.substr(0, end_idx + 1);
}

std::string StripPrefix(const std::string& str, const std::string& prefix) {
  return StartsWith(str, prefix) ? str.substr(prefix.size()) : str;
}

std::string StripSuffix(const std::string& str, const std::string& suffix) {
  return EndsWith(str, suffix) ? str.substr(0, str.size() - suffix.size())
                               : str;
}

std::string ToUpper(const std::string& str) {
  // Don't use toupper(), it depends on the locale.
  std::string res(str);
  auto end = res.end();
  for (auto c = res.begin(); c != end; ++c)
    *c = Uppercase(*c);
  return res;
}

std::string ToLower(const std::string& str) {
  // Don't use tolower(), it depends on the locale.
  std::string res(str);
  auto end = res.end();
  for (auto c = res.begin(); c != end; ++c)
    *c = Lowercase(*c);
  return res;
}

std::string ToHex(const char* data, size_t size) {
  std::string hex(2 * size + 1, 'x');
  for (size_t i = 0; i < size; ++i) {
    // snprintf prints 3 characters, the two hex digits and a null byte. As we
    // write left to right, we keep overwriting the nullbytes, except for the
    // last call to snprintf.
    snprintf(&(hex[2 * i]), 3, "%02hhx", data[i]);
  }
  // Remove the trailing nullbyte produced by the last snprintf.
  hex.resize(2 * size);
  return hex;
}

std::string IntToHexString(uint32_t number) {
  size_t max_size = 11;  // Max uint32 is 0xFFFFFFFF + 1 for null byte.
  std::string buf;
  buf.resize(max_size);
  size_t final_len = SprintfTrunc(&buf[0], max_size, "0x%02x", number);
  buf.resize(static_cast<size_t>(final_len));  // Cuts off the final null byte.
  return buf;
}

std::string Uint64ToHexString(uint64_t number) {
  return "0x" + Uint64ToHexStringNoPrefix(number);
}

std::string Uint64ToHexStringNoPrefix(uint64_t number) {
  size_t max_size = 17;  // Max uint64 is FFFFFFFFFFFFFFFF + 1 for null byte.
  std::string buf;
  buf.resize(max_size);
  size_t final_len = SprintfTrunc(&buf[0], max_size, "%" PRIx64 "", number);
  buf.resize(static_cast<size_t>(final_len));  // Cuts off the final null byte.
  return buf;
}

std::string StripChars(const std::string& str,
                       const std::string& chars,
                       char replacement) {
  std::string res(str);
  const char* start = res.c_str();
  const char* remove = chars.c_str();
  for (const char* c = strpbrk(start, remove); c; c = strpbrk(c + 1, remove))
    res[static_cast<uintptr_t>(c - start)] = replacement;
  return res;
}

std::string ReplaceAll(std::string str,
                       const std::string& to_replace,
                       const std::string& replacement) {
  PERFETTO_CHECK(!to_replace.empty());
  size_t pos = 0;
  while ((pos = str.find(to_replace, pos)) != std::string::npos) {
    str.replace(pos, to_replace.length(), replacement);
    pos += replacement.length();
  }
  return str;
}

bool CheckAsciiAndRemoveInvalidUTF8(base::StringView str, std::string& output) {
  bool is_ascii = std::all_of(str.begin(), str.end(), [](char c) {
    return (static_cast<unsigned char>(c) & 0b10000000) == 0b00000000;
  });
  if (is_ascii) {
    return true;
  }

  // https://www.rfc-editor.org/rfc/rfc3629.txt
  output.clear();
  output.reserve(str.size());
  for (size_t i = 0; i < str.size();) {
    unsigned char c = static_cast<unsigned char>(str.data()[i]);
    size_t num_bytes = 0;
    bool valid_sequence = true;

    if ((c & 0b10000000) == 0b00000000) {
      num_bytes = 1;
    } else if ((c & 0b11100000) == 0b11000000) {
      num_bytes = 2;
    } else if ((c & 0b11110000) == 0b11100000) {
      num_bytes = 3;
    } else if ((c & 0b11111000) == 0b11110000) {
      num_bytes = 4;
    } else {
      valid_sequence = false;
      // Skip this byte
      num_bytes = 1;
    }

    if (valid_sequence) {
      // Check if enough bytes are available in the string
      if (i + num_bytes > str.size()) {
        valid_sequence = false;
        num_bytes = 1;  // Treat as a single invalid byte for advancement
      } else {
        // Check for overlong encodings, surrogates, and out-of-range
        if (num_bytes == 2 && c < 0b11000010) {  // 0xC2
          valid_sequence = false;                // Overlong
        } else if (num_bytes == 3) {
          unsigned char byte2 = static_cast<unsigned char>(str.data()[i + 1]);
          if ((c == 0b11100000 && byte2 < 0b10100000) ||   // Overlong E0
              (c == 0b11101101 && byte2 >= 0b10100000)) {  // Surrogate ED
            valid_sequence = false;
          }
        } else if (num_bytes == 4) {
          unsigned char byte2 = static_cast<unsigned char>(str.data()[i + 1]);
          if ((c == 0b11110000 && byte2 < 0b10010000) ||  // Overlong F0
              (c == 0b11110100 && byte2 > 0b10001111)) {  // Out of range F4
            valid_sequence = false;
          }
        }

        if (valid_sequence && num_bytes > 1) {
          for (size_t j = 1; j < num_bytes; ++j) {
            unsigned char continuation_byte =
                static_cast<unsigned char>(str.data()[i + j]);
            if ((continuation_byte & 0b11000000) != 0b10000000) {
              valid_sequence = false;
              break;
            }
          }
        }
      }
    }

    if (valid_sequence) {
      for (size_t j = 0; j < num_bytes; ++j) {
        output.push_back(str.data()[i + j]);
      }
    }

    i += num_bytes;
  }
  return false;
}

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
bool WideToUTF8(const std::wstring& source, std::string& output) {
  if (source.empty() ||
      source.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  int size = ::WideCharToMultiByte(CP_UTF8, 0, &source[0],
                                   static_cast<int>(source.size()), nullptr, 0,
                                   nullptr, nullptr);
  output.assign(static_cast<size_t>(size), '\0');
  if (::WideCharToMultiByte(CP_UTF8, 0, &source[0],
                            static_cast<int>(source.size()), &output[0], size,
                            nullptr, nullptr) != size) {
    return false;
  }
  return true;
}
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
bool UTF8ToWide(const std::string& source, std::wstring& output) {
  if (source.empty() ||
      source.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  int size = ::MultiByteToWideChar(CP_UTF8, 0, &source[0],
                                   static_cast<int>(source.size()), nullptr, 0);
  output.assign(static_cast<size_t>(size), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, 0, &source[0],
                            static_cast<int>(source.size()), &output[0],
                            size) != size) {
    return false;
  }
  return true;
}
#endif  // PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

size_t SprintfTrunc(char* dst, size_t dst_size, const char* fmt, ...) {
  if (PERFETTO_UNLIKELY(dst_size == 0))
    return 0;

  va_list args;
  va_start(args, fmt);
  int src_size = vsnprintf(dst, dst_size, fmt, args);
  va_end(args);

  if (PERFETTO_UNLIKELY(src_size <= 0)) {
    dst[0] = '\0';
    return 0;
  }

  size_t res;
  if (PERFETTO_LIKELY(src_size < static_cast<int>(dst_size))) {
    // Most common case.
    res = static_cast<size_t>(src_size);
  } else {
    // Truncation case.
    res = dst_size - 1;
  }

  PERFETTO_DCHECK(res < dst_size);
  PERFETTO_DCHECK(dst[res] == '\0');
  return res;
}

std::optional<LineWithOffset> FindLineWithOffset(base::StringView str,
                                                 uint32_t offset) {
  static constexpr char kNewLine = '\n';
  uint32_t line_offset = 0;
  uint32_t line_count = 1;
  for (uint32_t i = 0; i < str.size(); ++i) {
    if (str.at(i) == kNewLine) {
      line_offset = i + 1;
      line_count++;
      continue;
    }
    if (i == offset) {
      size_t end_offset = str.find(kNewLine, i);
      if (end_offset == std::string::npos) {
        end_offset = str.size();
      }
      base::StringView line = str.substr(line_offset, end_offset - line_offset);
      return LineWithOffset{line, offset - line_offset, line_count};
    }
  }
  return std::nullopt;
}

}  // namespace base
}  // namespace perfetto
