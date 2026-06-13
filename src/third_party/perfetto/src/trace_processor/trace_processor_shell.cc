/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "perfetto/ext/trace_processor/trace_processor_shell.h"

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <google/protobuf/compiler/parser.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"  // IWYU pragma: keep
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/metatrace_config.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/read_trace_internal.h"
#include "src/trace_processor/rpc/rpc.h"
#include "src/trace_processor/rpc/stdiod.h"
#include "src/trace_processor/shell/common_flags.h"
#include "src/trace_processor/shell/convert_subcommand.h"
#include "src/trace_processor/shell/export_subcommand.h"
#include "src/trace_processor/shell/interactive_subcommand.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/metrics_subcommand.h"
#include "src/trace_processor/shell/query_subcommand.h"
#include "src/trace_processor/shell/server_subcommand.h"
#include "src/trace_processor/shell/subcommand.h"
#include "src/trace_processor/shell/summarize_subcommand.h"
#include "src/trace_processor/util/symbolizer/symbolize_database.h"

#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
#include "src/trace_processor/rpc/httpd.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_HAS_SIGNAL_H() 1
#else
#define PERFETTO_HAS_SIGNAL_H() 0
#endif

#if PERFETTO_HAS_SIGNAL_H()
#include <signal.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <io.h>
#define ftruncate _chsize
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace perfetto::trace_processor {

namespace {

struct CommandLineOptions {
  std::string trace_file_path;

  bool enable_httpd = false;
  std::string port_number;
  std::string listen_ip;
  std::vector<std::string> additional_cors_origins;
  bool enable_stdiod = false;
  bool launch_shell = false;

  bool force_full_sort = false;
  bool no_ftrace_raw = false;

  std::string query_file_path;
  std::string query_string;
  std::vector<std::string> sql_package_paths;
  std::vector<std::string> override_sql_package_paths;

  bool summary = false;
  std::string summary_metrics_v2;
  std::string summary_metadata_query;
  std::vector<std::string> summary_specs;
  std::string summary_output;

  std::string metatrace_path;
  size_t metatrace_buffer_capacity = 0;
  metatrace::MetatraceCategories metatrace_categories =
      static_cast<metatrace::MetatraceCategories>(
          metatrace::MetatraceCategories::QUERY_TIMELINE |
          metatrace::MetatraceCategories::API_TIMELINE);

  bool dev = false;
  std::vector<std::string> dev_flags;
  bool extra_checks = false;
  std::string export_file_path;
  std::string perf_file_path;
  bool wide = false;
  bool analyze_trace_proto_content = false;
  bool crop_track_events = false;
  std::string register_files_dir;
  std::string override_stdlib_path;

  std::string pre_metrics_v1_path;
  std::string metric_v1_names;
  std::string metric_v1_output;
  std::vector<std::string> raw_metric_v1_extensions;
};

void PrintSubcommandHelp(const char* argv0) {
  printf(R"(Perfetto Trace Processor.
Usage: %s [command] [flags] [trace_file]

If no command is given, opens an interactive SQL shell on the trace file.

Commands:
  query         Load a trace and run a SQL query.
  interactive   Interactive SQL shell (default if no command is given).
  server        Start an RPC server (http or stdio).
  summarize     Compute a trace summary from specs and/or built-in metrics.
  export        Export a trace to a database file.
  metrics       Run v1 metrics (deprecated; use 'summarize --metrics-v2').
  convert       Convert trace format.

Common flags (apply to all commands):
  -h, --help                  Show help (per-command if after a command).
  -v, --version               Print version.
      --full-sort             Force full sort ignoring windowing.
      --no-ftrace-raw         Prevent ingestion of typed ftrace into raw table.
      --add-sql-package PATH  Register SQL files from a directory as a package.
  -m, --metatrace FILE        Enable metatracing, write to FILE.

Run '%s help <command>' for per-command flags and details.

Examples:
  tp trace.pb                                       Interactive shell.
  tp query trace.pb "SELECT ts, dur FROM slice"     Run a query.
  tp query -f queries.sql trace.pb                  Run queries from file.
  tp server http                                    Start HTTP server.
  tp summarize --metrics-v2 all trace.pb            Summarize a trace.
  tp convert json trace.pb out.json                 Convert to JSON.

Classic interface:
  The previous flat-flag interface (-q, --httpd, --summary, -e, etc.) is
  fully supported and will remain so. Existing scripts will continue to work.
  Run '%s --help-classic' to see the classic flag reference.
)",
         argv0, argv0, argv0);
}

void PrintClassicUsage(char** argv) {
  PERFETTO_ELOG(R"(
Interactive trace processor shell.
Usage: %s [FLAGS] trace_file.pb

General purpose:
 -h, --help                           Prints this guide.
 -v, --version                        Prints the version of trace processor.

Behavioural:
 -D, --httpd                          Enables the HTTP RPC server.
 --http-port PORT                     Specify what port to run HTTP RPC server.
 --http-ip-address ip                 Specify what ip address to run HTTP RPC server.
 --http-additional-cors-origins origin1,origin2,...
                                      Specify a comma-separated list of
                                      additional CORS allowed origins for the
                                      HTTP RPC server. These are in addition to
                                      the default origins: [https://ui.perfetto.dev,
                                      http://localhost:10000, http://127.0.0.1:10000]
 --stdiod                             Enables the stdio RPC server.
 -i, --interactive                    Starts interactive mode even after
                                      executing some other commands (-q, -Q,
                                      --run-metrics, --summary).

Parsing:
 --full-sort                          Forces the trace processor into performing
                                      a full sort ignoring any windowing
                                      logic.
 --no-ftrace-raw                      Prevents ingestion of typed ftrace events
                                      into the raw table. This significantly
                                      reduces the memory usage of trace
                                      processor when loading traces containing
                                      ftrace events.

PerfettoSQL:
 -q, --query-file FILE                Read and execute an SQL query from a file.
                                      If used with --run-metrics, the query is
                                      executed after the selected metrics and
                                      the metrics output is suppressed.
 -Q, --query-string QUERY             Execute the SQL query QUERY.
                                      If used with --run-metrics, the query is
                                      executed after the selected metrics and
                                      the metrics output is suppressed.
 --add-sql-package PATH[@PACKAGE]     Registers SQL files from a directory as
                                      a package for use with INCLUDE PERFETTO
                                      MODULE statements.

                                      By default, the directory name becomes the
                                      root package name. Use @PACKAGE to
                                      override.

                                      Given a directory structure:
                                        mydir/
                                          utils.sql
                                          helpers/common.sql

                                      --add-sql-package ./mydir
                                        Registers modules as:
                                          mydir.utils
                                          mydir.helpers.common
                                        Usage: INCLUDE PERFETTO MODULE mydir.utils;

                                      --add-sql-package ./mydir@foo
                                        Registers modules as:
                                          foo.utils
                                          foo.helpers.common
                                        Usage: INCLUDE PERFETTO MODULE foo.utils;

                                      --add-sql-package ./mydir@foo.bar.baz
                                        Registers modules as:
                                          foo.bar.baz.utils
                                          foo.bar.baz.helpers.common
                                        Usage: INCLUDE PERFETTO MODULE foo.bar.*;


Trace summarization:
  --summary                           Enables the trace summarization features of
                                      trace processor. Required for any flags
                                      starting with --summary-* to be meaningful.
                                      --summary-format can be used to control the
                                      output format.
  --summary-metrics-v2 ID1,ID2,ID3    Specifies that the given v2 metrics (as
                                      defined by a comma separated set of ids)
                                      should be computed and returned as part of
                                      the trace summary. The spec for every metric
                                      must exist in one of the files passed to
                                      --summary-spec. Specify `all` to execute all
                                      available v2 metrics.
  --summary-metadata-query ID         Specifies that the given query id should be
                                      used to populate the `metadata` field of the
                                      trace summary. The spec for the query must
                                      exist in one of the files passed to
                                      --summary-spec.
  --summary-spec SUMMARY_PATH         Parses the spec at the specified path and
                                      makes it available to all summarization
                                      operators (--summary-metrics-v2). Spec
                                      files must be instances of the
                                      perfetto.protos.TraceSummarySpec proto.
                                      If the file extension is `.textproto` then
                                      the spec file will be parsed as a
                                      textproto. If the file extension is `.pb`
                                      then it will be parsed as a binary
                                      protobuf. Otherwise, heureustics will be
                                      used to determine the format.
  --summary-format [text,binary]      Controls the serialization format of trace
                                      summarization proto
                                      (perfetto.protos.TraceSummary). If
                                      `binary`, then the output is a binary
                                      protobuf. If unspecified or `text` then
                                      the output is a textproto.

Metatracing:
 -m, --metatrace FILE                 Enables metatracing of trace processor
                                      writing the resulting trace into FILE.
 --metatrace-buffer-capacity N        Sets metatrace event buffer to capture
                                      last N events.
 --metatrace-categories CATEGORIES    A comma-separated list of metatrace
                                      categories to enable.

Advanced:
 --dev                                Enables features which are reserved for
                                      local development use only and
                                      *should not* be enabled on production
                                      builds. The features behind this flag can
                                      break at any time without any warning.
 --dev-flag KEY=VALUE                 Set a development flag to the given value.
                                      Does not have any affect unless --dev is
                                      specified.
 --extra-checks                       Enables additional checks which can catch
                                      more SQL errors, but which incur
                                      additional runtime overhead.
 -e, --export FILE                    Export the contents of trace processor
                                      into an SQLite database after running any
                                      metrics or queries specified.
 -p, --perf-file FILE                 Writes the time taken to ingest the trace
                                      and execute the queries to the given file.
                                      Only valid with -q or --run-metrics and
                                      the file will only be written if the
                                      execution is successful.
 -W, --wide                           Prints interactive output with double
                                      column width.
 --analyze-trace-proto-content        Enables trace proto content analysis in
                                      trace processor.
 --crop-track-events                  Ignores track event outside of the
                                      range of interest in trace processor.
 --register-files-dir PATH            The contents of all files in this
                                      directory and subdirectories will be made
                                      available to the trace processor runtime.
                                      Some importers can use this data to
                                      augment trace data (e.g. decode ETM
                                      instruction streams).
 --override-stdlib=[path_to_stdlib]   Will override trace_processor/stdlib with
                                      passed contents. The outer directory will
                                      be ignored. Only allowed when --dev is
                                      specified.
 --override-sql-package PATH[@PKG]    Same as --add-sql-package but allows
                                      overriding existing user-registered
                                      packages with the same name. This bypasses
                                      checks trace processor makes around
                                      packages already existing and clashing
                                      with stdlib package names so should be
                                      used with caution.

Structured queries:
 --structured-query-spec SPEC_PATH    Parses the spec at the specified path and
                                      makes queries available for execution.
                                      Spec files must be instances of the
                                      perfetto.protos.TraceSummarySpec proto.
                                      If the file extension is `.textproto` then
                                      the spec file will be parsed as a
                                      textproto. If the file extension is `.pb`
                                      then it will be parsed as a binary
                                      protobuf. Otherwise, heuristics will be
                                      used to determine the format.
 --structured-query-id ID             Specifies that the structured query with
                                      the given ID should be executed. The spec
                                      for the query must exist in one of the
                                      files passed to --structured-query-spec.

Metrics (v1):

  NOTE: the trace-based metrics system has been "soft" deprecated. Specifically,
  all existing metrics will continue functioning but we will not be building
  any new features nor developing any metrics there further. Please use the
  metrics v2 system as part of trace summarization.

 --run-metrics x,y,z                  Runs a comma separated list of metrics and
                                      prints the result as a TraceMetrics proto
                                      to stdout. The specified can either be
                                      in-built metrics or SQL/proto files of
                                      extension metrics.
 --pre-metrics FILE                   Read and execute an SQL query from a file.
                                      This query is executed before the selected
                                      metrics and can't output any results.
 --metrics-output=[binary|text|json]  Allows the output of --run-metrics to be
                                      specified in either proto binary, proto
                                      text format or JSON format (default: proto
                                      text).
 --metric-extension DISK_PATH@VIRTUAL_PATH
                                      Loads metric proto and sql files from
                                      DISK_PATH/protos and DISK_PATH/sql
                                      respectively, and mounts them onto
                                      VIRTUAL_PATH.
)",
                argv[0]);
}

enum LongOption {
  OPT_HTTP_PORT = 1000,
  OPT_HTTP_IP,
  OPT_HTTP_ADDITIONAL_CORS_ORIGINS,
  OPT_STDIOD,

  OPT_FORCE_FULL_SORT,
  OPT_NO_FTRACE_RAW,

  OPT_ADD_SQL_PACKAGE,
  OPT_OVERRIDE_SQL_PACKAGE,
  OPT_STRUCTURED_QUERY_SPEC,
  OPT_STRUCTURED_QUERY_ID,

  OPT_SUMMARY,
  OPT_SUMMARY_METRICS_V2,
  OPT_SUMMARY_METADATA_QUERY,
  OPT_SUMMARY_SPEC,
  OPT_SUMMARY_FORMAT,

  OPT_METATRACE_BUFFER_CAPACITY,
  OPT_METATRACE_CATEGORIES,

  OPT_DEV,
  OPT_DEV_FLAG,
  OPT_EXTRA_CHECKS,
  OPT_ANALYZE_TRACE_PROTO_CONTENT,
  OPT_CROP_TRACK_EVENTS,
  OPT_REGISTER_FILES_DIR,
  OPT_OVERRIDE_STDLIB,

  OPT_RUN_METRICS,
  OPT_PRE_METRICS,
  OPT_METRICS_OUTPUT,
  OPT_METRIC_EXTENSION,

  OPT_HELP_CLASSIC,
};

constexpr char kShortOptions[] = "hvWiDdm:p:q:Q:e:";

const option kLongOptions[] = {
    {"help", no_argument, nullptr, 'h'},
    {"help-classic", no_argument, nullptr, OPT_HELP_CLASSIC},
    {"version", no_argument, nullptr, 'v'},

    {"httpd", no_argument, nullptr, 'D'},
    {"http-port", required_argument, nullptr, OPT_HTTP_PORT},
    {"http-ip-address", required_argument, nullptr, OPT_HTTP_IP},
    {"http-additional-cors-origins", required_argument, nullptr,
     OPT_HTTP_ADDITIONAL_CORS_ORIGINS},
    {"stdiod", no_argument, nullptr, OPT_STDIOD},
    {"interactive", no_argument, nullptr, 'i'},

    {"full-sort", no_argument, nullptr, OPT_FORCE_FULL_SORT},
    {"no-ftrace-raw", no_argument, nullptr, OPT_NO_FTRACE_RAW},

    {"query-file", required_argument, nullptr, 'q'},
    {"query-string", required_argument, nullptr, 'Q'},
    {"add-sql-package", required_argument, nullptr, OPT_ADD_SQL_PACKAGE},
    {"override-sql-package", required_argument, nullptr,
     OPT_OVERRIDE_SQL_PACKAGE},

    {"summary", no_argument, nullptr, OPT_SUMMARY},
    {"summary-metrics-v2", required_argument, nullptr, OPT_SUMMARY_METRICS_V2},
    {"summary-metadata-query", required_argument, nullptr,
     OPT_SUMMARY_METADATA_QUERY},
    {"summary-spec", required_argument, nullptr, OPT_SUMMARY_SPEC},
    {"summary-format", required_argument, nullptr, OPT_SUMMARY_FORMAT},

    {"metatrace", required_argument, nullptr, 'm'},
    {"metatrace-buffer-capacity", required_argument, nullptr,
     OPT_METATRACE_BUFFER_CAPACITY},
    {"metatrace-categories", required_argument, nullptr,
     OPT_METATRACE_CATEGORIES},

    {"dev", no_argument, nullptr, OPT_DEV},
    {"dev-flag", required_argument, nullptr, OPT_DEV_FLAG},
    {"extra-checks", no_argument, nullptr, OPT_EXTRA_CHECKS},
    {"export", required_argument, nullptr, 'e'},
    {"perf-file", required_argument, nullptr, 'p'},
    {"wide", no_argument, nullptr, 'W'},
    {"analyze-trace-proto-content", no_argument, nullptr,
     OPT_ANALYZE_TRACE_PROTO_CONTENT},
    {"crop-track-events", no_argument, nullptr, OPT_CROP_TRACK_EVENTS},
    {"register-files-dir", required_argument, nullptr, OPT_REGISTER_FILES_DIR},
    {"override-stdlib", required_argument, nullptr, OPT_OVERRIDE_STDLIB},

    {"run-metrics", required_argument, nullptr, OPT_RUN_METRICS},
    {"pre-metrics", required_argument, nullptr, OPT_PRE_METRICS},
    {"metrics-output", required_argument, nullptr, OPT_METRICS_OUTPUT},
    {"metric-extension", required_argument, nullptr, OPT_METRIC_EXTENSION},

    {nullptr, 0, nullptr, 0}};

CommandLineOptions ParseCommandLineOptions(int argc, char** argv) {
  CommandLineOptions command_line_options;

  bool explicit_interactive = false;
  for (;;) {
    int option = getopt_long(argc, argv, kShortOptions, kLongOptions, nullptr);

    if (option == -1)
      break;  // EOF.

    if (option == 'v') {
      printf("%s\n", base::GetVersionString());
      printf("Trace Processor RPC API version: %d\n",
             protos::pbzero::TRACE_PROCESSOR_CURRENT_API_VERSION);
      exit(0);
    }

    if (option == 'W') {
      command_line_options.wide = true;
      continue;
    }

    if (option == 'p') {
      command_line_options.perf_file_path = optarg;
      continue;
    }

    if (option == 'q') {
      command_line_options.query_file_path = optarg;
      continue;
    }

    if (option == 'Q') {
      command_line_options.query_string = optarg;
      continue;
    }

    if (option == 'D') {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
      command_line_options.enable_httpd = true;
#else
      PERFETTO_FATAL("HTTP RPC module not supported in this build");
#endif
      continue;
    }

    if (option == OPT_HTTP_PORT) {
      command_line_options.port_number = optarg;
      continue;
    }

    if (option == OPT_HTTP_IP) {
      command_line_options.listen_ip = optarg;
      continue;
    }

    if (option == OPT_HTTP_ADDITIONAL_CORS_ORIGINS) {
      command_line_options.additional_cors_origins =
          base::SplitString(optarg, ",");
      continue;
    }

    if (option == OPT_STDIOD) {
      command_line_options.enable_stdiod = true;
      continue;
    }

    if (option == 'i') {
      explicit_interactive = true;
      continue;
    }

    if (option == 'e') {
      command_line_options.export_file_path = optarg;
      continue;
    }

    if (option == 'm') {
      command_line_options.metatrace_path = optarg;
      continue;
    }

    if (option == OPT_METATRACE_BUFFER_CAPACITY) {
      command_line_options.metatrace_buffer_capacity =
          static_cast<size_t>(atoi(optarg));
      continue;
    }

    if (option == OPT_METATRACE_CATEGORIES) {
      command_line_options.metatrace_categories =
          ParseMetatraceCategories(optarg);
      continue;
    }

    if (option == OPT_FORCE_FULL_SORT) {
      command_line_options.force_full_sort = true;
      continue;
    }

    if (option == OPT_NO_FTRACE_RAW) {
      command_line_options.no_ftrace_raw = true;
      continue;
    }

    if (option == OPT_ANALYZE_TRACE_PROTO_CONTENT) {
      command_line_options.analyze_trace_proto_content = true;
      continue;
    }

    if (option == OPT_CROP_TRACK_EVENTS) {
      command_line_options.crop_track_events = true;
      continue;
    }

    if (option == OPT_DEV) {
      command_line_options.dev = true;
      continue;
    }

    if (option == OPT_EXTRA_CHECKS) {
      command_line_options.extra_checks = true;
      continue;
    }

    if (option == OPT_ADD_SQL_PACKAGE) {
      command_line_options.sql_package_paths.emplace_back(optarg);
      continue;
    }

    if (option == OPT_OVERRIDE_SQL_PACKAGE) {
      command_line_options.override_sql_package_paths.emplace_back(optarg);
      continue;
    }

    if (option == OPT_OVERRIDE_STDLIB) {
      command_line_options.override_stdlib_path = optarg;
      continue;
    }

    if (option == OPT_RUN_METRICS) {
      command_line_options.metric_v1_names = optarg;
      continue;
    }

    if (option == OPT_PRE_METRICS) {
      command_line_options.pre_metrics_v1_path = optarg;
      continue;
    }

    if (option == OPT_METRICS_OUTPUT) {
      command_line_options.metric_v1_output = optarg;
      continue;
    }

    if (option == OPT_METRIC_EXTENSION) {
      command_line_options.raw_metric_v1_extensions.emplace_back(optarg);
      continue;
    }

    if (option == OPT_DEV_FLAG) {
      command_line_options.dev_flags.emplace_back(optarg);
      continue;
    }

    if (option == OPT_REGISTER_FILES_DIR) {
      command_line_options.register_files_dir = optarg;
      continue;
    }

    if (option == OPT_SUMMARY) {
      command_line_options.summary = true;
      continue;
    }

    if (option == OPT_SUMMARY_METRICS_V2) {
      command_line_options.summary_metrics_v2 = optarg;
      continue;
    }

    if (option == OPT_SUMMARY_METADATA_QUERY) {
      command_line_options.summary_metadata_query = optarg;
      continue;
    }

    if (option == OPT_SUMMARY_SPEC) {
      command_line_options.summary_specs.emplace_back(optarg);
      continue;
    }

    if (option == OPT_SUMMARY_FORMAT) {
      command_line_options.summary_output = optarg;
      continue;
    }

    if (option == OPT_HELP_CLASSIC) {
      PrintClassicUsage(argv);
      exit(0);
    }
    if (option == 'h') {
      PrintSubcommandHelp(argv[0]);
      exit(0);
    }
    PrintClassicUsage(argv);
    exit(1);
  }

  command_line_options.launch_shell =
      explicit_interactive || (command_line_options.metric_v1_names.empty() &&
                               command_line_options.query_file_path.empty() &&
                               command_line_options.query_string.empty() &&
                               command_line_options.export_file_path.empty() &&
                               !command_line_options.summary);

  // Only allow non-interactive queries to emit perf data.
  if (!command_line_options.perf_file_path.empty() &&
      command_line_options.launch_shell) {
    PrintClassicUsage(argv);
    exit(1);
  }

  if (command_line_options.summary &&
      !command_line_options.metric_v1_names.empty()) {
    PERFETTO_ELOG("Cannot specify both metrics v1 and trace summarization");
    exit(1);
  }

  // The only case where we allow omitting the trace file path is when running
  // in --httpd or --stdiod mode. In all other cases, the last argument must be
  // the trace file.
  if (optind == argc - 1 && argv[optind]) {
    command_line_options.trace_file_path = argv[optind];
  } else if (!command_line_options.enable_httpd &&
             !command_line_options.enable_stdiod) {
    PrintClassicUsage(argv);
    exit(1);
  }

  return command_line_options;
}

class DefaultPlatformInterface : public TraceProcessorShell::PlatformInterface {
 public:
  ~DefaultPlatformInterface() override;

  Config DefaultConfig() const override { return {}; }

  base::Status OnTraceProcessorCreated(TraceProcessor*) override {
    return base::OkStatus();
  }

  base::Status LoadTrace(
      TraceProcessor* trace_processor,
      const std::string& path,
      std::function<void(size_t)> progress_callback) override {
    return ReadTraceUnfinalized(trace_processor, path.c_str(),
                                progress_callback);
  }
};

DefaultPlatformInterface::~DefaultPlatformInterface() = default;

}  // namespace

