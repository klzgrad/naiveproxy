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

#include "src/trace_processor/shell/metrics_subcommand.h"

#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/interactive.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/metrics.h"
#include "src/trace_processor/shell/query.h"
#include "src/trace_processor/shell/subcommand.h"

namespace perfetto::trace_processor::shell {

const char* MetricsSubcommand::name() const {
  return "metrics";
}

const char* MetricsSubcommand::description() const {
  return "Run v1 metrics (deprecated; use 'summarize --metrics-v2').";
}

const char* MetricsSubcommand::usage_args() const {
  return "<trace_file>";
}

const char* MetricsSubcommand::detailed_help() const {
  return R"(Run v1 trace processor metrics. This system is deprecated; prefer
'summarize --metrics-v2' for new workflows.

Metrics are specified by name with --run (comma-separated). Use
--output to control the format (text proto, binary proto, or JSON).)";
}

std::vector<FlagSpec> MetricsSubcommand::GetFlags() {
  return {
      StringFlag("run", '\0', "NAMES", "Comma-separated metric names.",
                 &metric_names_),
      StringFlag("pre", '\0', "FILE", "SQL file before metrics.", &pre_path_),
      StringFlag("output", '\0', "FORMAT", "Output format (binary|text|json).",
                 &metric_output_),
      StringFlag("post-query", '\0', "FILE", "SQL file after metrics.",
                 &post_query_path_),
      StringFlag("perf-file", '\0', "FILE", "Write perf timing data to FILE.",
                 &perf_file_),
      BoolFlag("interactive", 'i', "Start interactive shell after metrics.",
               &interactive_),
  };
}

base::Status MetricsSubcommand::Run(const SubcommandContext& ctx) {
  if (metric_names_.empty()) {
    return base::ErrStatus("metrics: --run is required");
  }

  if (ctx.positional_args.empty()) {
    return base::ErrStatus("metrics: trace file is required");
  }
  std::string trace_file = ctx.positional_args[0];

  // Metric extensions and their descriptor pool are pre-populated in
  // GlobalOptions; SetupTraceProcessor loads them into TP.
  auto config = BuildConfig(*ctx.global, ctx.platform);
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));

  PERFETTO_CHECK(ctx.global->metric_descriptor_pool);
  auto& pool = *ctx.global->metric_descriptor_pool;

  ASSIGN_OR_RETURN(auto t_load,
                   LoadTraceFile(tp.get(), ctx.platform, trace_file));

  // Pre-metrics query.
  if (!pre_path_.empty()) {
    RETURN_IF_ERROR(RunQueriesFromFile(tp.get(), pre_path_, false));
  }

  // Load and run metrics.
  std::vector<MetricNameAndPath> metrics;
  RETURN_IF_ERROR(LoadMetrics(tp.get(), metric_names_, pool, metrics));

  MetricV1OutputFormat format = MetricV1OutputFormat::kTextProto;
  if (metric_output_ == "binary") {
    format = MetricV1OutputFormat::kBinaryProto;
  } else if (metric_output_ == "json") {
    format = MetricV1OutputFormat::kJson;
  }

  base::TimeNanos t_query_start = base::GetWallTimeNs();
  RETURN_IF_ERROR(RunMetrics(tp.get(), metrics, format));

  // Post-query.
  if (!post_query_path_.empty()) {
    RETURN_IF_ERROR(RunQueriesFromFile(tp.get(), post_query_path_, true));
  }
  base::TimeNanos t_query = base::GetWallTimeNs() - t_query_start;

  if (!perf_file_.empty()) {
    RETURN_IF_ERROR(PrintPerfFile(perf_file_, t_load, t_query));
  }

  if (interactive_) {
    RETURN_IF_ERROR(StartInteractiveShell(
        tp.get(), InteractiveOptions{20u, format, ctx.global->metric_extensions,
                                     metrics, &pool}));
  }

  RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), ctx.global->metatrace_path));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
