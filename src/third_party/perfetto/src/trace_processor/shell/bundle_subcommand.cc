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

#include "src/trace_processor/shell/bundle_subcommand.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/traceconv/trace_to_bundle.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#endif

namespace perfetto::trace_processor::shell {
namespace {

// Returns a human-readable description of a symlink problem with `path`, or
// an empty string if the path is either not a symlink or is a symlink whose
// target resolves. Used to distinguish a broken/dangling link or a link loop
// from a plain missing file, which would otherwise be reported as "does not
// exist".
std::string SymlinkProblemDescription(const std::string& path) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return "";
#else
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) {
    return "";
  }
  // MSan's stat() interceptor on glibc 2.35+ does not mark the output buffer
  // as initialized (the syscall goes through statx).
  PERFETTO_MSAN_UNPOISON(&st, sizeof(st));
  if (!S_ISLNK(st.st_mode)) {
    return "";
  }
  if (stat(path.c_str(), &st) != 0) {
    if (errno == ELOOP) {
      return "symbolic link loop (the link ultimately points to itself)";
    }
    return "broken symbolic link (its target does not exist)";
  }
  return "";
#endif
}

}  // namespace

const char* BundleSubcommand::name() const {
  return "bundle";
}

const char* BundleSubcommand::description() const {
  return "Bundle a trace with symbols and deobfuscation data.";
}

const char* BundleSubcommand::usage_args() const {
  return "<input> <output>";
}

const char* BundleSubcommand::detailed_help() const {
  return R"(Create a self-contained bundle from a trace.

Outputs a TAR containing the trace plus the symbols and deobfuscation
mappings needed to make it self-contained. Both <input> and <output> must be
real file paths (stdin/stdout are not supported).)";
}

std::vector<FlagSpec> BundleSubcommand::GetFlags() {
  return {
      StringFlag("symbol-paths", '\0', "PATH1,PATH2,...",
                 "Additional paths to search for symbols.", &symbol_paths_),
      BoolFlag("no-auto-symbol-paths", '\0',
               "Disable automatic symbol path discovery.",
               &no_auto_symbol_paths_),
      FlagSpec{
          "proguard-map", '\0', true, "[pkg=]PATH",
          "ProGuard/R8 mapping.txt for deobfuscation (may be repeated). The "
          "pkg= prefix scopes the map to a package.",
          [this](const char* v) { proguard_maps_.emplace_back(v); }},
      BoolFlag("no-auto-proguard-maps", '\0',
               "Disable automatic ProGuard/R8 mapping discovery.",
               &no_auto_proguard_maps_),
      BoolFlag("verbose", '\0', "Print more detailed output.", &verbose_),
  };
}

base::Status BundleSubcommand::Run(const SubcommandContext& ctx) {
  if (ctx.positional_args.size() < 2) {
    return base::ErrStatus(
        "bundle requires both an input and an output file path, got %zu "
        "argument(s).",
        ctx.positional_args.size());
  }
  if (ctx.positional_args.size() > 2) {
    return RejectExtraPositionals(
        ctx, "bundle", 2,
        "Note: --symbol-paths takes a single comma-separated list of paths, "
        "e.g. --symbol-paths path1,path2,path3; do not pass each path as a "
        "separate argument.");
  }
  const std::string& input_file = ctx.positional_args[0];
  const std::string& output_file = ctx.positional_args[1];

  if (input_file == "-") {
    return base::ErrStatus(
        "bundle does not support stdin input; provide a file path.");
  }
  if (output_file == "-") {
    return base::ErrStatus(
        "bundle does not support stdout output; provide a file path.");
  }
  if (!base::FileExists(input_file)) {
    std::string symlink_problem = SymlinkProblemDescription(input_file);
    if (!symlink_problem.empty()) {
      return base::ErrStatus(
          "bundle: input file '%s' is a %s. Fix the symlink and try again.",
          input_file.c_str(), symlink_problem.c_str());
    }
    return base::ErrStatus(
        "bundle: input file '%s' does not exist. Check the path and try "
        "again.",
        input_file.c_str());
  }
  if (base::DirectoryExists(input_file)) {
    return base::ErrStatus(
        "bundle: input path '%s' is a directory, expected a trace file. If "
        "you are trying to bundle a directory of traces, point at the "
        "individual files instead.",
        input_file.c_str());
  }
  if (base::DirectoryExists(output_file)) {
    return base::ErrStatus(
        "bundle: output path '%s' is a directory, expected a file path for "
        "the bundle (e.g. %s.bundle.tar).",
        output_file.c_str(), input_file.c_str());
  }
  std::string symlink_problem = SymlinkProblemDescription(output_file);
  if (!symlink_problem.empty()) {
    return base::ErrStatus(
        "bundle: output path '%s' is a %s. Fix the symlink and try again.",
        output_file.c_str(), symlink_problem.c_str());
  }

  // Fail fast on output paths that cannot be created, before spending time
  // reading the (potentially huge) trace. The TarWriter re-opens with O_TRUNC
  // once the trace has been read successfully.
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  {
    base::ScopedFile probe =
        base::OpenFile(output_file, O_CREAT | O_WRONLY, 0644);
    if (!probe) {
      return base::ErrStatus(
          "bundle: cannot create output file '%s' (errno: %d, %s). Check "
          "that the parent directory exists and is writable, then try "
          "again.",
          output_file.c_str(), errno, strerror(errno));
    }
  }
#endif

  trace_to_text::BundleContext context;
  if (!symbol_paths_.empty())
    context.symbol_paths = base::SplitString(symbol_paths_, ",");
  for (const std::string& map : proguard_maps_) {
    trace_to_text::ProguardMapSpec spec;
    size_t eq = map.find('=');
    if (eq == std::string::npos) {
      spec.path = map;
    } else {
      spec.package = map.substr(0, eq);
      spec.path = map.substr(eq + 1);
    }
    context.proguard_maps.push_back(std::move(spec));
  }
  context.no_auto_symbol_paths = no_auto_symbol_paths_;
  context.no_auto_proguard_maps = no_auto_proguard_maps_;
  context.verbose = verbose_;
  if (const char* val = getenv("ANDROID_PRODUCT_OUT"))
    context.android_product_out = val;
  if (const char* val = getenv("HOME"))
    context.home_dir = val;
  context.root_dir = "/";

  base::Status status =
      trace_to_text::TraceToBundle(input_file, output_file, context);
  if (status.ok()) {
    fprintf(stdout, "Wrote %s.\n", output_file.c_str());
    fflush(stdout);
  }
  return status;
}

}  // namespace perfetto::trace_processor::shell
