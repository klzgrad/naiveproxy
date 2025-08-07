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

#include "src/trace_processor/importers/systrace/systrace_trace_parser.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"

#include <cctype>
#include <cinttypes>
#include <string>
#include <unordered_map>

namespace perfetto {
namespace trace_processor {
namespace {

std::vector<base::StringView> SplitOnSpaces(base::StringView str) {
  std::vector<base::StringView> result;
  for (size_t i = 0; i < str.size(); ++i) {
    // Consume all spaces.
    for (; i < str.size() && str.data()[i] == ' '; ++i)
      ;
    // If we haven't reached the end consume all non-spaces and add result.
    if (i != str.size()) {
      size_t start = i;
      for (; i < str.size() && str.data()[i] != ' '; ++i)
        ;
      result.push_back(base::StringView(str.data() + start, i - start));
    }
  }
  return result;
}

bool IsProcessDumpShortHeader(const std::vector<base::StringView>& tokens) {
  return tokens.size() == 4 && tokens[0] == "USER" && tokens[1] == "PID" &&
         tokens[2] == "TID" && tokens[3] == "CMD";
}

bool IsProcessDumpLongHeader(const std::vector<base::StringView>& tokens) {
  return tokens.size() > 4 && tokens[0] == "USER" && tokens[1] == "PID" &&
         tokens[2] == "PPID" && tokens[3] == "VSZ";
}

}  // namespace

SystraceTraceParser::SystraceTraceParser(TraceProcessorContext* ctx)
    : line_parser_(ctx), ctx_(ctx) {}
SystraceTraceParser::~SystraceTraceParser() = default;

base::Status SystraceTraceParser::Parse(TraceBlobView blob) {
  if (state_ == ParseState::kEndOfSystrace)
    return base::OkStatus();
  partial_buf_.insert(partial_buf_.end(), blob.data(),
                      blob.data() + blob.size());

  if (state_ == ParseState::kBeforeParse) {
    // Remove anything before the TRACE:\n marker, which is emitted when
    // obtaining traces via  `adb shell "atrace -t 1 sched" > out.txt`.
    std::array<uint8_t, 7> kAtraceMarker = {'T', 'R', 'A', 'C', 'E', ':', '\n'};
    auto search_end = partial_buf_.begin() +
                      static_cast<int>(std::min(partial_buf_.size(),
                                                kGuessTraceMaxLookahead));
    auto it = std::search(partial_buf_.begin(), search_end,
                          kAtraceMarker.begin(), kAtraceMarker.end());
    if (it != search_end)
      partial_buf_.erase(partial_buf_.begin(), it + kAtraceMarker.size());

    // Deal with HTML traces.
    state_ = partial_buf_[0] == '<' ? ParseState::kHtmlBeforeSystrace
                                    : ParseState::kSystrace;
  }

  // There can be multiple trace data sections in an HTML trace, we want to
  // ignore any that don't contain systrace data. In the future it would be
  // good to also parse the process dump section.
  const char kTraceDataSection[] =
      R"(<script class="trace-data" type="application/text">)";
  auto start_it = partial_buf_.begin();
  for (;;) {
    auto line_it = std::find(start_it, partial_buf_.end(), '\n');
    if (line_it == partial_buf_.end())
      break;

    std::string buffer(start_it, line_it);

    if (state_ == ParseState::kHtmlBeforeSystrace) {
      if (base::Contains(buffer, kTraceDataSection)) {
        state_ = ParseState::kTraceDataSection;
      }
    } else if (state_ == ParseState::kTraceDataSection) {
      if (base::StartsWith(buffer, "#") && base::Contains(buffer, "TASK-PID")) {
        state_ = ParseState::kSystrace;
      } else if (base::StartsWith(buffer, "PROCESS DUMP")) {
        state_ = ParseState::kProcessDumpLong;
      } else if (base::StartsWith(buffer, "CGROUP DUMP")) {
        state_ = ParseState::kCgroupDump;
      } else if (base::Contains(buffer, R"(</script>)")) {
        state_ = ParseState::kHtmlBeforeSystrace;
      }
    } else if (state_ == ParseState::kSystrace) {
      if (base::Contains(buffer, R"(</script>)")) {
        state_ = ParseState::kEndOfSystrace;
        break;
      } else if (!base::StartsWith(buffer, "#") && !buffer.empty()) {
        SystraceLine line;
        base::Status status = line_tokenizer_.Tokenize(buffer, &line);
        if (status.ok()) {
          line_parser_.ParseLine(std::move(line));
        } else {
          ctx_->storage->IncrementStats(stats::systrace_parse_failure);
        }
      }
    } else if (state_ == ParseState::kProcessDumpLong ||
               state_ == ParseState::kProcessDumpShort) {
      if (base::Contains(buffer, R"(</script>)")) {
        state_ = ParseState::kHtmlBeforeSystrace;
      } else {
        std::vector<base::StringView> tokens =
            SplitOnSpaces(base::StringView(buffer));
        if (IsProcessDumpShortHeader(tokens)) {
          state_ = ParseState::kProcessDumpShort;
        } else if (IsProcessDumpLongHeader(tokens)) {
          state_ = ParseState::kProcessDumpLong;
        } else if (state_ == ParseState::kProcessDumpLong &&
                   tokens.size() >= 10) {
          // Format is:
          // user pid ppid vsz rss wchan pc s name my cmd line
          const std::optional<uint32_t> pid =
              base::StringToUInt32(tokens[1].ToStdString());
          const std::optional<uint32_t> ppid =
              base::StringToUInt32(tokens[2].ToStdString());
          base::StringView name = tokens[8];
          // Command line may contain spaces, merge all remaining tokens:
          const char* cmd_start = tokens[9].data();
          base::StringView cmd(
              cmd_start,
              static_cast<size_t>((buffer.data() + buffer.size()) - cmd_start));
          if (!pid || !ppid) {
            PERFETTO_ELOG("Could not parse line '%s'", buffer.c_str());
            return base::ErrStatus("Could not parse PROCESS DUMP line");
          }
          ctx_->process_tracker->SetProcessMetadata(pid.value(), ppid, name,
                                                    base::StringView());
        } else if (state_ == ParseState::kProcessDumpShort &&
                   tokens.size() >= 4) {
          // Format is:
          // username pid tid my cmd line
          const std::optional<uint32_t> tgid =
              base::StringToUInt32(tokens[1].ToStdString());
          const std::optional<uint32_t> tid =
              base::StringToUInt32(tokens[2].ToStdString());
          // Command line may contain spaces, merge all remaining tokens:
          const char* cmd_start = tokens[3].data();
          base::StringView cmd(
              cmd_start,
              static_cast<size_t>((buffer.data() + buffer.size()) - cmd_start));
          StringId cmd_id =
              ctx_->storage->mutable_string_pool()->InternString(cmd);
          if (!tid || !tgid) {
            PERFETTO_ELOG("Could not parse line '%s'", buffer.c_str());
            return base::ErrStatus("Could not parse PROCESS DUMP line");
          }
          UniqueTid utid =
              ctx_->process_tracker->UpdateThread(tid.value(), tgid.value());
          ctx_->process_tracker->UpdateThreadNameByUtid(
              utid, cmd_id, ThreadNamePriority::kOther);
        }
      }
    } else if (state_ == ParseState::kCgroupDump) {
      if (base::Contains(buffer, R"(</script>)")) {
        state_ = ParseState::kHtmlBeforeSystrace;
      }
      // TODO(lalitm): see if it is important to parse this.
    }
    start_it = line_it + 1;
  }
  if (state_ == ParseState::kEndOfSystrace) {
    partial_buf_.clear();
  } else {
    partial_buf_.erase(partial_buf_.begin(), start_it);
  }
  return base::OkStatus();
}

base::Status SystraceTraceParser::NotifyEndOfFile() {
  return base::OkStatus();
}

}  // namespace trace_processor
}  // namespace perfetto
