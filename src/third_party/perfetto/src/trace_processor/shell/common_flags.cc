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

#include "src/trace_processor/shell/common_flags.h"
#include "src/traceconv/utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/descriptor.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/trace_processor/trace_processor_shell.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/metatrace_config.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/metrics.h"
#include "src/trace_processor/shell/shell_utils.h"
#include "src/trace_processor/shell/sql_packages.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/trace_processor/util/deobfuscation/deobfuscator.h"
#include "src/trace_processor/util/symbolizer/symbolize_database.h"

#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_IPC)
#include "src/trace_processor/rpc/remote_trace_processor.h"
#endif

namespace perfetto::trace_processor {

// Keep the destructor in the common shell library so embedders using the
// subcommand implementation do not need to link the legacy shell entrypoint.
TraceProcessorShell_PlatformInterface::
    ~TraceProcessorShell_PlatformInterface() = default;

}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::shell {
namespace {

void AppendFlagList(std::string* out, const std::vector<FlagSpec>& flags) {
  char buf[256];
  for (const auto& f : flags) {
    if (f.short_name) {
      snprintf(buf, sizeof(buf), "  -%c, --%-24s %s\n", f.short_name,
               f.long_name, f.help);
    } else {
      snprintf(buf, sizeof(buf), "      --%-24s %s\n", f.long_name, f.help);
    }
    *out += buf;
  }
}

base::StatusOr<std::string> ReadFileContents(const std::string& filename) {
  if (!base::FileExists(filename)) {
    return base::ErrStatus("File %s does not exist", filename.c_str());
  }
  std::string data;
  if (!base::ReadFile(filename, &data)) {
    return base::ErrStatus("Cannot read file '%s'", filename.c_str());
  }
  return std::move(data);
}

}  // namespace

std::string FormatSubcommandUsage(const char* argv0, Subcommand* cmd) {
  std::string out;
  base::StackString<256> buf("Usage: %s %s [FLAGS] %s\n\n", argv0, cmd->name(),
                             cmd->usage_args());
  out += buf.c_str();
  out += cmd->description();
  out += "\n\n";
  out += cmd->detailed_help();
  out += "\n\n";

  auto sub_flags = cmd->GetFlags();
  if (!sub_flags.empty()) {
    out += "Subcommand flags:\n";
    AppendFlagList(&out, sub_flags);
    out += "\n";
  }

  GlobalOptions dummy;
  out += "Global flags:\n";
  AppendFlagList(&out, GetGlobalFlagSpecs(&dummy));
  return out;
}

std::vector<FlagSpec> GetGlobalFlagSpecs(GlobalOptions* opts) {
  std::vector<FlagSpec> flags;
  flags.push_back(BoolFlag("help", 'h', "Prints this guide.", &opts->help));
  flags.push_back(
      BoolFlag("version", 'v', "Prints the version.", &opts->version));
  flags.push_back(BoolFlag("full-sort", '\0',
                           "Forces full sort ignoring windowing.",
                           &opts->force_full_sort));
  flags.push_back(BoolFlag("no-ftrace-raw", '\0',
                           "Prevents ingestion of typed ftrace into raw table.",
                           &opts->no_ftrace_raw));
  flags.push_back(BoolFlag("analyze-trace-proto-content", '\0',
                           "Enables trace proto content analysis.",
                           &opts->analyze_trace_proto_content));
  flags.push_back(BoolFlag("crop-track-events", '\0',
                           "Ignores track events outside range of interest.",
                           &opts->crop_track_events));
  flags.push_back(BoolFlag(
      "allow-sql-file-access", '\0',
      "Allows SQL functions to access files visible to the shell process. Do "
      "not enable this for untrusted SQL.",
      &opts->allow_sql_file_access));
  flags.push_back(
      BoolFlag("dev", '\0', "Enables local development features.", &opts->dev));
  flags.push_back({/*long_name=*/"dev-flag", /*short_name=*/'\0',
                   /*has_arg=*/true,
                   /*arg_name=*/"KEY=VALUE", /*help=*/"Set a development flag.",
                   [opts](const char* v) { opts->dev_flags.emplace_back(v); }});
  flags.push_back(BoolFlag("extra-checks", '\0',
                           "Enables additional SQL error checks.",
                           &opts->extra_checks));
  flags.push_back(
      {/*long_name=*/"add-sql-package", /*short_name=*/'\0',
       /*has_arg=*/true, /*arg_name=*/"PATH[@PKG]",
       /*help=*/"Registers SQL files from a directory as a package.",
       [opts](const char* v) { opts->sql_package_paths.emplace_back(v); }});
  flags.push_back({/*long_name=*/"override-sql-package", /*short_name=*/'\0',
                   /*has_arg=*/true, /*arg_name=*/"PATH[@PKG]",
                   /*help=*/"Same as --add-sql-package but allows overriding.",
                   [opts](const char* v) {
                     opts->override_sql_package_paths.emplace_back(v);
                   }});
  flags.push_back(StringFlag("override-stdlib", '\0', "PATH",
                             "Override trace_processor/stdlib.",
                             &opts->override_stdlib_path));
  flags.push_back(StringFlag("register-files-dir", '\0', "PATH",
                             "Register files for importers.",
                             &opts->register_files_dir));
  flags.push_back(
      {/*long_name=*/"metric-extension", /*short_name=*/'\0',
       /*has_arg=*/true, /*arg_name=*/"DISK@VIRTUAL",
       /*help=*/"Load metric extension protos/sql from DISK onto VIRTUAL.",
       [opts](const char* v) {
         opts->raw_metric_v1_extensions.emplace_back(v);
       }});
  flags.push_back(StringFlag(
      "remote", '\0', "ADDR",
      "Run against a warm session instead of loading a local trace. ADDR is a "
      "session name, a *.sock/absolute socket path, or host:port.",
      &opts->remote_addr));
  flags.push_back(StringFlag("metatrace", 'm', "FILE",
                             "Enables metatracing, writes to FILE.",
                             &opts->metatrace_path));
  flags.push_back(
      {/*long_name=*/"metatrace-buffer-capacity", /*short_name=*/'\0',
       /*has_arg=*/true, /*arg_name=*/"N",
       /*help=*/"Sets metatrace buffer to capture last N events.",
       [opts](const char* v) {
         opts->metatrace_buffer_capacity = static_cast<size_t>(atoi(v));
       }});
  flags.push_back({/*long_name=*/"metatrace-categories", /*short_name=*/'\0',
                   /*has_arg=*/true, /*arg_name=*/"CATEGORIES",
                   /*help=*/"Comma-separated list of metatrace categories.",
                   [opts](const char* v) {
                     opts->metatrace_categories = ParseMetatraceCategories(v);
                   }});
  return flags;
}

base::Status ParseFlags(Subcommand* cmd,
                        SubcommandContext* ctx,
                        int argc,
                        char** argv) {
  auto sub_flags = cmd->GetFlags();
  auto global_flags = GetGlobalFlagSpecs(ctx->global);

  // Build the getopt_long option array. We assign IDs starting at 1000
  // for long-only options.
  std::vector<option> long_opts;
  // Maps from option val -> FlagSpec handler.
  struct HandlerEntry {
    int val;
    std::function<void(const char*)>* handler;
  };
  std::vector<HandlerEntry> handlers;

  std::string short_opts;
  int next_id = 1000;

  auto add_flags = [&](const std::vector<FlagSpec>& specs) {
    for (const auto& f : specs) {
      int val;
      if (f.short_name != '\0') {
        val = f.short_name;
        short_opts += f.short_name;
        if (f.has_arg) {
          short_opts += ':';
        }
      } else {
        val = next_id++;
      }
      option opt = {};
      opt.name = f.long_name;
      opt.has_arg = f.has_arg ? required_argument : no_argument;
      opt.flag = nullptr;
      opt.val = val;
      long_opts.push_back(opt);
      // We need a stable pointer; we'll grab it from the original vector.
    }
  };

  add_flags(sub_flags);
  add_flags(global_flags);

  // Sentinel.
  long_opts.push_back({nullptr, 0, nullptr, 0});

  // Build a combined handler list indexed by val.
  auto build_handler_map = [&](std::vector<FlagSpec>* specs, int start_id) {
    for (auto& f : *specs) {
      int val;
      if (f.short_name != '\0') {
        val = f.short_name;
      } else {
        val = start_id++;
      }
      handlers.push_back({val, &f.handler});
    }
  };

  int sub_start = 1000;
  build_handler_map(&sub_flags, sub_start);
  int global_start = sub_start;
  // Count how many sub_flags have no short_name.
  for (const auto& f : sub_flags) {
    if (f.short_name == '\0') {
      global_start++;
    }
  }
  build_handler_map(&global_flags, global_start);

  // Reset getopt state. Use 0 (not 1) to force a full re-initialization: glibc
  // only resets its internal scan state when optind is 0, so 1 would leak state
  // between successive ParseFlags() calls in the same process (e.g. in tests).
  optind = 0;

  for (;;) {
    int opt =
        getopt_long(argc, argv, short_opts.c_str(), long_opts.data(), nullptr);
    if (opt == -1)
      break;

    if (opt == '?') {
      // getopt_long already printed a diagnostic to stderr; mark the
      // status so callers know not to print the message again.
      base::Status s = base::ErrStatus("Unknown flag");
      s.SetPayload("perfetto.dev/has_printed_error", "1");
      return s;
    }

    bool found = false;
    for (const auto& h : handlers) {
      if (h.val == opt) {
        (*h.handler)(optarg);
        found = true;
        break;
      }
    }
    if (!found) {
      return base::ErrStatus("Unhandled flag %d", opt);
    }
  }

  // Collect positional arguments.
  for (int i = optind; i < argc; ++i) {
    ctx->positional_args.emplace_back(argv[i]);
  }

  return base::OkStatus();
}

base::StatusOr<Config> BuildConfig(
    const GlobalOptions& opts,
    TraceProcessorShell_PlatformInterface* platform) {
  Config config = platform->DefaultConfig();
  config.enable_sql_file_access = opts.allow_sql_file_access;
  if (config.enable_sql_file_access && !platform->GetFileSystem()) {
    return base::ErrStatus(
        "--allow-sql-file-access is not supported by this shell platform");
  }
  config.sorting_mode = opts.force_full_sort ? SortingMode::kForceFullSort
                                             : SortingMode::kDefaultHeuristics;
  config.ingest_ftrace_in_raw_table = !opts.no_ftrace_raw;
  config.analyze_trace_proto_content = opts.analyze_trace_proto_content;
  config.drop_track_event_data_before =
      opts.crop_track_events
          ? DropTrackEventDataBefore::kTrackEventRangeOfInterest
          : DropTrackEventDataBefore::kNoDrop;

  for (const auto& ext : opts.metric_extensions) {
    config.skip_builtin_metric_paths.push_back(ext.virtual_path());
  }

  if (opts.dev) {
    config.enable_dev_features = true;
    for (const auto& flag_pair : opts.dev_flags) {
      auto kv = base::SplitString(flag_pair, "=");
      if (kv.size() != 2) {
        PERFETTO_ELOG("Ignoring unknown dev flag format %s", flag_pair.c_str());
        continue;
      }

      if (kv[0] == "extra_parsing_descriptors") {
        auto files = base::SplitString(kv[1], ";");
        for (const auto& filepath : files) {
          ASSIGN_OR_RETURN(std::string data, ReadFileContents(filepath));
          config.extra_parsing_descriptors.push_back(std::move(data));
        }
        continue;
      }

      config.dev_flags.emplace(kv[0], kv[1]);
    }
  }

  if (opts.extra_checks) {
    config.enable_extra_checks = true;
  }

  return config;
}

base::StatusOr<std::unique_ptr<TraceProcessor>> SetupTraceProcessor(
    const GlobalOptions& opts,
    const Config& config,
    TraceProcessorShell_PlatformInterface* platform) {
  std::unique_ptr<TraceProcessor> tp =
      TraceProcessor::CreateInstance(config, platform);
  auto status = platform->OnTraceProcessorCreated(tp.get());
  if (!status.ok()) {
    return base::StatusOr<std::unique_ptr<TraceProcessor>>(status);
  }

  // Apply stdlib overrides.
  if (!opts.override_stdlib_path.empty()) {
    if (!opts.dev) {
      return base::ErrStatus("Overriding stdlib requires --dev flag");
    }
    auto s = LoadOverridenStdlib(tp.get(), opts.override_stdlib_path);
    if (!s.ok()) {
      return base::ErrStatus("Couldn't override stdlib: %s", s.c_message());
    }
  }

  // Apply SQL package overrides.
  for (const auto& p : opts.override_sql_package_paths) {
    auto s = IncludeSqlPackage(tp.get(), p, true);
    if (!s.ok()) {
      return base::ErrStatus("Couldn't override stdlib package: %s",
                             s.c_message());
    }
  }

  // Add SQL packages.
  for (const auto& p : opts.sql_package_paths) {
    auto s = IncludeSqlPackage(tp.get(), p, false);
    if (!s.ok()) {
      return base::ErrStatus("Couldn't add SQL package: %s", s.c_message());
    }
  }

  // Load metric extensions. We load these even when --run-metrics is not
  // specified because we want the metrics to be available in interactive
  // mode or when used in UI via httpd. The descriptor pool and parsed
  // extensions are pre-populated in GlobalOptions by the caller.
  if (opts.metric_descriptor_pool) {
    for (const auto& extension : opts.metric_extensions) {
      RETURN_IF_ERROR(LoadMetricExtension(tp.get(), extension,
                                          *opts.metric_descriptor_pool));
    }
  }

  // Enable metatracing.
  if (!opts.metatrace_path.empty()) {
    metatrace::MetatraceConfig metatrace_config;
    metatrace_config.override_buffer_size = opts.metatrace_buffer_capacity;
    metatrace_config.categories = opts.metatrace_categories;
    tp->EnableMetatrace(metatrace_config);
  }

  // Register files.
  if (!opts.register_files_dir.empty()) {
    auto s = RegisterAllFilesInFolder(opts.register_files_dir, *tp);
    if (!s.ok()) {
      return base::StatusOr<std::unique_ptr<TraceProcessor>>(s);
    }
  }

  return std::move(tp);
}

base::StatusOr<base::TimeNanos> LoadTraceFile(
    TraceProcessor* tp,
    TraceProcessorShell_PlatformInterface* platform,
    const std::string& trace_file) {
  base::TimeNanos t_load_start = base::GetWallTimeNs();
  double size_mb = 0;

  base::Status load_status =
      platform->LoadTrace(tp, trace_file, [&size_mb](size_t parsed_size) {
        size_mb = static_cast<double>(parsed_size) / 1E6;
        fprintf(stderr, "\rLoading trace: %.2f MB\r", size_mb);
      });
  // Terminate the in-place progress line so errors/logs below start on a
  // fresh line.
  trace_to_text::EndProgressLine();
  if (!load_status.ok()) {
    return base::ErrStatus("failed to read trace file (path: %s): %s",
                           trace_file.c_str(), load_status.c_message());
  }

  bool is_proto_trace = false;
  {
    auto it = tp->ExecuteQuery(
        "SELECT str_value FROM metadata WHERE name = 'trace_type'");
    while (it.Next()) {
      if (it.Get(0).type == SqlValue::kString &&
          std::string_view(it.Get(0).AsString()) == "proto") {
        is_proto_trace = true;
        break;
      }
    }
  }

  profiling::SymbolizerConfig sym_config;
  const char* mode = getenv("PERFETTO_SYMBOLIZER_MODE");
  std::vector<std::string> paths = profiling::GetPerfettoBinaryPath();
  if (mode && std::string_view(mode) == "find") {
    sym_config.find_symbol_paths = std::move(paths);
  } else {
    sym_config.index_symbol_paths = std::move(paths);
  }
  if (!sym_config.index_symbol_paths.empty() ||
      !sym_config.find_symbol_paths.empty()) {
    if (is_proto_trace) {
      tp->Flush();
      auto sym_result =
          profiling::SymbolizeDatabaseAndLog(tp, sym_config, /*verbose=*/false);
      if (sym_result.error == profiling::SymbolizerError::kOk &&
          !sym_result.symbols.empty()) {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[sym_result.symbols.size()]);
        memcpy(buf.get(), sym_result.symbols.data(), sym_result.symbols.size());
        auto status = tp->Parse(std::move(buf), sym_result.symbols.size());
        if (!status.ok()) {
          PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s",
                                  status.message().c_str());
        }
      }
    } else {
      PERFETTO_ELOG("Skipping symbolization for non-proto trace");
    }
  }

  auto maybe_map = profiling::GetPerfettoProguardMapPath();
  if (!maybe_map.empty()) {
    if (is_proto_trace) {
      tp->Flush();
      auto deob_status = profiling::ReadProguardMapsToDeobfuscationPackets(
          maybe_map, [tp](const std::string& trace_proto) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[trace_proto.size()]);
            memcpy(buf.get(), trace_proto.data(), trace_proto.size());
            auto status = tp->Parse(std::move(buf), trace_proto.size());
            if (!status.ok()) {
              PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s",
                                      status.message().c_str());
              return;
            }
          });
      if (!deob_status.ok()) {
        PERFETTO_ELOG("Failed to deobfuscate: %s", deob_status.c_message());
      }
    } else {
      PERFETTO_ELOG("Skipping deobfuscation for non-proto trace");
    }
  }

  auto notify_status = tp->NotifyEndOfFile();
  if (!notify_status.ok()) {
    return base::StatusOr<base::TimeNanos>(notify_status);
  }

  base::TimeNanos t_load = base::GetWallTimeNs() - t_load_start;
  double t_load_s = static_cast<double>(t_load.count()) / 1E9;
  PERFETTO_ILOG("Trace loaded: %.2f MB in %.2fs (%.1f MB/s)", size_mb, t_load_s,
                size_mb / t_load_s);

  auto stats_status = PrintStats(tp);
  if (!stats_status.ok()) {
    return base::StatusOr<base::TimeNanos>(stats_status);
  }

  return t_load;
}

