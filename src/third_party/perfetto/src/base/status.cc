/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/base/status.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>

namespace perfetto::base {

Status ErrStatus(const char* format, ...) {
  std::string buf;
  buf.resize(1024);
  for (;;) {
    va_list ap;
    va_start(ap, format);
    int N = vsnprintf(buf.data(), buf.size() - 1, format, ap);
    va_end(ap);

    if (N <= 0) {
      buf = "[printf format error]";
      break;
    }

    auto sN = static_cast<size_t>(N);
    if (sN > buf.size() - 1) {
      // Indicates that the string was truncated and sN is the "number of
      // non-null bytes which would be needed to fit the result". This is the
      // C99 standard behaviour in the case of truncation. In that case, resize
      // the buffer to match the returned value (with + 1 for the null
      // terminator) and try again.
      buf.resize(sN + 1);
      continue;
    }
    if (sN == buf.size() - 1) {
      // Indicates that the string was likely truncated and sN is just the
      // number of bytes written into the string. This is the behaviour of
      // non-standard compilers (MSVC) etc. In that case, just double the
      // storage and try again.
      buf.resize(sN * 2);
      continue;
    }

    // Otherwise, indicates the string was written successfully: we need to
    // resize to match the number of non-null bytes and return.
    buf.resize(sN);
    break;
  }
  return Status(std::move(buf));
}

std::optional<std::string_view> Status::GetPayload(
    std::string_view type_url) const {
  if (ok()) {
    return std::nullopt;
  }
  for (const auto& kv : payloads_) {
    if (kv.type_url == type_url) {
      return kv.payload;
    }
  }
  return std::nullopt;
}

void Status::SetPayload(std::string_view type_url, std::string value) {
  if (ok()) {
    return;
  }
  for (auto& kv : payloads_) {
    if (kv.type_url == type_url) {
      kv.payload = value;
      return;
    }
  }
  payloads_.push_back(Payload{std::string(type_url), std::move(value)});
}

bool Status::ErasePayload(std::string_view type_url) {
  if (ok()) {
    return false;
  }
  auto it = std::remove_if(
      payloads_.begin(), payloads_.end(),
      [type_url](const Payload& p) { return p.type_url == type_url; });
  bool erased = it != payloads_.end();
  payloads_.erase(it, payloads_.end());
  return erased;
}

}  // namespace perfetto::base
