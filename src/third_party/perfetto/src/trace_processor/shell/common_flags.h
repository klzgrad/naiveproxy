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

#ifndef SRC_TRACE_PROCESSOR_SHELL_COMMON_FLAGS_H_
#define SRC_TRACE_PROCESSOR_SHELL_COMMON_FLAGS_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <google/protobuf/descriptor.h>

#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/metatrace_config.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/shell/metrics.h"
#include "src/trace_processor/shell/subcommand.h"

namespace perfetto::trace_processor {
class TraceProcessorShell_PlatformInterface;
}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::shell {

// Options shared across all subcommands (trace loading, metatrace, dev, etc.).
struct GlobalOptions {
  std::string trace_file;

  bool force_full_sort = false;
  bool no_ftrace_raw = false;
  bool analyze_trace_proto_content = false;
  bool crop_track_events = false;

  bool dev = false;
  std::vector<std::string> dev_flags;
  bool extra_checks = false;

  std::vector<std::string> sql_package_paths;
  std::vector<std::string> override_sql_package_paths;
  std::string override_stdlib_path;
  std::string register_files_dir;

  // Raw --metric-extension strings collected during flag parsing.
  std::vector<std::string> raw_metric_v1_extensions;
  // Parsed metric extensions and their descriptor pool. Populated by
  // ParseGlobalMetricExtensions() which must be called after flag parsing
  // and before BuildConfig()/SetupTraceProcessor().
  std::vector<MetricExtension> metric_extensions;
  std::unique_ptr<google::protobuf::DescriptorPool> metric_descriptor_pool;

  std::string metatrace_path;
  size_t metatrace_buffer_capacity = 0;
  metatrace::MetatraceCategories metatrace_categories =
      static_cast<metatrace::MetatraceCategories>(
          metatrace::MetatraceCategories::QUERY_TIMELINE |
          metatrace::MetatraceCategories::API_TIMELINE);

  bool help = false;
  bool version = false;
};

// Returns the FlagSpec entries for all global options.
std::vector<FlagSpec> GetGlobalFlagSpecs(GlobalOptions* opts);

// Returns the formatted usage string for a subcommand.
std::string FormatSubcommandUsage(const char* argv0, Subcommand* cmd);

// Parses flags for a subcommand. Combines the subcommand's flags with
// the global flags, then parses argv using getopt_long.
// Positional args are collected into ctx->positional_args.
base::Status ParseFlags(Subcommand* cmd,
                        SubcommandContext* ctx,
                        int argc,
                        char** argv);

// Builds a TraceProcessor Config from the global options.
Config BuildConfig(const GlobalOptions& opts,
                   TraceProcessorShell_PlatformInterface* platform);

// Creates and configures a TraceProcessor instance.
base::StatusOr<std::unique_ptr<TraceProcessor>> SetupTraceProcessor(
    const GlobalOptions& opts,
    const Config& config,
    TraceProcessorShell_PlatformInterface* platform);

// Loads a trace file into the TraceProcessor, performing symbolization
// and deobfuscation. Returns the wall-clock time spent loading.
base::StatusOr<base::TimeNanos> LoadTraceFile(
    TraceProcessor* tp,
    TraceProcessorShell_PlatformInterface* platform,
    const std::string& trace_file);

}  // namespace perfetto::trace_processor::shell

#endif  // SRC_TRACE_PROCESSOR_SHELL_COMMON_FLAGS_H_
