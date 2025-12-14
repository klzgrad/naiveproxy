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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/systrace/systrace_line.h"
#include "src/trace_processor/importers/systrace/systrace_line_parser.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {
namespace {

class SystraceLineSink
    : public TraceSorter::Sink<SystraceLine, SystraceLineSink> {
 public:
  explicit SystraceLineSink(SystraceLineParser* parser) : parser_(parser) {}
  void Parse(int64_t, SystraceLine data) { parser_->ParseLine(data); }

 private:
  SystraceLineParser* parser_ = nullptr;
};

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

bool IsCpusHeaderLine(const std::deque<uint8_t>& buf,
                      int64_t start,
                      int64_t end) {
  const std::array<uint8_t, 5> kCpusPrefix = {'c', 'p', 'u', 's', '='};
  const auto prefix_len = static_cast<int64_t>(kCpusPrefix.size());
  if (end - start < prefix_len) {
    return false;
  }
  // Check if line starts with "cpus="
  if (!std::equal(buf.begin() + start, buf.begin() + start + prefix_len,
                  kCpusPrefix.begin(), kCpusPrefix.end())) {
    return false;
  }
  // Check that everything after "cpus=" is digits
  for (int64_t i = start + prefix_len; i < end; ++i) {
    if (!std::isdigit(buf[static_cast<size_t>(i)])) {
      return false;
    }
  }
  return true;
}

}  // namespace

SystraceTraceParser::SystraceTraceParser(TraceProcessorContext* ctx)
    : line_parser_(ctx),
      ctx_(ctx),
      stream_(ctx->sorter->CreateStream(
          std::make_unique<SystraceLineSink>(&line_parser_))) {}
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

    // Also remove cpus=<number> header line from trace-cmd text output.
    auto first_newline =
        std::find(partial_buf_.begin(), partial_buf_.end(), '\n');
    if (first_newline != partial_buf_.end()) {
      int64_t line_len =
          static_cast<int64_t>(first_newline - partial_buf_.begin());
      if (IsCpusHeaderLine(partial_buf_, 0, line_len)) {
        partial_buf_.erase(partial_buf_.begin(), first_newline + 1);
      }
    }

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
      }
      if (!base::StartsWith(buffer, "#") && !buffer.empty()) {
        SystraceLine line;
        base::Status status = line_tokenizer_.Tokenize(buffer, &line);
        if (status.ok()) {
          stream_->Push(line.ts, std::move(line));
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
          UniquePid pupid =
              ctx_->process_tracker->GetOrCreateProcess(ppid.value());
          UniquePid upid =
              ctx_->process_tracker->GetOrCreateProcess(pid.value());
          upid =
              ctx_->process_tracker->UpdateProcessWithParent(upid, pupid, true);
          ctx_->process_tracker->SetProcessMetadata(upid, name,
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
          ctx_->process_tracker->UpdateThreadName(utid, cmd_id,
                                                  ThreadNamePriority::kOther);
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

}  // namespace perfetto::trace_processor
