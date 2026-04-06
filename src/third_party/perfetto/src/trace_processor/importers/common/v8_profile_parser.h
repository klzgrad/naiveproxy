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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_V8_PROFILE_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_V8_PROFILE_PARSER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor {

// V8 CPU profile data structures.
struct V8CallFrame {
  std::optional<std::string> url;
  std::string function_name;
};

struct V8Node {
  uint32_t id = 0;
  std::optional<uint32_t> parent;
  std::vector<uint32_t> children;
  V8CallFrame call_frame;
};

struct V8Profile {
  std::optional<int64_t> start_time;
  std::vector<V8Node> nodes;
  std::vector<uint32_t> samples;
  std::vector<int64_t> time_deltas;
};

// Parse a V8 CPU profile JSON string.
// The JSON should have the structure:
// {
//   "startTime": <int64>,
//   "cpuProfile": { "nodes": [...], "samples": [...] },
//   "timeDeltas": [...]
// }
base::StatusOr<V8Profile> ParseV8Profile(std::string_view json_str);

// Parse a V8 CPU profile from Chrome trace event args.
// The JSON should have the structure: {"data": { ... profile ... }}
base::StatusOr<V8Profile> ParseV8ProfileArgs(std::string_view json_str);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_V8_PROFILE_PARSER_H_
