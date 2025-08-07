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

#include "src/trace_processor/importers/perf_text/perf_text_sample_line_parser.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/string_utils.h"

namespace perfetto::trace_processor::perf_text_importer {

namespace {

std::string_view FindTsAtEnd(std::string_view line) {
  // We need to have 8 characters to have a valid timestamp with decimal
  // and 6 trailing digits.
  if (line.size() < 8) {
    return {};
  }
  // All of the 6 trailing digits should be digits.
  for (char c : line.substr(line.size() - 6)) {
    if (!isdigit(c)) {
      return {};
    }
  }
  // 7 digits from the end should be a '.'.
  if (line[line.size() - 7] != '.') {
    return {};
  }

  // A space before the timestamp dot should exist.
  std::string_view until_dot = line.substr(0, line.size() - 7);
  size_t c = until_dot.rfind(' ');
  if (c == std::string_view::npos) {
    return {};
  }

  // All the characters between the last space and the colon should also
  // be the digits.
  for (char x : until_dot.substr(c + 1, until_dot.size() - c - 1)) {
    if (!isdigit(x)) {
      return {};
    }
  }
  return line.substr(c + 1);
}

}  // namespace

std::optional<SampleLine> ParseSampleLine(std::string_view line) {
  // Example of what we're parsing here:
  // trace_processor 3962131 303057.417513:          1 cpu_atom/cycles/Pu:
  //
  // Find colons and look backwards to find something which looks like a
  // timestamp. Anything before that is metadata of the sample we may be able
  // to parse out.
  for (size_t s = 0, cln = line.find(':', s); cln != std::string_view::npos;
       s = cln + 1, cln = line.find(':', s)) {
    std::string_view raw_ts = FindTsAtEnd(line.substr(0, cln));
    if (raw_ts.empty()) {
      continue;
    }
    std::optional<double> ts = base::StringToDouble(std::string(raw_ts));
    if (!ts) {
      continue;
    }
    std::string before_ts(line.data(),
                          static_cast<size_t>(raw_ts.data() - line.data()));

    // simpleperf puts tabs after the comm while perf puts spaces. Make it
    // consistent and just use spaces.
    before_ts = base::ReplaceAll(before_ts, "\t", "  ");

    std::vector<std::string> pieces = base::SplitString(before_ts, " ");
    if (pieces.empty()) {
      continue;
    }

    size_t pos = pieces.size() - 1;

    // Try to parse out the CPU in the form: '[cpu]' (e.g. '[3]').
    std::optional<uint32_t> cpu;
    if (base::StartsWith(pieces[pos], "[") &&
        base::EndsWith(pieces[pos], "]")) {
      cpu = base::StringToUInt32(pieces[pos].substr(1, pieces[pos].size() - 2));
      if (!cpu) {
        continue;
      }
      --pos;
    }

    // Try to parse out the tid and pid in the form 'pid/tid' (e.g.
    // '1024/1025'). If there's no '/' then just try to parse it as a tid.
    std::vector<std::string> pid_and_tid = base::SplitString(pieces[pos], "/");
    if (pid_and_tid.size() == 0 || pid_and_tid.size() > 2) {
      continue;
    }

    uint32_t tid_idx = pid_and_tid.size() == 1 ? 0 : 1;
    auto opt_tid = base::StringToUInt32(pid_and_tid[tid_idx]);
    if (!opt_tid) {
      continue;
    }
    uint32_t tid = *opt_tid;

    std::optional<uint32_t> pid;
    if (pid_and_tid.size() == 2) {
      pid = base::StringToUInt32(pid_and_tid[0]);
      if (!pid) {
        continue;
      }
    }

    // All the remaining pieces are the comm which needs to be joined together
    // with ' '.
    pieces.resize(pos);
    std::string comm = base::Join(pieces, " ");
    return SampleLine{
        comm, pid, tid, cpu, static_cast<int64_t>(*ts * 1000 * 1000 * 1000),
    };
  }
  return std::nullopt;
}

bool IsPerfTextFormatTrace(const uint8_t* ptr, size_t size) {
  std::string_view str(reinterpret_cast<const char*>(ptr), size);
  size_t nl = str.find('\n');
  if (nl == std::string_view::npos) {
    return false;
  }
  return ParseSampleLine(str.substr(0, nl)).has_value();
}

}  // namespace perfetto::trace_processor::perf_text_importer
