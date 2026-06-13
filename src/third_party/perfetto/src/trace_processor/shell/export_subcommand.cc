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
  return "Export trace to a database file.";
}

const char* ExportSubcommand::usage_args() const {
  return "<format> -o FILE <trace_file>";
}

const char* ExportSubcommand::detailed_help() const {
  return R"(Load a trace and export it to a database file.

Currently the only supported format is "sqlite", which exports all trace
processor tables to a SQLite database. The format is the first positional
argument, and -o specifies the output path.)";
}

std::vector<FlagSpec> ExportSubcommand::GetFlags() {
  return {
      StringFlag("output", 'o', "FILE", "Output file path.", &output_path_),
  };
}

base::Status ExportSubcommand::Run(const SubcommandContext& ctx) {
  // First positional arg is the format.
  if (ctx.positional_args.empty()) {
    return base::ErrStatus("export: must specify format (sqlite)");
  }
  const std::string& format = ctx.positional_args[0];

  if (format != "sqlite") {
    return base::ErrStatus("export: unknown format '%s' (expected sqlite)",
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

  auto config = BuildConfig(*ctx.global, ctx.platform);
  ASSIGN_OR_RETURN(auto tp,
                   SetupTraceProcessor(*ctx.global, config, ctx.platform));
  RETURN_IF_ERROR(LoadTraceFile(tp.get(), ctx.platform, trace_file).status());

  return ExportTraceToDatabase(tp.get(), output_path_);
}

}  // namespace perfetto::trace_processor::shell
