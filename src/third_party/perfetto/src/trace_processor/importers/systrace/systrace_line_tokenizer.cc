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

#include "src/trace_processor/importers/systrace/systrace_line_tokenizer.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/util/regex.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace perfetto::trace_processor {

namespace {
std::string SubstrTrim(const std::string& input) {
  std::string s = input;
  s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                  [](char ch) { return !std::isspace(ch); }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](char ch) { return !std::isspace(ch); })
              .base(),
          s.end());
  return s;
}
}  // namespace

SystraceLineTokenizer::SystraceLineTokenizer()
    : std_line_matcher_(
          std::regex(R"(-(\d+)\s+\(?\s*(\d+|-+)?\)?\s?\[(\d+)\]\s*)"
                     R"([a-zA-Z0-9.]{0,5}\s*(\d+\.\d+):\s+(\S+):)")) {
  if constexpr (regex::IsRegexSupported()) {
    auto regex_or = regex::Regex::Create(
        R"(-([0-9]+)[[:space:]]+\(?[[:space:]]*([0-9]+|-+)?\)?[[:space:]]?\[([0-9]+)\][[:space:]]*[a-zA-Z0-9.]{0,5}[[:space:]]*([0-9]+\.[0-9]+):[[:space:]]+([^[:space:]]+):)");
    if (!regex_or.ok()) {
      PERFETTO_FATAL("%s", regex_or.status().c_message());
    }
    line_matcher_ = std::make_unique<regex::Regex>(std::move(regex_or.value()));
  }
}

// TODO(hjd): This should be more robust to being passed random input.
// This can happen if we mess up detecting a gzip trace for example.
base::Status SystraceLineTokenizer::Tokenize(const std::string& buffer,
                                             SystraceLine* line) {
  // An example line from buffer looks something like the following:
  // kworker/u16:1-77    (   77) [004] ....   316.196720: 0:
  // B|77|__scm_call_armv8_64|0
  //
  // However, sometimes the tgid can be missing and buffer looks like this:
  // <idle>-0     [000] ...2     0.002188: task_newtask: pid=1 ...
  //
  // Also the irq fields can be missing (we don't parse these anyway)
  // <idle>-0     [000]  0.002188: task_newtask: pid=1 ...
  //
  // The task name can contain any characters e.g -:[(/ and for this reason
  // it is much easier to use a regex (even though it is slower than parsing
  // manually)

  std::vector<std::string_view> matches;
  bool matched;
  if constexpr (regex::IsRegexSupported()) {
    line_matcher_->Submatch(buffer.c_str(), matches);
    matched = !matches.empty();
  } else {
    std::smatch smatches;
    matched = std::regex_search(buffer, smatches, std_line_matcher_);
    for (const auto& smatch : smatches) {
      matches.emplace_back(&*smatch.first, smatch.length());
    }
  }
  if (!matched) {
    return base::ErrStatus("Not a known systrace event format (line: %s)",
                           buffer.c_str());
  }

  std::string pid_str(matches[1]);
  std::string cpu_str(matches[3]);
  std::string ts_str(matches[4]);

  std::string_view prefix(
      buffer.data(), static_cast<size_t>(matches[0].data() - buffer.data()));

  const char* match_end = matches[0].data() + matches[0].size();
  std::string_view suffix(
      match_end,
      static_cast<size_t>(buffer.data() + buffer.size() - match_end));
  line->task = SubstrTrim(std::string(prefix));
  line->tgid_str = matches[2];
  line->event_name = matches[5];
  line->args_str = SubstrTrim(std::string(suffix));

  std::optional<uint32_t> maybe_pid = base::StringToUInt32(pid_str);
  if (!maybe_pid.has_value()) {
    return base::Status("Could not convert pid " + pid_str);
  }
  line->pid = maybe_pid.value();

  std::optional<uint32_t> maybe_cpu = base::StringToUInt32(cpu_str);
  if (!maybe_cpu.has_value()) {
    return base::Status("Could not convert cpu " + cpu_str);
  }
  line->cpu = maybe_cpu.value();

  std::optional<double> maybe_ts = base::StringToDouble(ts_str);
  if (!maybe_ts.has_value()) {
    return base::ErrStatus("Could not convert ts %s", ts_str.c_str());
  }
  line->ts = static_cast<int64_t>(maybe_ts.value() * 1e9);

  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
