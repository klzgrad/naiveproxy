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

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"  // IWYU pragma: keep
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/metatrace_config.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/metrics/all_chrome_metrics.descriptor.h"
#include "src/trace_processor/metrics/all_webview_metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.descriptor.h"
#include "src/trace_processor/read_trace_internal.h"
#include "src/trace_processor/rpc/rpc.h"
#include "src/trace_processor/rpc/stdiod.h"
#include "src/trace_processor/shell/interactive.h"
#include "src/trace_processor/shell/metatrace.h"
#include "src/trace_processor/shell/metrics.h"
#include "src/trace_processor/shell/query.h"
#include "src/trace_processor/shell/shell_utils.h"
#include "src/trace_processor/shell/sql_packages.h"
#include "src/trace_processor/trace_summary/summary.h"
#include "src/trace_processor/util/deobfuscation/deobfuscator.h"
#include "src/trace_processor/util/sql_modules.h"
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

// Forward declaration.
TraceSummarySpecBytes::Format GuessSummarySpecFormat(
    const std::string& path,
    const std::string& content);

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
  std::vector<std::string> structured_query_specs;
  std::string structured_query_id;
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

void PrintUsage(char** argv) {
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

CommandLineOptions ParseCommandLineOptions(int argc, char** argv) {
  CommandLineOptions command_line_options;
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
  };

  static const option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
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
      {"structured-query-spec", required_argument, nullptr,
       OPT_STRUCTURED_QUERY_SPEC},
      {"structured-query-id", required_argument, nullptr,
       OPT_STRUCTURED_QUERY_ID},
      {"add-sql-package", required_argument, nullptr, OPT_ADD_SQL_PACKAGE},
      {"override-sql-package", required_argument, nullptr,
       OPT_OVERRIDE_SQL_PACKAGE},

      {"summary", no_argument, nullptr, OPT_SUMMARY},
      {"summary-metrics-v2", required_argument, nullptr,
       OPT_SUMMARY_METRICS_V2},
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
      {"register-files-dir", required_argument, nullptr,
       OPT_REGISTER_FILES_DIR},
      {"override-stdlib", required_argument, nullptr, OPT_OVERRIDE_STDLIB},

      {"run-metrics", required_argument, nullptr, OPT_RUN_METRICS},
      {"pre-metrics", required_argument, nullptr, OPT_PRE_METRICS},
      {"metrics-output", required_argument, nullptr, OPT_METRICS_OUTPUT},
      {"metric-extension", required_argument, nullptr, OPT_METRIC_EXTENSION},

      {nullptr, 0, nullptr, 0}};

  bool explicit_interactive = false;
  for (;;) {
    int option =
        getopt_long(argc, argv, "hvWiDdm:p:q:Q:e:", long_options, nullptr);

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

    if (option == OPT_STRUCTURED_QUERY_SPEC) {
      command_line_options.structured_query_specs.emplace_back(optarg);
      continue;
    }

    if (option == OPT_STRUCTURED_QUERY_ID) {
      command_line_options.structured_query_id = optarg;
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

    PrintUsage(argv);
    exit(option == 'h' ? 0 : 1);
  }

  command_line_options.launch_shell =
      explicit_interactive ||
      (command_line_options.metric_v1_names.empty() &&
       command_line_options.query_file_path.empty() &&
       command_line_options.query_string.empty() &&
       command_line_options.structured_query_id.empty() &&
       command_line_options.export_file_path.empty() &&
       !command_line_options.summary);

  // Only allow non-interactive queries to emit perf data.
  if (!command_line_options.perf_file_path.empty() &&
      command_line_options.launch_shell) {
    PrintUsage(argv);
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
    PrintUsage(argv);
    exit(1);
  }

  return command_line_options;
}

