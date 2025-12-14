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

#include "src/traceconv/trace_to_systrace.h"

#include <stdio.h>

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/fixed_string_writer.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/traceconv/utils.h"

namespace perfetto {
namespace trace_to_text {

namespace {

const char kProcessDumpHeader[] =
    "\"androidProcessDump\": "
    "\"PROCESS DUMP\\nUSER           PID  PPID     VSZ    RSS WCHAN  "
    "PC S NAME                        COMM                       \\n";

const char kThreadHeader[] = "USER           PID   TID CMD \\n";

const char kProcessDumpFooter[] = "\"";

const char kSystemTraceEvents[] = "  \"systemTraceEvents\": \"";

const char kFtraceHeader[] =
    "# tracer: nop\n"
    "#\n"
    "# entries-in-buffer/entries-written: 30624/30624   #P:4\n"
    "#\n"
    "#                                      _-----=> irqs-off\n"
    "#                                     / _----=> need-resched\n"
    "#                                    | / _---=> hardirq/softirq\n"
    "#                                    || / _--=> preempt-depth\n"
    "#                                    ||| /     delay\n"
    "#           TASK-PID    TGID   CPU#  ||||    TIMESTAMP  FUNCTION\n"
    "#              | |        |      |   ||||       |         |\n";

const char kFtraceJsonHeader[] =
    "# tracer: nop\\n"
    "#\\n"
    "# entries-in-buffer/entries-written: 30624/30624   #P:4\\n"
    "#\\n"
    "#                                      _-----=> irqs-off\\n"
    "#                                     / _----=> need-resched\\n"
    "#                                    | / _---=> hardirq/softirq\\n"
    "#                                    || / _--=> preempt-depth\\n"
    "#                                    ||| /     delay\\n"
    "#           TASK-PID    TGID   CPU#  ||||    TIMESTAMP  FUNCTION\\n"
    "#              | |        |      |   ||||       |         |\\n";

// The legacy trace viewer requires a clock sync marker to tie ftrace and
// userspace clocks together. Trace processor already aligned these clocks, so
// we just emit a clock sync for an equality mapping.
const char kSystemTraceEventsFooter[] =
    "\\n<...>-12345 (-----) [000] ...1 0.000000: tracing_mark_write: "
    "trace_event_clock_sync: parent_ts=0\\n\"";

inline void FormatProcess(uint32_t pid,
                          uint32_t ppid,
                          const base::StringView& name,
                          base::FixedStringWriter* writer) {
  writer->AppendLiteral("root             ");
  writer->AppendInt(pid);
  writer->AppendLiteral("     ");
  writer->AppendInt(ppid);
  writer->AppendLiteral("   00000   000 null 0000000000 S ");
  writer->AppendString(name);
  writer->AppendLiteral("         null");
}

inline void FormatThread(uint32_t tid,
                         uint32_t tgid,
                         const base::StringView& name,
                         base::FixedStringWriter* writer) {
  writer->AppendLiteral("root         ");
  writer->AppendInt(tgid);
  writer->AppendChar(' ');
  writer->AppendInt(tid);
  writer->AppendChar(' ');
  if (name.empty()) {
    writer->AppendLiteral("<...>");
  } else {
    writer->AppendString(name);
  }
}

class QueryWriter {
 public:
  QueryWriter(trace_processor::TraceProcessor* tp, TraceWriter* trace_writer)
      : tp_(tp),
        buffer_(base::PagedMemory::Allocate(kBufferSize)),
        global_writer_(static_cast<char*>(buffer_.Get()), kBufferSize),
        trace_writer_(trace_writer) {}

  template <typename Callback>
  bool RunQuery(const std::string& sql, Callback callback) {
    char buffer[2048];
    auto iterator = tp_->ExecuteQuery(sql);
    for (uint32_t rows = 0; iterator.Next(); rows++) {
      base::FixedStringWriter line_writer(buffer, base::ArraySize(buffer));
      callback(&iterator, &line_writer);

      if (global_writer_.pos() + line_writer.pos() >= global_writer_.size()) {
        fprintf(stderr, "Writing row %" PRIu32 "%c", rows, kProgressChar);
        auto str = global_writer_.GetStringView();
        trace_writer_->Write(str.data(), str.size());
        global_writer_.reset();
      }
      global_writer_.AppendStringView(line_writer.GetStringView());
    }

    // Check if we have an error in the iterator and print if so.
    auto status = iterator.Status();
    if (!status.ok()) {
      PERFETTO_ELOG("Error while writing systrace %s", status.c_message());
      return false;
    }

    // Flush any dangling pieces in the global writer.
    auto str = global_writer_.GetStringView();
    trace_writer_->Write(str.data(), str.size());
    global_writer_.reset();
    return true;
  }

 private:
  static constexpr uint32_t kBufferSize = 1024u * 1024u * 16u;

