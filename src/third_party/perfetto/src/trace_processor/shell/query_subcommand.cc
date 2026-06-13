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

#include "src/trace_processor/shell/query_subcommand.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/summarizer.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/protozero/text_to_proto/text_to_proto.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/interactive.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/query.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/trace_processor/trace_summary/trace_summary.descriptor.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <io.h>
#else
#include <unistd.h>
#endif
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && !defined(STDIN_FILENO)
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_HAS_SIGNAL_H() 1
#include <signal.h>
#else
#define PERFETTO_HAS_SIGNAL_H() 0
#endif

namespace perfetto::trace_processor::shell {
namespace {

// Returns true if the file at |path| with |content| should be treated as
// textproto rather than binary proto. Uses the same heuristic as the
// classic codepath: .pb → binary, .textproto → text, otherwise
// content-sniff the first 128 bytes (all printable/whitespace → text).
bool IsTextproto(const std::string& path, const std::string& content) {
  if (base::EndsWith(path, ".pb")) {
    return false;
  }
  if (base::EndsWith(path, ".textproto")) {
    return true;
  }
  std::string_view prefix(content.c_str(),
                          std::min<size_t>(content.size(), 128));
  return std::all_of(prefix.begin(), prefix.end(),
                     [](char c) { return std::isspace(c) || std::isprint(c); });
}

// Reads a TraceSummarySpec file and passes it to |summarizer|, converting
// textproto to binary proto when necessary.
base::Status LoadSpecIntoSummarizer(Summarizer* summarizer,
                                    const std::string& path) {
  std::string content;
  if (!base::ReadFile(path, &content)) {
    return base::ErrStatus("Unable to read spec file %s", path.c_str());
  }

  const uint8_t* spec_data;
  size_t spec_size;
  std::vector<uint8_t> binary_proto;
  if (IsTextproto(path, content)) {
    ASSIGN_OR_RETURN(binary_proto,
                     protozero::TextToProto(kTraceSummaryDescriptor.data(),
                                            kTraceSummaryDescriptor.size(),
                                            ".perfetto.protos.TraceSummarySpec",
                                            "-", std::string_view(content)));
    spec_data = binary_proto.data();
    spec_size = binary_proto.size();
  } else {
    spec_data = reinterpret_cast<const uint8_t*>(content.data());
    spec_size = content.size();
  }

  SummarizerUpdateSpecResult update_result;
  RETURN_IF_ERROR(summarizer->UpdateSpec(spec_data, spec_size, &update_result));
  for (const auto& q : update_result.queries) {
    if (q.error.has_value()) {
      return base::ErrStatus("Error in query '%s' from spec '%s': %s",
                             q.query_id.c_str(), path.c_str(),
                             q.error->c_str());
    }
  }
  return base::OkStatus();
}

}  // namespace

const char* QuerySubcommand::name() const {
  return "query";
}

const char* QuerySubcommand::description() const {
  return "Load a trace and run a SQL query.";
}

const char* QuerySubcommand::usage_args() const {
  return "<trace_file> [SQL]";
}

const char* QuerySubcommand::detailed_help() const {
  return R"(Run one or more SQL queries against a loaded trace file and print results.

SQL can be provided in three ways:
  1. Positional argument:  tp query trace.pb "SELECT ts FROM slice LIMIT 10"
  2. From a file:          tp query -f queries.sql trace.pb
  3. From stdin:           cat q.sql | tp query trace.pb

Multiple semicolon-separated statements are supported. Use -i to drop into
an interactive shell after the queries complete.

Advanced (for debugging/testing structured queries):
  --structured-query-id ID --summary-spec FILE [...]
  Executes a single structured query by ID from the given summary spec
  files. The spec files replace -f/stdin/positional SQL. Output is the
  query result table.)";
}

std::vector<FlagSpec> QuerySubcommand::GetFlags() {
  return {
      StringFlag("query-file", 'f', "FILE",
                 "Read SQL from FILE (use '-' for stdin).", &query_file_),
      StringFlag("structured-query-id", '\0', "ID",
                 "[Advanced] Run a single structured query by ID.",
                 &structured_query_id_),
      {"summary-spec", '\0', true, "FILE",
       "[Advanced] Summary spec file for structured queries (repeatable).",
       [this](const char* v) { structured_query_specs_.emplace_back(v); }},
      BoolFlag("interactive", 'i', "Start interactive shell after query.",
               &interactive_),
      BoolFlag("wide", 'W', "Double column width for output.", &wide_),
      StringFlag("perf-file", '\0', "FILE", "Write perf timing data to FILE.",
                 &perf_file_),
  };
}