base::Status LoadTrace(TraceProcessor* trace_processor,
                       TraceProcessorShell::PlatformInterface* platform,
                       const std::string& trace_file_path,
                       double* size_mb) {
  base::Status load_status = platform->LoadTrace(
      trace_processor, trace_file_path, [&size_mb](size_t parsed_size) {
        *size_mb = static_cast<double>(parsed_size) / 1E6;
        fprintf(stderr, "\rLoading trace: %.2f MB\r", *size_mb);
      });
  if (!load_status.ok()) {
    return base::ErrStatus("Could not read trace file (path: %s): %s",
                           trace_file_path.c_str(), load_status.c_message());
  }

  bool is_proto_trace = false;
  {
    auto it = trace_processor->ExecuteQuery(
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
      trace_processor->Flush();
      auto sym_result = profiling::SymbolizeDatabaseAndLog(
          trace_processor, sym_config, /*verbose=*/false);
      if (sym_result.error == profiling::SymbolizerError::kOk &&
          !sym_result.symbols.empty()) {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[sym_result.symbols.size()]);
        memcpy(buf.get(), sym_result.symbols.data(), sym_result.symbols.size());
        auto status =
            trace_processor->Parse(std::move(buf), sym_result.symbols.size());
        if (!status.ok()) {
          PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s",
                                  status.message().c_str());
        }
      }
    } else {
      // TODO(lalitm): support symbolization for non-proto traces.
      PERFETTO_ELOG("Skipping symbolization for non-proto trace");
    }
  }
  auto maybe_map = profiling::GetPerfettoProguardMapPath();
  if (!maybe_map.empty()) {
    if (is_proto_trace) {
      trace_processor->Flush();
      profiling::ReadProguardMapsToDeobfuscationPackets(
          maybe_map, [trace_processor](const std::string& trace_proto) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[trace_proto.size()]);
            memcpy(buf.get(), trace_proto.data(), trace_proto.size());
            auto status =
                trace_processor->Parse(std::move(buf), trace_proto.size());
            if (!status.ok()) {
              PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s",
                                      status.message().c_str());
              return;
            }
          });
    } else {
      // TODO(lalitm): support deobfuscation for non-proto traces.
      PERFETTO_ELOG("Skipping deobfuscation for non-proto trace");
    }
  }
  return trace_processor->NotifyEndOfFile();
}

MetricV1OutputFormat ParseMetricV1OutputFormat(
    const CommandLineOptions& options) {
  if (!options.query_file_path.empty())
    return MetricV1OutputFormat::kNone;
  if (options.metric_v1_output == "binary")
    return MetricV1OutputFormat::kBinaryProto;
  if (options.metric_v1_output == "json")
    return MetricV1OutputFormat::kJson;
  return MetricV1OutputFormat::kTextProto;
}

base::Status MaybeUpdateSqlPackages(TraceProcessor* trace_processor,
                                    const CommandLineOptions& options) {
  if (!options.override_stdlib_path.empty()) {
    if (!options.dev)
      return base::ErrStatus("Overriding stdlib requires --dev flag");

    auto status =
        LoadOverridenStdlib(trace_processor, options.override_stdlib_path);
    if (!status.ok())
      return base::ErrStatus("Couldn't override stdlib: %s",
                             status.c_message());
  }

  if (!options.override_sql_package_paths.empty()) {
    for (const auto& override_sql_package_path :
         options.override_sql_package_paths) {
      auto status =
          IncludeSqlPackage(trace_processor, override_sql_package_path, true);
      if (!status.ok())
        return base::ErrStatus("Couldn't override stdlib package: %s",
                               status.c_message());
    }
  }

  if (!options.sql_package_paths.empty()) {
    for (const auto& add_sql_package_path : options.sql_package_paths) {
      auto status =
          IncludeSqlPackage(trace_processor, add_sql_package_path, false);
      if (!status.ok())
        return base::ErrStatus("Couldn't add SQL package: %s",
                               status.c_message());
    }
  }
  return base::OkStatus();
}

TraceSummarySpecBytes::Format GuessSummarySpecFormat(
    const std::string& path,
    const std::string& content) {
  if (base::EndsWith(path, ".pb")) {
    return TraceSummarySpecBytes::Format::kBinaryProto;
  }
  if (base::EndsWith(path, ".textproto")) {
    return TraceSummarySpecBytes::Format::kTextProto;
  }
  std::string_view content_str(content.c_str(),
                               std::min<size_t>(content.size(), 128));
  auto fn = [](const char c) { return std::isspace(c) || std::isprint(c); };
  if (std::all_of(content_str.begin(), content_str.end(), fn)) {
    return TraceSummarySpecBytes::Format::kTextProto;
  }
  return TraceSummarySpecBytes::Format::kBinaryProto;
}

