/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/ext/base/base64.h"

namespace perfetto {
namespace base {

namespace {

constexpr char kPadding = '=';

constexpr char kEncTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static_assert(sizeof(kEncTable) == (1u << 6) + sizeof('\0'), "Bad table size");

// Maps an ASCII character to its 6-bit value. It only contains translations
// from '+' to 'z'. Supports the standard (+/) and URL-safe (-_) alphabets.
constexpr uint8_t kX = 0xff;  // Value used for invalid characters
constexpr uint8_t kDecTable[] = {
    62, kX, 62, kX, 63, 52, 53, 54, 55, 56,  // 00 - 09
    57, 58, 59, 60, 61, kX, kX, kX, 0,  kX,  // 10 - 19
    kX, kX, 0,  1,  2,  3,  4,  5,  6,  7,   // 20 - 29
    8,  9,  10, 11, 12, 13, 14, 15, 16, 17,  // 30 - 39
    18, 19, 20, 21, 22, 23, 24, 25, kX, kX,  // 40 - 49
    kX, kX, 63, kX, 26, 27, 28, 29, 30, 31,  // 50 - 59
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41,  // 60 - 69
    42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  // 70 - 79
};
constexpr char kMinDecChar = '+';
constexpr char kMaxDecChar = 'z';
static_assert(kMaxDecChar - kMinDecChar <= sizeof(kDecTable), "Bad table size");

inline uint8_t DecodeChar(char c) {
  if (c < kMinDecChar || c > kMaxDecChar)
    return kX;
  return kDecTable[c - kMinDecChar];
}

}  // namespace

ssize_t Base64Encode(const void* src,
                     size_t src_size,
                     char* dst,
                     size_t dst_size) {
  const size_t padded_dst_size = Base64EncSize(src_size);
  if (dst_size < padded_dst_size)
    return -1;  // Not enough space in output.

  const uint8_t* rd = static_cast<const uint8_t*>(src);
  const uint8_t* const end = rd + src_size;
  size_t wr_size = 0;
  while (rd < end) {
    uint8_t s[3]{};
    s[0] = *(rd++);
    dst[wr_size++] = kEncTable[s[0] >> 2];

    uint8_t carry0 = static_cast<uint8_t>((s[0] & 0x03) << 4);
    if (PERFETTO_LIKELY(rd < end)) {
      s[1] = *(rd++);
      dst[wr_size++] = kEncTable[carry0 | (s[1] >> 4)];
    } else {
      dst[wr_size++] = kEncTable[carry0];
      dst[wr_size++] = kPadding;
      dst[wr_size++] = kPadding;
      break;
    }

    uint8_t carry1 = static_cast<uint8_t>((s[1] & 0x0f) << 2);
    if (PERFETTO_LIKELY(rd < end)) {
      s[2] = *(rd++);
      dst[wr_size++] = kEncTable[carry1 | (s[2] >> 6)];
    } else {
      dst[wr_size++] = kEncTable[carry1];
      dst[wr_size++] = kPadding;
      break;
    }

    dst[wr_size++] = kEncTable[s[2] & 0x3f];
  }
  PERFETTO_DCHECK(wr_size == padded_dst_size);
  return static_cast<ssize_t>(padded_dst_size);
}

std::string Base64Encode(const void* src, size_t src_size) {
  std::string dst;
  dst.resize(Base64EncSize(src_size));
  auto res = Base64Encode(src, src_size, &dst[0], dst.size());
  PERFETTO_CHECK(res == static_cast<ssize_t>(dst.size()));
  return dst;
}

ssize_t Base64Decode(const char* src,
                     size_t src_size,
                     uint8_t* dst,
                     size_t dst_size) {
  const size_t min_dst_size = Base64DecSize(src_size);
  if (dst_size < min_dst_size)
    return -1;

  const char* rd = src;
  const char* const end = src + src_size;
  size_t wr_size = 0;

  char s[4]{};
  while (rd < end) {
    uint8_t d[4];
    for (uint32_t j = 0; j < 4; j++) {
      // Padding is only feasible for the last 2 chars of each group of 4.
      s[j] = rd < end ? *(rd++) : (j < 2 ? '\0' : kPadding);
      d[j] = DecodeChar(s[j]);
      if (d[j] == kX)
        return -1;  // Invalid input char.
    }
    dst[wr_size] = static_cast<uint8_t>((d[0] << 2) | (d[1] >> 4));
    dst[wr_size + 1] = static_cast<uint8_t>((d[1] << 4) | (d[2] >> 2));
    dst[wr_size + 2] = static_cast<uint8_t>((d[2] << 6) | (d[3]));
    wr_size += 3;
  }

  PERFETTO_CHECK(wr_size <= dst_size);
  wr_size -= (s[3] == kPadding ? 1 : 0) + (s[2] == kPadding ? 1 : 0);
  return static_cast<ssize_t>(wr_size);
}

std::optional<std::string> Base64Decode(const char* src, size_t src_size) {
  std::string dst;
  dst.resize(Base64DecSize(src_size));
  auto res = Base64Decode(src, src_size, reinterpret_cast<uint8_t*>(&dst[0]),
                          dst.size());
  if (res < 0)
    return std::nullopt;  // Decoding error.

  PERFETTO_CHECK(res <= static_cast<ssize_t>(dst.size()));
  dst.resize(static_cast<size_t>(res));
  return std::make_optional(dst);
}

}  // namespace base
}  // namespace perfetto