base::Status ResolveTraceFileArg(const SubcommandContext& ctx,
                                 const char* subcommand,
                                 std::string* trace_file,
                                 size_t* first_extra_arg) {
  if (!ctx.global->remote_addr.empty()) {
    trace_file->clear();
    if (first_extra_arg)
      *first_extra_arg = 0;
    return base::OkStatus();
  }
  if (ctx.positional_args.empty()) {
    return base::ErrStatus("%s: trace file is required", subcommand);
  }
  *trace_file = ctx.positional_args[0];
  if (first_extra_arg)
    *first_extra_arg = 1;
  return base::OkStatus();
}

#if PERFETTO_BUILDFLAG(PERFETTO_IPC)
// In --remote mode the trace is already loaded and configured by the session,
// so flags that configure local parsing or register local engine state have no
// effect. Reject them explicitly rather than silently ignore them. (Forwarding
// the verb-backed ones, e.g. --add-sql-package, is possible future work.)
static base::Status CheckRemoteFlagCompatibility(const GlobalOptions& opts) {
  const struct {
    bool is_set;
    const char* flag;
  } incompatible[] = {
      {opts.force_full_sort, "--full-sort"},
      {opts.no_ftrace_raw, "--no-ftrace-raw"},
      {opts.analyze_trace_proto_content, "--analyze-trace-proto-content"},
      {opts.crop_track_events, "--crop-track-events"},
      {opts.allow_sql_file_access, "--allow-sql-file-access"},
      {opts.dev, "--dev"},
      {!opts.dev_flags.empty(), "--dev-flag"},
      {opts.extra_checks, "--extra-checks"},
      {!opts.sql_package_paths.empty(), "--add-sql-package"},
      {!opts.override_sql_package_paths.empty(), "--override-sql-package"},
      {!opts.override_stdlib_path.empty(), "--override-stdlib"},
      {!opts.register_files_dir.empty(), "--register-files-dir"},
      {!opts.raw_metric_v1_extensions.empty(), "--metric-extension"},
  };
  for (const auto& f : incompatible) {
    if (f.is_set) {
      return base::ErrStatus(
          "%s cannot be combined with --remote: the trace is already loaded "
          "and configured by the session. Pass this flag to `tp server unix` "
          "when starting the session instead.",
          f.flag);
    }
  }
  return base::OkStatus();
}
#endif  // PERFETTO_BUILDFLAG(PERFETTO_IPC)