TraceSummaryOutputSpec::Format GetSummaryOutputFormat(
    const CommandLineOptions& options) {
  if (options.summary_output == "text" || options.summary_output == "") {
    return TraceSummaryOutputSpec::Format::kTextProto;
  }
  if (options.summary_output == "binary") {
    return TraceSummaryOutputSpec::Format::kBinaryProto;
  }
  PERFETTO_ELOG("Unknown summary output format %s",
                options.summary_output.c_str());
  exit(1);
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
  CommandLineOptions options = ParseCommandLineOptions(argc, argv);

  Config config = platform_interface_->DefaultConfig();
  config.sorting_mode = options.force_full_sort
                            ? SortingMode::kForceFullSort
                            : SortingMode::kDefaultHeuristics;
  config.ingest_ftrace_in_raw_table = !options.no_ftrace_raw;
  config.analyze_trace_proto_content = options.analyze_trace_proto_content;
  config.drop_track_event_data_before =
      options.crop_track_events
          ? DropTrackEventDataBefore::kTrackEventRangeOfInterest
          : DropTrackEventDataBefore::kNoDrop;

  std::vector<MetricExtension> metric_extensions;
  RETURN_IF_ERROR(ParseMetricExtensionPaths(
      options.dev, options.raw_metric_v1_extensions, metric_extensions));

  for (const auto& extension : metric_extensions) {
    config.skip_builtin_metric_paths.push_back(extension.virtual_path());
  }

  if (options.dev) {
    config.enable_dev_features = true;
    for (const auto& flag_pair : options.dev_flags) {
      auto kv = base::SplitString(flag_pair, "=");
      if (kv.size() != 2) {
        PERFETTO_ELOG("Ignoring unknown dev flag format %s", flag_pair.c_str());
        continue;
      }
      config.dev_flags.emplace(kv[0], kv[1]);
    }
  }

  if (options.extra_checks) {
    config.enable_extra_checks = true;
  }

  std::unique_ptr<TraceProcessor> tp = TraceProcessor::CreateInstance(config);
  platform_interface_->OnTraceProcessorCreated(tp.get());
  RETURN_IF_ERROR(MaybeUpdateSqlPackages(tp.get(), options));

  // Enable metatracing as soon as possible.
  if (!options.metatrace_path.empty()) {
    metatrace::MetatraceConfig metatrace_config;
    metatrace_config.override_buffer_size = options.metatrace_buffer_capacity;
    metatrace_config.categories = options.metatrace_categories;
    tp->EnableMetatrace(metatrace_config);
  }

  if (!options.register_files_dir.empty()) {
    RETURN_IF_ERROR(RegisterAllFilesInFolder(options.register_files_dir, *tp));
  }

  // Descriptor pool used for printing output as textproto. Building on top of
  // generated pool so default protos in google.protobuf.descriptor.proto are
  // available.
  // For some insane reason, the descriptor pool is not movable so we need to
  // create it here so we can create references and pass it everywhere.
  google::protobuf::DescriptorPool pool(
      google::protobuf::DescriptorPool::generated_pool());
  RETURN_IF_ERROR(PopulateDescriptorPool(pool, metric_extensions));

  // We load all the metric extensions even when --run-metrics arg is not there,
  // because we want the metrics to be available in interactive mode or when
  // used in UI using httpd.
  // Metric extensions are also used to populate the descriptor pool.
  for (const auto& extension : metric_extensions) {
    RETURN_IF_ERROR(LoadMetricExtension(tp.get(), extension, pool));
  }

  base::TimeNanos t_load{};
  if (!options.trace_file_path.empty()) {
    base::TimeNanos t_load_start = base::GetWallTimeNs();
    double size_mb = 0;
    RETURN_IF_ERROR(LoadTrace(tp.get(), platform_interface_.get(),
                              options.trace_file_path, &size_mb));
    t_load = base::GetWallTimeNs() - t_load_start;

    double t_load_s = static_cast<double>(t_load.count()) / 1E9;
    PERFETTO_ILOG("Trace loaded: %.2f MB in %.2fs (%.1f MB/s)", size_mb,
                  t_load_s, size_mb / t_load_s);

    RETURN_IF_ERROR(PrintStats(tp.get()));
  }

#if PERFETTO_HAS_SIGNAL_H()
  // Set up interrupt signal to allow the user to abort query.
  static TraceProcessor* g_tp_for_signal_handler = tp.get();
  signal(SIGINT, [](int) { g_tp_for_signal_handler->InterruptQuery(); });
#endif

  base::TimeNanos t_query_start = base::GetWallTimeNs();
  if (!options.pre_metrics_v1_path.empty()) {
    RETURN_IF_ERROR(
        RunQueriesFromFile(tp.get(), options.pre_metrics_v1_path, false));
  }

  // Trace summarization
  if (options.summary) {
    PERFETTO_CHECK(options.metric_v1_names.empty());

    std::vector<std::string> spec_content;
    spec_content.reserve(options.summary_specs.size());
    for (const auto& s : options.summary_specs) {
      spec_content.emplace_back();
      if (!base::ReadFile(s, &spec_content.back())) {
        return base::ErrStatus("Unable to read summary spec file %s",
                               s.c_str());
      }
    }

    std::vector<TraceSummarySpecBytes> specs;
    specs.reserve(options.summary_specs.size());
    for (uint32_t i = 0; i < options.summary_specs.size(); ++i) {
      specs.emplace_back(TraceSummarySpecBytes{
          reinterpret_cast<const uint8_t*>(spec_content[i].data()),
          spec_content[i].size(),
          GuessSummarySpecFormat(options.summary_specs[i], spec_content[i]),
      });
    }

    TraceSummaryComputationSpec computation_config;

    if (options.summary_metrics_v2.empty()) {
      computation_config.v2_metric_ids = std::vector<std::string>();
    } else if (base::CaseInsensitiveEqual(options.summary_metrics_v2, "all")) {
      computation_config.v2_metric_ids = std::nullopt;
    } else {
      computation_config.v2_metric_ids =
          base::SplitString(options.summary_metrics_v2, ",");
    }

    computation_config.metadata_query_id =
        options.summary_metadata_query.empty()
            ? std::nullopt
            : std::make_optional(options.summary_metadata_query);

    TraceSummaryOutputSpec output_spec;
    output_spec.format = GetSummaryOutputFormat(options);

    std::vector<uint8_t> output;
    RETURN_IF_ERROR(
        tp->Summarize(computation_config, specs, &output, output_spec));
    if (options.query_file_path.empty()) {
      fwrite(output.data(), sizeof(char), output.size(), stdout);
    }
  }

  // v1 metrics.
  std::vector<MetricNameAndPath> metrics;
  if (!options.metric_v1_names.empty()) {
    PERFETTO_CHECK(!options.summary);
    RETURN_IF_ERROR(
        LoadMetrics(tp.get(), options.metric_v1_names, pool, metrics));
  }

  MetricV1OutputFormat metric_format = ParseMetricV1OutputFormat(options);
  if (!metrics.empty()) {
    RETURN_IF_ERROR(RunMetrics(tp.get(), metrics, metric_format));
  }

  if (!options.query_file_path.empty()) {
    base::Status status =
        RunQueriesFromFile(tp.get(), options.query_file_path, true);
    if (!status.ok()) {
      // Write metatrace if needed before exiting.
      RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), options.metatrace_path));
      return status;
    }
  }

  if (!options.query_string.empty()) {
    base::Status status = RunQueries(tp.get(), options.query_string, true);
    if (!status.ok()) {
      // Write metatrace if needed before exiting.
      RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), options.metatrace_path));
      return status;
    }
  }

  if (!options.structured_query_id.empty()) {
    // Load spec files.
    std::vector<std::string> spec_content;
    spec_content.reserve(options.structured_query_specs.size());
    for (const auto& s : options.structured_query_specs) {
      spec_content.emplace_back();
      if (!base::ReadFile(s, &spec_content.back())) {
        return base::ErrStatus("Unable to read structured query spec file %s",
                               s.c_str());
      }
    }

    // Convert to TraceSummarySpecBytes.
    std::vector<TraceSummarySpecBytes> specs;
    specs.reserve(options.structured_query_specs.size());
    for (uint32_t i = 0; i < options.structured_query_specs.size(); ++i) {
      specs.emplace_back(TraceSummarySpecBytes{
          reinterpret_cast<const uint8_t*>(spec_content[i].data()),
          spec_content[i].size(),
          GuessSummarySpecFormat(options.structured_query_specs[i],
                                 spec_content[i]),
      });
    }

    // Execute the structured query.
    std::string output;
    base::Status status = summary::ExecuteStructuredQuery(
        tp.get(), specs, options.structured_query_id, &output);
    if (!status.ok()) {
      // Write metatrace if needed before exiting.
      RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), options.metatrace_path));
      return status;
    }

    // Print the result.
    fprintf(stdout, "%s", output.c_str());
  }

  base::TimeNanos t_query = base::GetWallTimeNs() - t_query_start;

  if (!options.export_file_path.empty()) {
    RETURN_IF_ERROR(ExportTraceToDatabase(tp.get(), options.export_file_path));
  }

  if (options.enable_httpd) {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
    Rpc rpc(std::move(tp), !options.trace_file_path.empty(), config,
            [this](TraceProcessor* tp) {
              platform_interface_->OnTraceProcessorCreated(tp);
            });

#if PERFETTO_HAS_SIGNAL_H()
    static Rpc* g_rpc_for_signal_handler = &rpc;

    if (options.metatrace_path.empty()) {
      // Restore the default signal handler to allow the user to terminate
      // httpd server via Ctrl-C.
      signal(SIGINT, SIG_DFL);
    } else {
      // Write metatrace to file before exiting.
      static std::string* metatrace_path = &options.metatrace_path;
      signal(SIGINT, [](int) {
        MaybeWriteMetatrace(g_rpc_for_signal_handler->trace_processor(),
                            *metatrace_path);
        exit(1);
      });
    }
#endif
    std::vector<std::string> additional_cors_origins = base::SplitString(
        PERFETTO_BUILDFLAG(PERFETTO_HTTP_ADDITIONAL_CORS_ORIGINS), ",");

    for (const auto& origin : options.additional_cors_origins) {
      PERFETTO_ILOG("Adding additional CORS origin: %s", origin.c_str());
      additional_cors_origins.push_back(origin);
    }

    RunHttpRPCServer(
        /*rpc=*/rpc,
        /*listen_ip=*/options.listen_ip,
        /*port_number=*/options.port_number,
        /*additional_cors_origins=*/additional_cors_origins);
    PERFETTO_FATAL("Should never return");
#else
    PERFETTO_FATAL("HTTP not available");
#endif
  }

  if (options.enable_stdiod) {
    Rpc rpc(std::move(tp), !options.trace_file_path.empty(), config,
            [this](TraceProcessor* tp) {
              platform_interface_->OnTraceProcessorCreated(tp);
            });
#if PERFETTO_HAS_SIGNAL_H()
    static Rpc* g_rpc_for_signal_handler = &rpc;
    g_tp_for_signal_handler = nullptr;
    signal(SIGINT, [](int) {
      g_rpc_for_signal_handler->trace_processor()->InterruptQuery();
    });
#endif
    return RunStdioRpcServer(rpc);
  }

  if (options.launch_shell) {
    RETURN_IF_ERROR(StartInteractiveShell(
        tp.get(), InteractiveOptions{options.wide ? 40u : 20u, metric_format,
                                     metric_extensions, metrics, &pool}));
  } else if (!options.perf_file_path.empty()) {
    RETURN_IF_ERROR(PrintPerfFile(options.perf_file_path, t_load, t_query));
  }

  RETURN_IF_ERROR(MaybeWriteMetatrace(tp.get(), options.metatrace_path));

  return base::OkStatus();
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
