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

#include "src/trace_processor/shell/summarize_subcommand.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/interactive.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/query.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/trace_processor/trace_summary/summary.h"  // no-include-violation-check

namespace perfetto::trace_processor::shell {
namespace {

TraceSummarySpecBytes::Format GuessSummarySpecFormat(
    const std::string& path,
    const std::string& content) {
  if (base::EndsWith(path, ".pb")) {
    return TraceSummarySpecBytes::Format::kBinaryProto;
  }
  if (base::EndsWith(path, ".textproto")) {
    return TraceSummarySpecBytes::Format::kTextProto;
  }
  // Content-sniffing fallback: if the first 128 bytes are all printable or
  // whitespace, assume text proto; otherwise binary.
  std::string_view prefix(content.c_str(),
                          std::min<size_t>(content.size(), 128));
  if (std::all_of(prefix.begin(), prefix.end(),
                  [](char c) { return std::isspace(c) || std::isprint(c); })) {
    return TraceSummarySpecBytes::Format::kTextProto;
  }
  return TraceSummarySpecBytes::Format::kBinaryProto;
}

}  // namespace

const char* SummarizeSubcommand::name() const {
  return "summarize";
}

const char* SummarizeSubcommand::description() const {
  return "Run trace summarization.";
}

const char* SummarizeSubcommand::usage_args() const {
  return "<trace_file> [spec_file ...]";
}

const char* SummarizeSubcommand::detailed_help() const {
  return R"(Compute a trace summary using spec files and/or built-in metrics.

Spec files (textproto or binary proto) define which summary computations
to run. They are passed as positional arguments after the trace file.
Use --metrics-v2 to run built-in v2 metrics (pass "all" or comma-separated
IDs). Output defaults to text proto; use --format binary for binary proto.)";
}

std::vector<FlagSpec> SummarizeSubcommand::GetFlags() {
  // Note: --spec is multi-valued but FlagSpec handlers are called per
  // occurrence, so we accumulate in a string with comma separation and
  // split later. This matches the old getopt behavior where --spec could
  // be repeated.
  return {
      StringFlag("metrics-v2", '\0', "IDS", "Metric IDs, or \"all\".",
                 &metrics_v2_),
      StringFlag("metadata-query", '\0', "ID", "Metadata query ID.",
                 &metadata_query_),
      StringFlag("format", '\0', "FORMAT", "Output format (text or binary).",
                 &output_format_),
      StringFlag("post-query", '\0', "FILE",
                 "SQL file to run after summarization.", &post_query_path_),
      StringFlag("perf-file", '\0', "FILE", "Write perf timing data to FILE.",
                 &perf_file_),
      BoolFlag("interactive", 'i',
               "Start interactive shell after summarization.", &interactive_),
  };
}

base::Status SummarizeSubcommand::Run(const SubcommandContext& ctx) {
  if (ctx.positional_args.empty()) {
    return base::ErrStatus("summarize: trace file is required");
  }
  std::string trace_file = ctx.positional_args[0];

  // Collect spec paths from remaining positional args (if any).
  std::vector<std::string> spec_paths;
  for (size_t i = 1; i < ctx.positional_args.size(); ++i) {
    spec_paths.push_back(ctx.positional_args[i]);
  }

  auto config = BuildConfig(*ctx.global, ctx.platform);
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));
  ASSIGN_OR_RETURN(auto t_load,
                   LoadTraceFile(tp.get(), ctx.platform, trace_file));

  // Load spec files.
  std::vector<std::string> spec_content;
  spec_content.reserve(spec_paths.size());
  for (const auto& s : spec_paths) {
    spec_content.emplace_back();
    if (!base::ReadFile(s, &spec_content.back())) {
      return base::ErrStatus("Unable to read summary spec file %s", s.c_str());
    }
  }

  std::vector<TraceSummarySpecBytes> specs;
  specs.reserve(spec_paths.size());
  for (uint32_t i = 0; i < spec_paths.size(); ++i) {
    auto format = GuessSummarySpecFormat(spec_paths[i], spec_content[i]);
    specs.emplace_back(TraceSummarySpecBytes{
        reinterpret_cast<const uint8_t*>(spec_content[i].data()),
        spec_content[i].size(),
        format,
    });
  }

  TraceSummaryComputationSpec computation_config;
  if (metrics_v2_.empty()) {
    computation_config.v2_metric_ids = std::vector<std::string>();
  } else if (base::CaseInsensitiveEqual(metrics_v2_, "all")) {
    computation_config.v2_metric_ids = std::nullopt;
  } else {
    computation_config.v2_metric_ids = base::SplitString(metrics_v2_, ",");
  }
  computation_config.metadata_query_id =
      metadata_query_.empty() ? std::nullopt
                              : std::make_optional(metadata_query_);

  TraceSummaryOutputSpec output_spec;
  if (output_format_ == "binary") {
    output_spec.format = TraceSummaryOutputSpec::Format::kBinaryProto;
  } else {
    output_spec.format = TraceSummaryOutputSpec::Format::kTextProto;
  }

  base::TimeNanos t_query_start = base::GetWallTimeNs();
  std::vector<uint8_t> output;
  RETURN_IF_ERROR(
      tp->Summarize(computation_config, specs, &output, output_spec));

  if (post_query_path_.empty()) {
    fwrite(output.data(), sizeof(char), output.size(), stdout);
  }

  if (!post_query_path_.empty()) {
    RETURN_IF_ERROR(RunQueriesFromFile(tp.get(), post_query_path_, true));
  }
  base::TimeNanos t_query = base::GetWallTimeNs() - t_query_start;

  if (!perf_file_.empty()) {
    RETURN_IF_ERROR(PrintPerfFile(perf_file_, t_load, t_query));
  }

  if (interactive_) {
    RETURN_IF_ERROR(StartInteractiveShell(
        tp.get(),
        InteractiveOptions{20u, MetricV1OutputFormat::kNone, {}, {}, nullptr}));
  }

  RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), ctx.global->metatrace_path));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