base::StatusOr<std::unique_ptr<TraceProcessor>> CreateTraceProcessor(
    const GlobalOptions& opts,
    TraceProcessorShell_PlatformInterface* platform,
    const std::string& trace_file,
    base::TimeNanos* t_load_out) {
  if (!opts.remote_addr.empty()) {
#if PERFETTO_BUILDFLAG(PERFETTO_IPC)
    RETURN_IF_ERROR(CheckRemoteFlagCompatibility(opts));
    ASSIGN_OR_RETURN(auto remote,
                     RemoteTraceProcessor::Connect(opts.remote_addr));
    // Metatracing is scoped to this client's queries: enabled here, then
    // collected by MaybeWriteMetatrace. Note the buffer capacity override is
    // not forwarded over the RPC, only the categories.
    if (!opts.metatrace_path.empty()) {
      metatrace::MetatraceConfig metatrace_config;
      metatrace_config.categories = opts.metatrace_categories;
      remote->EnableMetatrace(metatrace_config);
    }
    if (t_load_out)
      *t_load_out = base::TimeNanos(0);
    return std::unique_ptr<TraceProcessor>(std::move(remote));
#else
    return base::ErrStatus("--remote not supported in this build");
#endif
  }
  ASSIGN_OR_RETURN(Config config, BuildConfig(opts, platform));
  ASSIGN_OR_RETURN(auto tp, SetupTraceProcessor(opts, config, platform));
  ASSIGN_OR_RETURN(base::TimeNanos t_load,
                   LoadTraceFile(tp.get(), platform, trace_file));
  if (t_load_out)
    *t_load_out = t_load;
  return std::move(tp);
}

}  // namespace perfetto::trace_processor::shell
