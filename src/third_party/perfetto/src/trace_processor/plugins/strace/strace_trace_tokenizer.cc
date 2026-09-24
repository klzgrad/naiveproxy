/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/plugins/strace/strace_trace_tokenizer.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/plugins/strace/strace_event.h"
#include "src/trace_processor/plugins/strace/strace_trace_parser.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/clock_synchronizer.h"

namespace perfetto::trace_processor::strace_importer {

namespace {

constexpr int64_t kNsPerSec = 1000LL * 1000 * 1000;
constexpr int64_t kNsPerUs = 1000;

std::string_view ToStringView(const TraceBlobView& tbv) {
  return {reinterpret_cast<const char*>(tbv.data()), tbv.size()};
}

// The largest number of whole seconds since the epoch for which
// `seconds * kNsPerSec` cannot overflow int64_t. Anything larger is
// rejected outright rather than silently saturating/overflowing; genuine
// strace -ttt output is always vastly smaller than this (it's a real
// wall-clock date), so this only ever rejects garbage input.
constexpr int64_t kMaxEpochSeconds =
    std::numeric_limits<int64_t>::max() / kNsPerSec;

// Parses a `-ttt` "seconds[.microseconds]" timestamp (Unix epoch) into
// nanoseconds. Returns std::nullopt for anything else, including `-t`/`-tt`
// "HH:MM:SS[.ffffff]" time-of-day timestamps: those contain no date and
// cannot be safely reinterpreted as an absolute point in time (see
// StraceLine's header comment), so callers must reject them rather than
// silently treat the pre-midnight offset as an epoch time.
std::optional<int64_t> ParseEpochTimestamp(std::string_view s) {
  if (s.find(':') != std::string_view::npos) {
    return std::nullopt;
  }

  size_t dot = s.find('.');
  std::string_view whole = s.substr(0, dot);
  std::string_view frac =
      dot == std::string_view::npos ? std::string_view() : s.substr(dot + 1);

  auto seconds = base::StringViewToInt64(base::StringView(whole));
  // Reject negative and out-of-range values explicitly. StringViewToInt64
  // parses a leading '-' (this is not a `-t`/`-tt` line, which is already
  // excluded by the ':' check above, but garbage/adversarial input could
  // still supply one), so a negative value must be rejected rather than
  // fed into the multiplication below. A digit run long enough to overflow
  // int64_t returns nullopt (caught by `!seconds`), but a value that fits
  // in int64_t yet exceeds kMaxEpochSeconds would still overflow
  // `*seconds * kNsPerSec` (undefined behaviour), so guard that too. A real
  // strace timestamp is always a small, non-negative number of seconds.
  if (!seconds || *seconds < 0 || *seconds > kMaxEpochSeconds)
    return std::nullopt;

  int64_t ns = *seconds * kNsPerSec;
  if (!frac.empty()) {
    auto us = base::StringViewToInt64(base::StringView(frac));
    // Reject a negative fractional part (it would otherwise silently move
    // the timestamp backwards) and one out of the microsecond range. strace
    // only ever prints up to 6 digits of microseconds here.
    if (!us || *us < 0 || *us > 999999)
      return std::nullopt;
    // frac is microseconds regardless of how many digits strace prints.
    ns += *us * kNsPerUs;
  }
  return ns;
}

}  // namespace

ParseStraceLineResult ParseStraceLine(std::string_view line) {
  std::string_view rest = base::TrimWhitespace(line);
  if (rest.empty() || rest[0] == '-' /* "--- SIGCHLD ... ---" */ ||
      rest[0] == '+' /* "+++ exited with 0 +++" */) {
    return {};
  }

  StraceLine out;

  // Optional leading pid (present with -f/-ff): "1234 1700000000.000000 ...".
  // A bare digit run is ambiguous with an integral (no-fraction) `-ttt`
  // timestamp like "1700000000 read(...)", so length disambiguates: Unix
  // epoch seconds have been (and will remain, until the year 2286) 10
  // digits, while Linux's pid_max tops out at 4194304 (7 digits) even at
  // its highest configurable value. A 10+ digit run is therefore always the
  // timestamp, never a pid, and is left for the block below.
  {
    size_t sp = rest.find(' ');
    if (sp != std::string_view::npos) {
      std::string_view head = rest.substr(0, sp);
      bool all_digits = !head.empty() && head.size() < 10;
      for (char c : head) {
        if (!isdigit(static_cast<unsigned char>(c))) {
          all_digits = false;
          break;
        }
      }
      if (all_digits) {
        auto pid = base::StringViewToNumber<uint32_t>(base::StringView(head));
        if (!pid)
          return {};
        out.pid = *pid;
        rest = base::TrimWhitespace(rest.substr(sp + 1));
      }
    }
  }

  // Timestamp: a run of digits optionally followed by ".digits", ending at
  // the next space. Only the -ttt (Unix epoch) form is accepted; see
  // ParseEpochTimestamp.
  {
    size_t sp = rest.find(' ');
    if (sp == std::string_view::npos)
      return {};
    std::string_view ts_tok = rest.substr(0, sp);
    auto ts = ParseEpochTimestamp(ts_tok);
    if (!ts) {
      // A `-t`/`-tt` timestamp ("HH:MM:SS[.ffffff]") is the one case worth
      // distinguishing from "not a syscall line at all": it's a syscall
      // line, just in an unsupported format, so it gets a specific,
      // actionable stat rather than the generic parse-failure one.
      ParseStraceLineResult result;
      result.unsupported_timestamp_format =
          ts_tok.find(':') != std::string_view::npos;
      return result;
    }
    out.epoch_ns = *ts;
    rest = base::TrimWhitespace(rest.substr(sp + 1));
  }

  if (rest.empty())
    return {};

  // "<... syscall resumed> rest-of-args) = ret"
  constexpr std::string_view kResumedPrefix = "<... ";
  bool resumed = false;
  if (rest.substr(0, kResumedPrefix.size()) == kResumedPrefix) {
    size_t name_start = kResumedPrefix.size();
    size_t name_end = rest.find(" resumed>", name_start);
    if (name_end == std::string_view::npos)
      return {};
    out.syscall = std::string(rest.substr(name_start, name_end - name_start));
    resumed = true;
    rest = base::TrimWhitespace(
        rest.substr(name_end + std::string_view(" resumed>").size()));
  } else {
    size_t paren = rest.find('(');
    if (paren == std::string_view::npos)
      return {};
    out.syscall = std::string(rest.substr(0, paren));
    rest = rest.substr(paren + 1);
  }

  // "<unfinished ...>" has no closing paren/return value.
  constexpr std::string_view kUnfinished = "<unfinished ...>";
  size_t unfinished_pos = rest.find(kUnfinished);
  if (unfinished_pos != std::string_view::npos) {
    out.args =
        std::string(base::TrimWhitespace(rest.substr(0, unfinished_pos)));
    // Strip a trailing comma left over from "arg1, arg2, <unfinished ...>".
    while (!out.args.empty() && (out.args.back() == ',')) {
      out.args.pop_back();
    }
    // A line that is both resumed *and* unfinished ("<... foo resumed> args
    // <unfinished ...>") ends the prior call and immediately begins another.
    out.kind = resumed ? StraceEventKind::kResumedThenUnfinished
                       : StraceEventKind::kUnfinished;
    return ParseStraceLineResult{std::move(out)};
  }

  // Otherwise this call carries a return value and so is complete: either a
  // resumed call's tail ("<... foo resumed> ...) = ret") or a plain
  // single-line call ("foo(...) = ret").
  out.kind = resumed ? StraceEventKind::kResumed : StraceEventKind::kComplete;

  // The return value itself may contain further parens (e.g. "-1 ENOENT (No
  // such file or directory)"), so anchor on a ')' followed by whitespace and
  // then '=' — not the last ')' in the line — to separate the call's own
  // closing paren from its return value. strace right-aligns the '=' column
  // with spaces (e.g. "close(2)        = 0"), so the gap between ')' and '='
  // cannot be assumed to be exactly one space.
  size_t close_paren = std::string_view::npos;
  for (size_t pos = rest.find(')'); pos != std::string_view::npos;
       pos = rest.find(')', pos + 1)) {
    size_t after = pos + 1;
    while (after < rest.size() && base::IsSpace(rest[after]))
      ++after;
    if (after < rest.size() && rest[after] == '=') {
      close_paren = pos;
      break;
    }
  }
  if (close_paren == std::string_view::npos) {
    // A resumed line whose original call had no args left, e.g.
    // "<... read resumed>) = 3" collapses to just "= 3" after we've already
    // stripped the leading ")" as part of the resumed-prefix parsing above.
    size_t eq = rest.find('=');
    if (eq == std::string_view::npos)
      return {};
    out.return_value = std::string(base::TrimWhitespace(rest.substr(eq + 1)));
    return ParseStraceLineResult{std::move(out)};
  }

  out.args = std::string(base::TrimWhitespace(rest.substr(0, close_paren)));
  size_t eq = rest.find('=', close_paren + 1);
  out.return_value = std::string(base::TrimWhitespace(rest.substr(eq + 1)));
  return ParseStraceLineResult{std::move(out)};
}

bool IsStraceFormatTrace(const uint8_t* ptr, size_t size) {
  std::string_view str(reinterpret_cast<const char*>(ptr), size);
  size_t nl = str.find('\n');
  std::string_view first_line =
      nl == std::string_view::npos ? str : str.substr(0, nl);
  return ParseStraceLine(first_line).line.has_value();
}

StraceTraceTokenizer::StraceTraceTokenizer(TraceProcessorContext* ctx)
    : context_(ctx),
      stream_(
          ctx->sorter->CreateStream(std::make_unique<StraceTraceParser>(ctx))) {
}
StraceTraceTokenizer::~StraceTraceTokenizer() = default;

base::Status StraceTraceTokenizer::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));
  for (;;) {
    auto it = reader_.GetIterator();
    auto r = it.MaybeFindAndRead('\n');
    if (!r) {
      return base::OkStatus();
    }
    std::string_view line = ToStringView(*r);
    reader_.PopFrontUntil(it.file_offset());

    ParseStraceLineResult result = ParseStraceLine(line);
    if (!result.line) {
      // Not every line in an strace log is a syscall (signal delivery,
      // process exit banners, etc). Log it and skip rather than treating
      // the whole trace as invalid; a `-t`/`-tt` timestamp we intentionally
      // don't support gets a more specific, actionable message.
      context_->import_logs_tracker->RecordTokenizationLog(
          result.unsupported_timestamp_format
              ? stats::strace_unsupported_timestamp_format
              : stats::strace_parse_failure,
          it.file_offset());
      continue;
    }
    StraceLine& parsed = *result.line;

    std::optional<int64_t> trace_ts =
        context_->clock_tracker->ConvertDefaultClockToTraceTime(
            parsed.epoch_ns);
    if (!trace_ts) {
      continue;
    }

    // Without `-f`/`-ff` strace prints no pid, so there is nothing to
    // attribute the event to: any synthetic tid we invent here would
    // collide with real threads when traces are merged. Reject the line
    // with an actionable error instead of guessing.
    if (!parsed.pid) {
      context_->import_logs_tracker->RecordTokenizationLog(
          stats::strace_missing_pid, it.file_offset());
      continue;
    }

    StraceEvent evt;
    evt.tid = *parsed.pid;
    evt.syscall_name_id =
        context_->storage->InternString(base::StringView(parsed.syscall));
    if (!parsed.args.empty()) {
      evt.args_id =
          context_->storage->InternString(base::StringView(parsed.args));
    }
    if (parsed.return_value) {
      evt.return_value_id = context_->storage->InternString(
          base::StringView(*parsed.return_value));
    }
    evt.kind = parsed.kind;

    stream_->Push(*trace_ts, evt);
  }
}

}  // namespace perfetto::trace_processor::strace_importer
