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

#include "src/trace_processor/shell/export_subcommand.h"

#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/shell_utils.h"
#include "src/trace_processor/shell/subcommand.h"

namespace perfetto::trace_processor::shell {

const char* ExportSubcommand::name() const {
  return "export";
}

const char* ExportSubcommand::description() const {
  return "Export the contents of Trace Processor.";
}

const char* ExportSubcommand::usage_args() const {
  return "<format> -o FILE <trace_file>";
}

const char* ExportSubcommand::detailed_help() const {
  return R"(Load a trace and export it to a file.

Supported formats:
  sqlite      Exports all SQL-visible tables and views to SQLite.
  arrow_tar   Exports static tables as a cross-version-compatible tar of Arrow
              files for external consumers. It cannot be loaded back into Trace
              Processor.
  perfetto    Exports static tables that a fresh Trace Processor instance from
              the same version can load. A different version may also load the
              archive, but this is not guaranteed.

The exact contents exported are defined by the selected format.

The format is the first positional argument, and -o specifies the output
path.)";
}

std::vector<FlagSpec> ExportSubcommand::GetFlags() {
  return {
      StringFlag("output", 'o', "FILE", "Output file path.", &output_path_),
  };
}

base::Status ExportSubcommand::Run(const SubcommandContext& ctx) {
  RETURN_IF_ERROR(RejectExtraPositionals(ctx, "export", 2));
  // First positional arg is the format.
  if (ctx.positional_args.empty()) {
    return base::ErrStatus(
        "export: must specify format (sqlite, arrow_tar, or perfetto)");
  }
  const std::string& format = ctx.positional_args[0];

  if (format != "sqlite" && format != "arrow_tar" && format != "perfetto") {
    return base::ErrStatus(
        "export: unknown format '%s' (expected sqlite, arrow_tar, or "
        "perfetto)",
        format.c_str());
  }

  if (output_path_.empty()) {
    return base::ErrStatus("export: -o FILE is required");
  }

  // Trace file is the second positional argument.
  if (ctx.positional_args.size() < 2) {
    return base::ErrStatus("export: trace file is required");
  }
  std::string trace_file = ctx.positional_args[1];

  ASSIGN_OR_RETURN(Config config, BuildConfig(*ctx.global, ctx.platform));
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));
  RETURN_IF_ERROR(LoadTraceFile(tp.get(), ctx.platform, trace_file).status());

  TraceProcessor::ExportFormat export_format;
  if (format == "sqlite") {
    export_format = TraceProcessor::ExportFormat::kSqlite;
  } else if (format == "arrow_tar") {
    export_format = TraceProcessor::ExportFormat::kArrowTar;
  } else {
    export_format = TraceProcessor::ExportFormat::kPerfetto;
  }
  return ExportTrace(tp.get(), export_format, output_path_);
}

}  // namespace perfetto::trace_processor::shell
