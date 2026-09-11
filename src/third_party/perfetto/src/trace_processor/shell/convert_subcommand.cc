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

#include "src/trace_processor/shell/convert_subcommand.h"

#include <cstdint>
#include <fstream>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/shell/convert_helpers.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/traceconv/trace_to_firefox.h"
#include "src/traceconv/trace_to_json.h"
#include "src/traceconv/trace_to_profile.h"
#include "src/traceconv/trace_to_systrace.h"
#include "src/traceconv/trace_to_text.h"

namespace perfetto::trace_processor::shell {

const char* ConvertSubcommand::name() const {
  return "convert";
}

const char* ConvertSubcommand::description() const {
  return "Convert trace format.";
}

const char* ConvertSubcommand::usage_args() const {
  return "<format> [input] [output]";
}

const char* ConvertSubcommand::detailed_help() const {
  return R"(Convert a trace between formats.

Formats:
  systrace              Convert to systrace HTML format
  json                  Convert to Chrome JSON format
  ctrace                Convert to compressed systrace format
  text                  Convert to human-readable text format
  profile               Convert profile data to pprof format
  firefox               Convert to Firefox profiler format

If no input file is given, reads from stdin.
If no output file is given, writes to stdout.

To symbolize/deobfuscate, decompress packets or convert a text proto to binary,
see the 'util' command. To create a self-contained bundle, see 'bundle'.)";
}

std::vector<FlagSpec> ConvertSubcommand::GetFlags() {
  return {
      StringFlag("truncate", 't', "start|end",
                 "Truncate trace to keep start or end.", &truncate_),
      BoolFlag("full-sort", '\0', "Force full trace sorting.", &full_sort_),
      StringFlag("pid", '\0', "PID", "Generate profiles for specific process.",
                 &pid_),
      StringFlag("timestamps", '\0', "T1,T2,...",
                 "Generate profiles for specific timestamps.", &timestamps_),
      BoolFlag("alloc", '\0', "Convert only allocator profile.", &alloc_),
      BoolFlag("perf", '\0', "Convert only perf profile.", &perf_),
      BoolFlag("java-heap", '\0', "Convert only heap graph profile.",
               &java_heap_),
      BoolFlag("no-annotations", '\0',
               "Don't add derived annotations to frames.", &no_annotations_),
      StringFlag("output-dir", '\0', "DIR", "Output directory for profiles.",
                 &output_dir_),
      BoolFlag("verbose", '\0', "Print more detailed output.", &verbose_),
      BoolFlag("skip-unknown", '\0',
               "Skip unknown fields when converting to text.", &skip_unknown_),
  };
}

base::Status ConvertSubcommand::Run(const SubcommandContext& ctx) {
  RETURN_IF_ERROR(RejectExtraPositionals(ctx, "convert", 3));
  if (ctx.positional_args.empty())
    return base::ErrStatus("convert: a format must be specified.");

  const std::string& format = ctx.positional_args[0];
  const std::string input_path =
      ctx.positional_args.size() > 1 ? ctx.positional_args[1] : "";
  const std::string output_path =
      ctx.positional_args.size() > 2 ? ctx.positional_args[2] : "";

  trace_to_text::Keep truncate_keep = trace_to_text::Keep::kAll;
  if (!truncate_.empty()) {
    if (truncate_ == "start") {
      truncate_keep = trace_to_text::Keep::kStart;
    } else if (truncate_ == "end") {
      truncate_keep = trace_to_text::Keep::kEnd;
    } else {
      return base::ErrStatus(
          "--truncate must specify whether to keep the 'end' or the 'start' "
          "of the trace.");
    }
  }

  uint64_t pid = 0;
  if (!pid_.empty()) {
    std::optional<uint64_t> parsed = base::StringToUInt64(pid_);
    if (!parsed)
      return base::ErrStatus("--pid must be a decimal integer.");
    pid = *parsed;
  }

  std::vector<uint64_t> timestamps;
  if (!timestamps_.empty()) {
    for (const std::string& ts : base::SplitString(timestamps_, ",")) {
      std::optional<uint64_t> parsed = base::StringToUInt64(ts);
      if (!parsed)
        return base::ErrStatus("--timestamps must be decimal integers.");
      timestamps.emplace_back(*parsed);
    }
  }

  std::optional<trace_to_text::ConversionMode> profile_type;
  if (alloc_)
    profile_type = trace_to_text::ConversionMode::kHeapProfile;
  if (perf_)
    profile_type = trace_to_text::ConversionMode::kPerfProfile;
  if (java_heap_)
    profile_type = trace_to_text::ConversionMode::kJavaHeapProfile;

  const bool is_profile = format == "profile";
  if (!is_profile && (pid != 0 || !timestamps.empty())) {
    return base::ErrStatus(
        "--pid and --timestamps are supported only for the 'profile' format.");
  }
  if (!is_profile && !output_dir_.empty()) {
    return base::ErrStatus(
        "--output-dir is supported only for the 'profile' format.");
  }

  std::ifstream input_file;
  std::istream* input = nullptr;
  RETURN_IF_ERROR(OpenConversionInput(input_path, &input_file, &input));

  std::ofstream output_file;
  std::ostream* output = nullptr;
  // ctrace is the only convert format that emits binary; the rest are
  // human-readable.
  const bool binary_output = format == "ctrace";
  RETURN_IF_ERROR(
      OpenConversionOutput(output_path, binary_output, &output_file, &output));

  if (format == "json") {
    RETURN_IF_ERROR(trace_to_text::TraceToJson(
        input, output, /*compress=*/false, truncate_keep, full_sort_));
  } else if (format == "systrace") {
    RETURN_IF_ERROR(trace_to_text::TraceToSystrace(
        input, output, /*ctrace=*/false, truncate_keep, full_sort_));
  } else if (format == "ctrace") {
    RETURN_IF_ERROR(trace_to_text::TraceToSystrace(
        input, output, /*ctrace=*/true, truncate_keep, full_sort_));
  } else if (format == "text" || format == "profile" || format == "firefox") {
    if (truncate_keep != trace_to_text::Keep::kAll) {
      return base::ErrStatus("--truncate is unsupported for the '%s' format.",
                             format.c_str());
    }
    if (full_sort_) {
      return base::ErrStatus("--full-sort is unsupported for the '%s' format.",
                             format.c_str());
    }
    if (format == "text") {
      trace_to_text::TraceToTextOptions options;
      options.skip_unknown_fields = skip_unknown_;
      RETURN_IF_ERROR(trace_to_text::TraceToText(input, output, options));
    } else if (format == "profile") {
      if (!output_path.empty()) {
        return base::ErrStatus(
            "output file is not supported for 'profile', use --output-dir "
            "instead.");
      }
      RETURN_IF_ERROR(trace_to_text::TraceToProfile(
          input, pid, timestamps, !no_annotations_, output_dir_, profile_type,
          verbose_));
    } else {  // firefox
      RETURN_IF_ERROR(trace_to_text::TraceToFirefoxProfile(input, output));
    }
  } else {
    return base::ErrStatus("convert: unknown format '%s'.", format.c_str());
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