  trace_processor::TraceProcessor* tp_ = nullptr;
  base::PagedMemory buffer_;
  base::FixedStringWriter global_writer_;
  TraceWriter* trace_writer_;
};

int ExtractRawEvents(TraceWriter* trace_writer,
                     QueryWriter& q_writer,
                     bool wrapped_in_json,
                     Keep truncate_keep) {
  using trace_processor::Iterator;

  static const char kRawEventsCountSql[] = "select count(1) from ftrace_event";
  uint32_t raw_events = 0;
  auto e_callback = [&raw_events](Iterator* it, base::FixedStringWriter*) {
    raw_events = static_cast<uint32_t>(it->Get(0).long_value);
  };
  if (!q_writer.RunQuery(kRawEventsCountSql, e_callback))
    return 1;

  if (raw_events == 0) {
    if (!wrapped_in_json) {
      // Write out the normal header even if we won't actually have
      // any events under it.
      trace_writer->Write(kFtraceHeader);
    }
    return 0;
  }

  fprintf(stderr, "Converting ftrace events%c", kProgressChar);
  fflush(stderr);

  auto raw_callback = [wrapped_in_json](Iterator* it,
                                        base::FixedStringWriter* writer) {
    const char* line = it->Get(0 /* col */).string_value;
    if (wrapped_in_json) {
      for (uint32_t i = 0; line[i] != '\0'; i++) {
        char c = line[i];
        switch (c) {
          case '\n':
            writer->AppendLiteral("\\n");
            break;
          case '\f':
            writer->AppendLiteral("\\f");
            break;
          case '\b':
            writer->AppendLiteral("\\b");
            break;
          case '\r':
            writer->AppendLiteral("\\r");
            break;
          case '\t':
            writer->AppendLiteral("\\t");
            break;
          case '\\':
            writer->AppendLiteral("\\\\");
            break;
          case '"':
            writer->AppendLiteral("\\\"");
            break;
          default:
            writer->AppendChar(c);
            break;
        }
      }
      writer->AppendChar('\\');
      writer->AppendChar('n');
    } else {
      writer->AppendString(line);
      writer->AppendChar('\n');
    }
  };

  // An estimate of 130b per ftrace event, allowing some space for the processes
  // and threads.
  const uint32_t max_ftrace_events = (140 * 1024 * 1024) / 130;

  static const char kRawEventsQuery[] =
      "select to_ftrace(id) from ftrace_event";

  // 1. Write the appropriate header for the file type.
  if (wrapped_in_json) {
    trace_writer->Write(",\n");
    trace_writer->Write(kSystemTraceEvents);
    trace_writer->Write(kFtraceJsonHeader);
  } else {
    trace_writer->Write(kFtraceHeader);
  }

  // 2. Write the actual events.
  if (truncate_keep == Keep::kEnd && raw_events > max_ftrace_events) {
    base::StackString<150> end_truncate("%s limit %u offset %u",
                                        kRawEventsQuery, max_ftrace_events,
                                        raw_events - max_ftrace_events);
    if (!q_writer.RunQuery(end_truncate.ToStdString(), raw_callback))
      return 1;
  } else if (truncate_keep == Keep::kStart) {
    base::StackString<150> start_truncate("%s limit %u", kRawEventsQuery,
                                          max_ftrace_events);
    if (!q_writer.RunQuery(start_truncate.ToStdString(), raw_callback))
      return 1;
  } else {
    if (!q_writer.RunQuery(kRawEventsQuery, raw_callback))
      return 1;
  }

  // 3. Write the footer for JSON.
  if (wrapped_in_json)
    trace_writer->Write(kSystemTraceEventsFooter);

  return 0;
}

}  // namespace

int TraceToSystrace(std::istream* input,
                    std::ostream* output,
                    bool ctrace,
                    Keep truncate_keep,
                    bool full_sort) {
  std::unique_ptr<TraceWriter> trace_writer(
      ctrace ? new DeflateTraceWriter(output) : new TraceWriter(output));

  trace_processor::Config config;
  config.sorting_mode = full_sort
                            ? trace_processor::SortingMode::kForceFullSort
                            : trace_processor::SortingMode::kDefaultHeuristics;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  if (!ReadTraceUnfinalized(tp.get(), input))
    return 1;
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    return 1;
  }

  if (ctrace)
    *output << "TRACE:\n";

  return ExtractSystrace(tp.get(), trace_writer.get(),
                         /*wrapped_in_json=*/false, truncate_keep);
}

int ExtractSystrace(trace_processor::TraceProcessor* tp,
                    TraceWriter* trace_writer,
                    bool wrapped_in_json,
                    Keep truncate_keep) {
  using trace_processor::Iterator;

  QueryWriter q_writer(tp, trace_writer);
  if (wrapped_in_json) {
    trace_writer->Write(kProcessDumpHeader);

    // Write out all the processes in the trace.
    // TODO(lalitm): change this query to actually use ppid when it is exposed
    // by the process table.
    static const char kPSql[] = "select pid, 0 as ppid, name from process";
    auto p_callback = [](Iterator* it, base::FixedStringWriter* writer) {
      uint32_t pid = static_cast<uint32_t>(it->Get(0 /* col */).long_value);
      uint32_t ppid = static_cast<uint32_t>(it->Get(1 /* col */).long_value);
      const auto& name_col = it->Get(2 /* col */);
      auto name_view = name_col.type == trace_processor::SqlValue::kString
                           ? base::StringView(name_col.string_value)
                           : base::StringView();
      FormatProcess(pid, ppid, name_view, writer);
    };
    if (!q_writer.RunQuery(kPSql, p_callback))
      return 1;

    trace_writer->Write(kThreadHeader);

    // Write out all the threads in the trace.
    static const char kTSql[] =
        "select tid, COALESCE(upid, 0), thread.name "
        "from thread left join process using (upid)";
    auto t_callback = [](Iterator* it, base::FixedStringWriter* writer) {
      uint32_t tid = static_cast<uint32_t>(it->Get(0 /* col */).long_value);
      uint32_t tgid = static_cast<uint32_t>(it->Get(1 /* col */).long_value);
      const auto& name_col = it->Get(2 /* col */);
      auto name_view = name_col.type == trace_processor::SqlValue::kString
                           ? base::StringView(name_col.string_value)
                           : base::StringView();
      FormatThread(tid, tgid, name_view, writer);
    };
    if (!q_writer.RunQuery(kTSql, t_callback))
      return 1;

    trace_writer->Write(kProcessDumpFooter);
  }
  return ExtractRawEvents(trace_writer, q_writer, wrapped_in_json,
                          truncate_keep);
}

}  // namespace trace_to_text
}  // namespace perfetto