base::Status QuerySubcommand::Run(const SubcommandContext& ctx) {
  if (ctx.positional_args.empty()) {
    return base::ErrStatus("query: trace file is required");
  }
  std::string trace_file = ctx.positional_args[0];

  // Advanced: structured query mode.
  if (!structured_query_id_.empty()) {
    return RunStructuredQuery(ctx, trace_file);
  }

  // Determine SQL source:
  //   1. Positional:  query trace.pb "SELECT ..."
  //   2. File:        query -f file.sql trace.pb
  //   3. Stdin flag:  query -f - trace.pb
  //   4. Stdin pipe:  query trace.pb < file.sql
  std::string sql;
  bool read_stdin =
      query_file_ == "-" || (query_file_.empty() && !isatty(STDIN_FILENO));
  if (ctx.positional_args.size() >= 2) {
    sql = ctx.positional_args[1];
  } else if (read_stdin) {
    if (!base::ReadFileDescriptor(STDIN_FILENO, &sql))
      return base::ErrStatus("query: failed to read SQL from stdin");
    query_file_.clear();
  }

  if (sql.empty() && query_file_.empty()) {
    return base::ErrStatus(
        "query: no SQL provided. Use positional arg, -f FILE, or pipe to "
        "stdin.");
  }

  auto config = BuildConfig(*ctx.global, ctx.platform);
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));
  ASSIGN_OR_RETURN(auto t_load,
                   LoadTraceFile(tp.get(), ctx.platform, trace_file));

  if (!query_file_.empty()) {
    if (!base::ReadFile(query_file_, &sql)) {
      return base::ErrStatus("query: unable to read file '%s'",
                             query_file_.c_str());
    }
  }
  PERFETTO_CHECK(!sql.empty());

#if PERFETTO_HAS_SIGNAL_H()
  static TraceProcessor* g_tp_for_signal_handler = tp.get();
  signal(SIGINT, [](int) { g_tp_for_signal_handler->InterruptQuery(); });
#endif

  base::TimeNanos t_query_start = base::GetWallTimeNs();
  auto status = RunQueries(tp.get(), sql, true);
  if (!status.ok()) {
    MaybeWriteMetatrace(tp.get(), ctx.global->metatrace_path);
    return status;
  }
  base::TimeNanos t_query = base::GetWallTimeNs() - t_query_start;

  if (!perf_file_.empty()) {
    RETURN_IF_ERROR(PrintPerfFile(perf_file_, t_load, t_query));
  }

  if (interactive_) {
    RETURN_IF_ERROR(StartInteractiveShell(
        tp.get(),
        InteractiveOptions{
            wide_ ? 40u : 20u, MetricV1OutputFormat::kNone, {}, {}, nullptr}));
  }

  RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), ctx.global->metatrace_path));
  return base::OkStatus();
}

base::Status QuerySubcommand::RunStructuredQuery(
    const SubcommandContext& ctx,
    const std::string& trace_file) {
  if (structured_query_specs_.empty()) {
    return base::ErrStatus(
        "query: --structured-query-id requires at least one --summary-spec");
  }

  auto config = BuildConfig(*ctx.global, ctx.platform);
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));
  ASSIGN_OR_RETURN(auto t_load,
                   LoadTraceFile(tp.get(), ctx.platform, trace_file));

  std::unique_ptr<Summarizer> summarizer;
  RETURN_IF_ERROR(tp->CreateSummarizer(&summarizer));
  for (const auto& path : structured_query_specs_) {
    RETURN_IF_ERROR(LoadSpecIntoSummarizer(summarizer.get(), path));
  }

  base::TimeNanos t_query_start = base::GetWallTimeNs();
  SummarizerQueryResult query_result;
  RETURN_IF_ERROR(summarizer->Query(structured_query_id_, &query_result));
  if (!query_result.exists) {
    return base::ErrStatus(
        "Structured query ID '%s' not found in the provided spec files",
        structured_query_id_.c_str());
  }

  RETURN_IF_ERROR(
      RunQueries(tp.get(), "SELECT * FROM " + query_result.table_name, true));
  base::TimeNanos t_query = base::GetWallTimeNs() - t_query_start;

  if (!perf_file_.empty()) {
    RETURN_IF_ERROR(PrintPerfFile(perf_file_, t_load, t_query));
  }
  RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), ctx.global->metatrace_path));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
