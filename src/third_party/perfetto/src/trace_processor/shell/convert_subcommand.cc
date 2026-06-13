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

#include <cstdlib>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/traceconv/traceconv.h"
#include "src/trace_processor/shell/subcommand.h"

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
  return R"(Convert a trace between formats. Wraps the traceconv tool.

Formats:
  systrace              Convert to systrace HTML format
  json                  Convert to Chrome JSON format
  ctrace                Convert to compressed systrace format
  text                  Convert to human-readable text format
  profile               Convert profile data to pprof format
  java_heap_profile     Legacy alias for "profile --java-heap"
  hprof                 Convert heap profile to hprof format
  symbolize             Symbolize addresses in profiles
  deobfuscate           Deobfuscate obfuscated profiles
  firefox               Convert to Firefox profiler format
  decompress_packets    Decompress compressed trace packets
  bundle                Create bundle with trace + debug data
  binary                Convert text proto to binary format

If no input file is given, reads from stdin.
If no output file is given, writes to stdout.)";
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
      StringFlag("symbol-paths", '\0', "PATH1,PATH2,...",
                 "Additional paths to search for symbols.", &symbol_paths_),
      BoolFlag("no-auto-symbol-paths", '\0',
               "Disable automatic symbol path discovery.",
               &no_auto_symbol_paths_),
      FlagSpec{"proguard-map", '\0', true, "[pkg=]PATH",
               "ProGuard/R8 mapping.txt for deobfuscation (may be repeated).",
               [this](const char* v) { proguard_maps_.emplace_back(v); }},
      BoolFlag("no-auto-proguard-maps", '\0',
               "Disable automatic ProGuard/R8 mapping discovery.",
               &no_auto_proguard_maps_),
      BoolFlag("verbose", '\0', "Print more detailed output.", &verbose_),
      BoolFlag("skip-unknown", '\0',
               "Skip unknown fields when converting to text.", &skip_unknown_),
  };
}

base::Status ConvertSubcommand::Run(const SubcommandContext& ctx) {
  // TODO(lalitm): this delegates to TraceconvMain via a synthetic argv as a
  // temporary measure. The traceconv logic should be fully inlined here and
  // TraceconvMain should be deleted.

  // Build a synthetic argv for TraceconvMain. The first element is the
  // program name, followed by any flags that were set, then positional args
  // (format, input, output).
  std::vector<std::string> args_storage;
  args_storage.push_back("trace_processor_shell convert");

  if (!truncate_.empty()) {
    args_storage.push_back("--truncate");
    args_storage.push_back(truncate_);
  }
  if (full_sort_) {
    args_storage.push_back("--full-sort");
  }
  if (!pid_.empty()) {
    args_storage.push_back("--pid");
    args_storage.push_back(pid_);
  }
  if (!timestamps_.empty()) {
    args_storage.push_back("--timestamps");
    args_storage.push_back(timestamps_);
  }
  if (alloc_) {
    args_storage.push_back("--alloc");
  }
  if (perf_) {
    args_storage.push_back("--perf");
  }
  if (java_heap_) {
    args_storage.push_back("--java-heap");
  }
  if (no_annotations_) {
    args_storage.push_back("--no-annotations");
  }
  if (!output_dir_.empty()) {
    args_storage.push_back("--output-dir");
    args_storage.push_back(output_dir_);
  }
  if (!symbol_paths_.empty()) {
    args_storage.push_back("--symbol-paths");
    args_storage.push_back(symbol_paths_);
  }
  if (no_auto_symbol_paths_) {
    args_storage.push_back("--no-auto-symbol-paths");
  }
  for (const auto& map : proguard_maps_) {
    args_storage.push_back("--proguard-map");
    args_storage.push_back(map);
  }
  if (no_auto_proguard_maps_) {
    args_storage.push_back("--no-auto-proguard-maps");
  }
  if (verbose_) {
    args_storage.push_back("--verbose");
  }
  if (skip_unknown_) {
    args_storage.push_back("--skip-unknown");
  }

  // Append positional args (format, input file, output file).
  for (const auto& arg : ctx.positional_args) {
    args_storage.push_back(arg);
  }

  // Build the char** argv from the string storage.
  std::vector<char*> argv_ptrs;
  for (auto& s : args_storage) {
    argv_ptrs.push_back(s.data());
  }
  argv_ptrs.push_back(nullptr);

  int argc = static_cast<int>(args_storage.size());
  int ret = traceconv::TraceconvMain(argc, argv_ptrs.data());
  if (ret != 0) {
    return base::ErrStatus("convert: traceconv returned error code %d", ret);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::shell