TraceProcessorShell::TraceProcessorShell(
    std::unique_ptr<PlatformInterface> platform_interface)
    : platform_interface_(std::move(platform_interface)) {}

std::unique_ptr<TraceProcessorShell> TraceProcessorShell::Create(
    std::unique_ptr<PlatformInterface> platform_interface) {
  return std::unique_ptr<TraceProcessorShell>(
      new TraceProcessorShell(std::move(platform_interface)));
}

std::unique_ptr<TraceProcessorShell>
TraceProcessorShell::CreateWithDefaultPlatform() {
  return std::unique_ptr<TraceProcessorShell>(
      new TraceProcessorShell(std::make_unique<DefaultPlatformInterface>()));
}

base::Status TraceProcessorShell::Run(int argc, char** argv) {
  // Subcommand dispatch: if a positional argument matches a known subcommand
  // name, route to it. Otherwise fall through to classic path.
  {
    shell::QuerySubcommand query_subcommand;
    shell::InteractiveSubcommand interactive_subcommand;
    shell::ServerSubcommand server_subcommand;
    shell::SummarizeSubcommand summarize_subcommand;
    shell::ExportSubcommand export_subcommand;
    shell::MetricsSubcommand metrics_subcommand;
    shell::ConvertSubcommand convert_subcommand;
    std::vector<shell::Subcommand*> subcommands = {
        &query_subcommand,     &interactive_subcommand, &server_subcommand,
        &summarize_subcommand, &export_subcommand,      &metrics_subcommand,
        &convert_subcommand,
    };

    // Handle "help" pseudo-subcommand: `tp help <command>` or bare `tp help`.
    for (int i = 1; i < argc; ++i) {
      if (argv[i][0] == '-')
        continue;
      if (strcmp(argv[i], "help") == 0) {
        if (i + 1 < argc) {
          // `help <command>` -- find the named subcommand and print its usage.
          for (auto* sc : subcommands) {
            if (strcmp(sc->name(), argv[i + 1]) == 0) {
              printf("%s", shell::FormatSubcommandUsage(argv[0], sc).c_str());
              return base::OkStatus();
            }
          }
          return base::ErrStatus("Unknown command '%s'.", argv[i + 1]);
        }
        // Bare `help` -- same as --help.
        PrintSubcommandHelp(argv[0]);
        return base::OkStatus();
      }
      break;  // First non-flag, non-help positional -> stop.
    }

    // Build the set of flags that consume an argument, derived from the
    // classic kLongOptions array, kShortOptions, and subcommand FlagSpecs.
    std::unordered_set<std::string> flags_with_arg;
    for (const auto* o = kLongOptions; o->name; ++o) {
      if (o->has_arg == required_argument)
        flags_with_arg.insert("--" + std::string(o->name));
    }
    for (const char* p = kShortOptions; *p; ++p) {
      if (*(p + 1) == ':') {
        flags_with_arg.insert(std::string("-") + *p);
        ++p;  // skip ':'
      }
    }
    for (auto* sc : subcommands) {
      for (const auto& f : sc->GetFlags()) {
        if (f.has_arg) {
          flags_with_arg.insert("--" + std::string(f.long_name));
          if (f.short_name)
            flags_with_arg.insert(std::string(1, '-') + f.short_name);
        }
      }
    }

    auto result =
        shell::FindSubcommandInArgs(argc, argv, subcommands, flags_with_arg);
    if (result.subcommand) {
      // If the matched word is also a file on disk, it's ambiguous -- the user
      // might have meant it as a trace file. Emit a hint.
      if (base::FileExists(argv[result.argv_index])) {
        PERFETTO_ELOG(
            "Note: '%s' matches both a subcommand and a file on disk. "
            "Interpreting as subcommand. To use it as a trace file, "
            "use './%s'.",
            argv[result.argv_index], argv[result.argv_index]);
      }

      // Remove the subcommand name from argv.
      for (int i = result.argv_index; i < argc - 1; ++i)
        argv[i] = argv[i + 1];
      argc--;

      shell::GlobalOptions global;
      shell::SubcommandContext ctx;
      ctx.platform = platform_interface_.get();
      ctx.global = &global;

      auto usage = shell::FormatSubcommandUsage(argv[0], result.subcommand);
      {
        auto parse_status =
            shell::ParseFlags(result.subcommand, &ctx, argc, argv);
        if (!parse_status.ok()) {
          bool already_printed =
              parse_status.GetPayload("perfetto.dev/has_printed_error")
                  .has_value();
          if (already_printed) {
            return base::ErrStatus("\n%s", usage.c_str());
          }
          return base::ErrStatus("%s\n\n%s", parse_status.c_message(),
                                 usage.c_str());
        }
      }
      if (global.help) {
        printf("%s", usage.c_str());
        return base::OkStatus();
      }
      if (global.version) {
        printf("%s\n", base::GetVersionString());
        printf("Trace Processor RPC API version: %d\n",
               protos::pbzero::TRACE_PROCESSOR_CURRENT_API_VERSION);
        return base::OkStatus();
      }

      // Parse metric extensions and populate their descriptor pool. The
      // pool is always created (built-in metrics need it for output
      // formatting); extensions just add to it.
      RETURN_IF_ERROR(ParseMetricExtensionPaths(global.dev,
                                                global.raw_metric_v1_extensions,
                                                global.metric_extensions));
      global.metric_descriptor_pool =
          std::make_unique<google::protobuf::DescriptorPool>(
              google::protobuf::DescriptorPool::generated_pool());
      RETURN_IF_ERROR(PopulateDescriptorPool(*global.metric_descriptor_pool,
                                             global.metric_extensions));

      return result.subcommand->Run(ctx);
    }
  }

  // No arguments at all: show the subcommand-based help.
  if (argc == 1) {
    PrintSubcommandHelp(argv[0]);
    return base::OkStatus();
  }

  // Classic flag path: translate classic flags into a subcommand invocation
  // and re-dispatch through the subcommand machinery above.
  CommandLineOptions options = ParseCommandLineOptions(argc, argv);

  // Build a synthetic argv for the target subcommand.
  std::vector<std::string> args;
  args.emplace_back(argv[0]);

  // Forward global flags.
  auto add_global_flags = [&]() {
    if (options.force_full_sort)
      args.emplace_back("--full-sort");
    if (options.no_ftrace_raw)
      args.emplace_back("--no-ftrace-raw");
    if (options.analyze_trace_proto_content)
      args.emplace_back("--analyze-trace-proto-content");
    if (options.crop_track_events)
      args.emplace_back("--crop-track-events");
    if (options.dev)
      args.emplace_back("--dev");
    for (const auto& f : options.dev_flags) {
      args.emplace_back("--dev-flag");
      args.emplace_back(f);
    }
    if (options.extra_checks)
      args.emplace_back("--extra-checks");
    for (const auto& p : options.sql_package_paths) {
      args.emplace_back("--add-sql-package");
      args.emplace_back(p);
    }
    for (const auto& p : options.override_sql_package_paths) {
      args.emplace_back("--override-sql-package");
      args.emplace_back(p);
    }
    if (!options.override_stdlib_path.empty()) {
      args.emplace_back("--override-stdlib");
      args.emplace_back(options.override_stdlib_path);
    }
    if (!options.register_files_dir.empty()) {
      args.emplace_back("--register-files-dir");
      args.emplace_back(options.register_files_dir);
    }
    for (const auto& e : options.raw_metric_v1_extensions) {
      args.emplace_back("--metric-extension");
      args.emplace_back(e);
    }
    if (!options.metatrace_path.empty()) {
      args.emplace_back("--metatrace");
      args.emplace_back(options.metatrace_path);
    }
    if (options.metatrace_buffer_capacity > 0) {
      args.emplace_back("--metatrace-buffer-capacity");
      args.emplace_back(std::to_string(options.metatrace_buffer_capacity));
    }
  };

  // Determine which subcommand to dispatch to.
  if (options.enable_httpd) {
    args.emplace_back("server");
    args.emplace_back("http");
    if (!options.port_number.empty()) {
      args.emplace_back("--port");
      args.emplace_back(options.port_number);
    }
    if (!options.listen_ip.empty()) {
      args.emplace_back("--ip-address");
      args.emplace_back(options.listen_ip);
    }
    for (const auto& o : options.additional_cors_origins) {
      args.emplace_back("--additional-cors-origins");
      args.emplace_back(o);
    }
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
  } else if (options.enable_stdiod) {
    args.emplace_back("server");
    args.emplace_back("stdio");
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
  } else if (options.summary) {
    args.emplace_back("summarize");
    if (!options.summary_metrics_v2.empty()) {
      args.emplace_back("--metrics-v2");
      args.emplace_back(options.summary_metrics_v2);
    }
    if (!options.summary_metadata_query.empty()) {
      args.emplace_back("--metadata-query");
      args.emplace_back(options.summary_metadata_query);
    }
    if (!options.summary_output.empty()) {
      args.emplace_back("--format");
      args.emplace_back(options.summary_output);
    }
    if (!options.query_file_path.empty()) {
      args.emplace_back("--post-query");
      args.emplace_back(options.query_file_path);
    }
    if (!options.perf_file_path.empty()) {
      args.emplace_back("--perf-file");
      args.emplace_back(options.perf_file_path);
    }
    if (options.launch_shell)
      args.emplace_back("-i");
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
    for (const auto& s : options.summary_specs)
      args.emplace_back(s);
  } else if (!options.metric_v1_names.empty()) {
    args.emplace_back("metrics");
    args.emplace_back("--run");
    args.emplace_back(options.metric_v1_names);
    if (!options.pre_metrics_v1_path.empty()) {
      args.emplace_back("--pre");
      args.emplace_back(options.pre_metrics_v1_path);
    }
    if (!options.metric_v1_output.empty()) {
      args.emplace_back("--output");
      args.emplace_back(options.metric_v1_output);
    }
    if (!options.query_file_path.empty()) {
      args.emplace_back("--post-query");
      args.emplace_back(options.query_file_path);
    }
    if (!options.perf_file_path.empty()) {
      args.emplace_back("--perf-file");
      args.emplace_back(options.perf_file_path);
    }
    if (options.launch_shell)
      args.emplace_back("-i");
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
  } else if (!options.export_file_path.empty()) {
    if (!options.query_file_path.empty()) {
      return base::ErrStatus(
          "Cannot combine -e (export) with -q (query). "
          "Use the 'export' subcommand directly.");
    }
    args.emplace_back("export");
    args.emplace_back("sqlite");
    args.emplace_back("-o");
    args.emplace_back(options.export_file_path);
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
  } else if (!options.query_file_path.empty() ||
             !options.query_string.empty()) {
    args.emplace_back("query");
    if (!options.query_file_path.empty()) {
      args.emplace_back("-f");
      args.emplace_back(options.query_file_path);
    }
    if (options.launch_shell)
      args.emplace_back("-i");
    if (options.wide)
      args.emplace_back("-W");
    if (!options.perf_file_path.empty()) {
      args.emplace_back("--perf-file");
      args.emplace_back(options.perf_file_path);
    }
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
    if (!options.query_string.empty())
      args.emplace_back(options.query_string);
  } else {
    // Default: interactive shell.
    args.emplace_back("interactive");
    if (options.wide)
      args.emplace_back("-W");
    add_global_flags();
    if (!options.trace_file_path.empty())
      args.emplace_back(options.trace_file_path);
  }

  // Convert to argc/argv and re-enter Run() which will match the subcommand.
  std::vector<char*> new_argv;
  new_argv.reserve(args.size());
  for (auto& a : args)
    new_argv.emplace_back(a.data());
  return Run(static_cast<int>(new_argv.size()), new_argv.data());
}

TraceProcessorShell_PlatformInterface::
    ~TraceProcessorShell_PlatformInterface() = default;

int PERFETTO_EXPORT_ENTRYPOINT TraceProcessorShellMain(int argc, char** argv) {
  auto shell = TraceProcessorShell::CreateWithDefaultPlatform();
  auto status = shell->Run(argc, argv);
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.c_message());
    return 1;
  }
  return 0;
}

}  // namespace perfetto::trace_processor
