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

#include "src/trace_processor/importers/android_bugreport/android_log_event.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto::trace_processor {

namespace {
std::vector<base::StringView> FindLines(const uint8_t* data, size_t size) {
  std::vector<base::StringView> lines;
  const char* line_start = reinterpret_cast<const char*>(data);
  const char* ptr = line_start;
  const char* const end = ptr + size;

  for (; ptr != end; ++ptr) {
    if (*ptr == '\n') {
      lines.emplace_back(line_start, static_cast<size_t>(ptr - line_start));
      line_start = ptr + 1;
    }
  }
  return lines;
}
}  // namespace

// static
std::optional<AndroidLogEvent::Format> AndroidLogEvent::DetectFormat(
    base::StringView line) {
  auto p = base::SplitString(line.ToStdString(), " ");
  if (p.size() < 5)
    return std::nullopt;

  if (p[0].size() != 5 || p[0][2] != '-')
    return std::nullopt;

  if (p[1].size() < 10 || p[1][2] != ':' || p[1][5] != ':' || p[1][8] != '.')
    return std::nullopt;

  if (p[4].size() == 1 && p[4][0] >= 'A' && p[4][0] <= 'Z')
    return Format::kPersistentLog;

  if (p[5].size() == 1 && p[5][0] >= 'A' && p[5][0] <= 'Z')
    return Format::kBugreport;

  return std::nullopt;
}

// static
bool AndroidLogEvent::IsAndroidLogcat(const uint8_t* data, size_t size) {
  // Make sure we don't split an entire file into lines.
  constexpr size_t kMaxGuessAndroidLogEventLookAhead = 4096;
  std::vector<base::StringView> lines =
      FindLines(data, std::min(size, kMaxGuessAndroidLogEventLookAhead));

  auto is_marker = [](base::StringView line) {
    return line.StartsWith("--------");
  };
  auto it = std::find_if_not(lines.begin(), lines.end(), is_marker);

  return it != lines.end() && DetectFormat(*it).has_value();
}

}  // namespace perfetto::trace_processor
