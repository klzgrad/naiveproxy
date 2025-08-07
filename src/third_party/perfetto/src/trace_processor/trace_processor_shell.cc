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

#include <fcntl.h>
#include <sys/stat.h>
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
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/profiling/deobfuscator.h"
#include "src/profiling/symbolizer/local_symbolizer.h"
#include "src/profiling/symbolizer/symbolize_database.h"
#include "src/profiling/symbolizer/symbolizer.h"
#include "src/trace_processor/metrics/all_chrome_metrics.descriptor.h"
#include "src/trace_processor/metrics/all_webview_metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.descriptor.h"
#include "src/trace_processor/read_trace_internal.h"
#include "src/trace_processor/rpc/stdiod.h"
#include "src/trace_processor/util/sql_modules.h"

#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
#include "src/trace_processor/rpc/httpd.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_HAS_SIGNAL_H() 1
#else
#define PERFETTO_HAS_SIGNAL_H() 0
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_TP_LINENOISE)
#include <linenoise.h>
#include <pwd.h>
#include <sys/types.h>
#endif

#if PERFETTO_HAS_SIGNAL_H()
#include <signal.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <io.h>
#define ftruncate _chsize
#else
#include <dirent.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_TP_LINENOISE) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <unistd.h>  // For getuid() in GetConfigPath().
#endif

namespace perfetto::trace_processor {

namespace {
TraceProcessor* g_tp;

#if PERFETTO_BUILDFLAG(PERFETTO_TP_LINENOISE)

bool EnsureDir(const std::string& path) {
  return base::Mkdir(path) || errno == EEXIST;
}

bool EnsureFile(const std::string& path) {
  return base::OpenFile(path, O_RDONLY | O_CREAT, 0644).get() != -1;
}

std::string GetConfigPath() {
  const char* homedir = getenv("HOME");
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  if (homedir == nullptr)
    homedir = getpwuid(getuid())->pw_dir;
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  if (homedir == nullptr)
    homedir = getenv("USERPROFILE");
#endif
  if (homedir == nullptr)
    return "";
  return std::string(homedir) + "/.config";
}

std::string GetPerfettoPath() {
  std::string config = GetConfigPath();
  if (config.empty())
    return "";
  return config + "/perfetto";
}

std::string GetHistoryPath() {
  std::string perfetto = GetPerfettoPath();
  if (perfetto.empty())
    return "";
  return perfetto + "/.trace_processor_shell_history";
}

void SetupLineEditor() {
  linenoiseSetMultiLine(true);
  linenoiseHistorySetMaxLen(1000);

  bool success = !GetHistoryPath().empty();
  success = success && EnsureDir(GetConfigPath());
  success = success && EnsureDir(GetPerfettoPath());
  success = success && EnsureFile(GetHistoryPath());
  success = success && linenoiseHistoryLoad(GetHistoryPath().c_str()) != -1;
  if (!success) {
    PERFETTO_PLOG("Could not load history from %s", GetHistoryPath().c_str());
  }
}

struct LineDeleter {
  void operator()(char* p) const {
    linenoiseHistoryAdd(p);
    linenoiseHistorySave(GetHistoryPath().c_str());
    linenoiseFree(p);
  }
};

using ScopedLine = std::unique_ptr<char, LineDeleter>;

ScopedLine GetLine(const char* prompt) {
  errno = 0;
  auto line = ScopedLine(linenoise(prompt));
  // linenoise returns a nullptr both for CTRL-C and CTRL-D, however in the
  // former case it sets errno to EAGAIN.
  // If the user press CTRL-C return "" instead of nullptr. We don't want the
  // main loop to quit in that case as that is inconsistent with the behavior
  // "CTRL-C interrupts the current query" and frustrating when hitting that
  // a split second after the query is done.
  if (!line && errno == EAGAIN)
    return ScopedLine(strdup(""));
  return line;
}

#else

void SetupLineEditor() {}

using ScopedLine = std::unique_ptr<char>;

ScopedLine GetLine(const char* prompt) {
  printf("\r%80s\r%s", "", prompt);
  fflush(stdout);
  ScopedLine line(new char[1024]);
  if (!fgets(line.get(), 1024 - 1, stdin))
    return nullptr;
  if (strlen(line.get()) > 0)
    line.get()[strlen(line.get()) - 1] = 0;
  return line;
}

#endif  // PERFETTO_TP_LINENOISE

base::Status PrintStats() {
  auto it = g_tp->ExecuteQuery(
      "SELECT name, idx, source, value from stats "
      "where severity IN ('error', 'data_loss') and value > 0");

  bool first = true;
  while (it.Next()) {
    if (first) {
      fprintf(stderr, "Error stats for this trace:\n");

      for (uint32_t i = 0; i < it.ColumnCount(); i++)
        fprintf(stderr, "%40s ", it.GetColumnName(i).c_str());
      fprintf(stderr, "\n");

      for (uint32_t i = 0; i < it.ColumnCount(); i++)
        fprintf(stderr, "%40s ", "----------------------------------------");
      fprintf(stderr, "\n");

      first = false;
    }

    for (uint32_t c = 0; c < it.ColumnCount(); c++) {
      auto value = it.Get(c);
      switch (value.type) {
        case SqlValue::Type::kNull:
          fprintf(stderr, "%-40.40s", "[NULL]");
          break;
        case SqlValue::Type::kDouble:
          fprintf(stderr, "%40f", value.double_value);
          break;
        case SqlValue::Type::kLong:
          fprintf(stderr, "%40" PRIi64, value.long_value);
          break;
        case SqlValue::Type::kString:
          fprintf(stderr, "%-40.40s", value.string_value);
          break;
        case SqlValue::Type::kBytes:
          printf("%-40.40s", "<raw bytes>");
          break;
      }
      fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");
  }

  base::Status status = it.Status();
  if (!status.ok()) {
    return base::ErrStatus("Error while iterating stats (%s)",
                           status.c_message());
  }
  return base::OkStatus();
}

base::Status ExportTraceToDatabase(const std::string& output_name) {
  PERFETTO_CHECK(output_name.find('\'') == std::string::npos);
  {
    base::ScopedFile fd(base::OpenFile(output_name, O_CREAT | O_RDWR, 0600));
    if (!fd)
      return base::ErrStatus("Failed to create file: %s", output_name.c_str());
    int res = ftruncate(fd.get(), 0);
    PERFETTO_CHECK(res == 0);
  }

  std::string attach_sql =
      "ATTACH DATABASE '" + output_name + "' AS perfetto_export";
  auto attach_it = g_tp->ExecuteQuery(attach_sql);
  bool attach_has_more = attach_it.Next();
  PERFETTO_DCHECK(!attach_has_more);

  RETURN_IF_ERROR(attach_it.Status());

  // Export real and virtual tables.
  auto tables_it = g_tp->ExecuteQuery("SELECT name FROM perfetto_tables");
  while (tables_it.Next()) {
    std::string table_name = tables_it.Get(0).string_value;
    PERFETTO_CHECK(!base::Contains(table_name, '\''));
    std::string export_sql = "CREATE TABLE perfetto_export." + table_name +
                             " AS SELECT * FROM " + table_name;

    auto export_it = g_tp->ExecuteQuery(export_sql);
    bool export_has_more = export_it.Next();
    PERFETTO_DCHECK(!export_has_more);
    RETURN_IF_ERROR(export_it.Status());
  }
  RETURN_IF_ERROR(tables_it.Status());

  // Export views.
  auto views_it =
      g_tp->ExecuteQuery("SELECT sql FROM sqlite_master WHERE type='view'");
  while (views_it.Next()) {
    std::string sql = views_it.Get(0).string_value;
    // View statements are of the form "CREATE VIEW name AS stmt". We need to
    // rewrite name to point to the exported db.
    const std::string kPrefix = "CREATE VIEW ";
    PERFETTO_CHECK(sql.find(kPrefix) == 0);
    sql = sql.substr(0, kPrefix.size()) + "perfetto_export." +
          sql.substr(kPrefix.size());

    auto export_it = g_tp->ExecuteQuery(sql);
    bool export_has_more = export_it.Next();
    PERFETTO_DCHECK(!export_has_more);
    RETURN_IF_ERROR(export_it.Status());
  }
  RETURN_IF_ERROR(views_it.Status());

  auto detach_it = g_tp->ExecuteQuery("DETACH DATABASE perfetto_export");
  bool detach_has_more = attach_it.Next();
  PERFETTO_DCHECK(!detach_has_more);
  return detach_it.Status();
}

class ErrorPrinter : public google::protobuf::io::ErrorCollector {
  void AddError(int line, int col, const std::string& msg) override {
    PERFETTO_ELOG("%d:%d: %s", line, col, msg.c_str());
  }

  void AddWarning(int line, int col, const std::string& msg) override {
    PERFETTO_ILOG("%d:%d: %s", line, col, msg.c_str());
  }
};

// This function returns an identifier for a metric suitable for use
// as an SQL table name (i.e. containing no forward or backward slashes).
std::string BaseName(std::string metric_path) {
  std::replace(metric_path.begin(), metric_path.end(), '\\', '/');
  auto slash_idx = metric_path.rfind('/');
  return slash_idx == std::string::npos ? metric_path
                                        : metric_path.substr(slash_idx + 1);
}

base::Status RegisterMetric(const std::string& register_metric) {
  std::string sql;
  base::ReadFile(register_metric, &sql);

  std::string path = "shell/" + BaseName(register_metric);
  return g_tp->RegisterMetric(path, sql);
}

base::Status ParseToFileDescriptorProto(
    const std::string& filename,
    google::protobuf::FileDescriptorProto* file_desc) {
  base::ScopedFile file(base::OpenFile(filename, O_RDONLY));
  if (file.get() == -1) {
    return base::ErrStatus("Failed to open proto file %s", filename.c_str());
  }

  google::protobuf::io::FileInputStream stream(file.get());
  ErrorPrinter printer;
  google::protobuf::io::Tokenizer tokenizer(&stream, &printer);

  google::protobuf::compiler::Parser parser;
  parser.Parse(&tokenizer, file_desc);
  return base::OkStatus();
}

base::Status ExtendMetricsProto(const std::string& extend_metrics_proto,
                                google::protobuf::DescriptorPool* pool) {
  google::protobuf::FileDescriptorSet desc_set;
  auto* file_desc = desc_set.add_file();
  RETURN_IF_ERROR(ParseToFileDescriptorProto(extend_metrics_proto, file_desc));

  file_desc->set_name(BaseName(extend_metrics_proto));
  pool->BuildFile(*file_desc);

  std::vector<uint8_t> metric_proto;
  metric_proto.resize(desc_set.ByteSizeLong());
  desc_set.SerializeToArray(metric_proto.data(),
                            static_cast<int>(metric_proto.size()));

  return g_tp->ExtendMetricsProto(metric_proto.data(), metric_proto.size());
}

enum MetricV1OutputFormat {
  kBinaryProto,
  kTextProto,
  kJson,
  kNone,
};

struct MetricNameAndPath {
  std::string name;
  std::optional<std::string> no_ext_path;
};

base::Status RunMetrics(const std::vector<MetricNameAndPath>& metrics,
                        MetricV1OutputFormat format) {
  std::vector<std::string> metric_names(metrics.size());
  for (size_t i = 0; i < metrics.size(); ++i) {
    metric_names[i] = metrics[i].name;
  }

  switch (format) {
    case MetricV1OutputFormat::kBinaryProto: {
      std::vector<uint8_t> metric_result;
      RETURN_IF_ERROR(g_tp->ComputeMetric(metric_names, &metric_result));
      fwrite(metric_result.data(), sizeof(uint8_t), metric_result.size(),
             stdout);
      break;
    }
    case MetricV1OutputFormat::kJson: {
      std::string out;
      RETURN_IF_ERROR(g_tp->ComputeMetricText(
          metric_names, TraceProcessor::MetricResultFormat::kJson, &out));
      out += '\n';
      fwrite(out.c_str(), sizeof(char), out.size(), stdout);
      break;
    }
    case MetricV1OutputFormat::kTextProto: {
      std::string out;
      RETURN_IF_ERROR(g_tp->ComputeMetricText(
          metric_names, TraceProcessor::MetricResultFormat::kProtoText, &out));
      out += '\n';
      fwrite(out.c_str(), sizeof(char), out.size(), stdout);
      break;
    }
    case MetricV1OutputFormat::kNone:
      break;
  }

  return base::OkStatus();
}

void PrintQueryResultInteractively(Iterator* it,
                                   base::TimeNanos t_start,
                                   uint32_t column_width) {
  base::TimeNanos t_end = base::GetWallTimeNs();
  for (uint32_t rows = 0; it->Next(); rows++) {
    if (rows % 32 == 0) {
      if (rows == 0) {
        t_end = base::GetWallTimeNs();
      } else {
        fprintf(stderr, "...\nType 'q' to stop, Enter for more records: ");
        fflush(stderr);
        char input[32];
        if (!fgets(input, sizeof(input) - 1, stdin))
          exit(0);
        if (input[0] == 'q')
          break;
      }
      for (uint32_t i = 0; i < it->ColumnCount(); i++)
        printf("%-*.*s ", column_width, column_width,
               it->GetColumnName(i).c_str());
      printf("\n");

      std::string divider(column_width, '-');
      for (uint32_t i = 0; i < it->ColumnCount(); i++) {
        printf("%-*s ", column_width, divider.c_str());
      }
      printf("\n");
    }

    for (uint32_t c = 0; c < it->ColumnCount(); c++) {
      auto value = it->Get(c);
      switch (value.type) {
        case SqlValue::Type::kNull:
          printf("%-*s", column_width, "[NULL]");
          break;
        case SqlValue::Type::kDouble:
          printf("%*f", column_width, value.double_value);
          break;
        case SqlValue::Type::kLong:
          printf("%*" PRIi64, column_width, value.long_value);
          break;
        case SqlValue::Type::kString:
          printf("%-*.*s", column_width, column_width, value.string_value);
          break;
        case SqlValue::Type::kBytes:
          printf("%-*s", column_width, "<raw bytes>");
          break;
      }
      printf(" ");
    }
    printf("\n");
  }

  base::Status status = it->Status();
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.c_message());
  }
  printf("\nQuery executed in %.3f ms\n\n",
         static_cast<double>((t_end - t_start).count()) / 1E6);
}

struct QueryResult {
  std::vector<std::string> column_names;
  std::vector<std::vector<std::string>> rows;
};

base::StatusOr<QueryResult> ExtractQueryResult(Iterator* it, bool has_more) {
  QueryResult result;

  for (uint32_t c = 0; c < it->ColumnCount(); c++) {
    fprintf(stderr, "column %u = %s\n", c, it->GetColumnName(c).c_str());
    result.column_names.push_back(it->GetColumnName(c));
  }

  for (; has_more; has_more = it->Next()) {
    std::vector<std::string> row;
    for (uint32_t c = 0; c < it->ColumnCount(); c++) {
      SqlValue value = it->Get(c);
      std::string str_value;
      switch (value.type) {
        case SqlValue::Type::kNull:
          str_value = "\"[NULL]\"";
          break;
        case SqlValue::Type::kDouble:
          str_value =
              base::StackString<256>("%f", value.double_value).ToStdString();
          break;
        case SqlValue::Type::kLong:
          str_value = base::StackString<256>("%" PRIi64, value.long_value)
                          .ToStdString();
          break;
        case SqlValue::Type::kString:
          str_value = '"' + std::string(value.string_value) + '"';
          break;
        case SqlValue::Type::kBytes:
          str_value = "\"<raw bytes>\"";
          break;
      }

      row.push_back(std::move(str_value));
    }
    result.rows.push_back(std::move(row));
  }
  RETURN_IF_ERROR(it->Status());
  return result;
}

void PrintQueryResultAsCsv(const QueryResult& result, FILE* output) {
  for (uint32_t c = 0; c < result.column_names.size(); c++) {
    if (c > 0)
      fprintf(output, ",");
    fprintf(output, "\"%s\"", result.column_names[c].c_str());
  }
  fprintf(output, "\n");

  for (const auto& row : result.rows) {
    for (uint32_t c = 0; c < result.column_names.size(); c++) {
      if (c > 0)
        fprintf(output, ",");
      fprintf(output, "%s", row[c].c_str());
    }
    fprintf(output, "\n");
  }
}

base::Status RunQueriesWithoutOutput(const std::string& sql_query) {
  auto it = g_tp->ExecuteQuery(sql_query);
  if (it.StatementWithOutputCount() > 0)
    return base::ErrStatus("Unexpected result from a query.");

  RETURN_IF_ERROR(it.Status());
  return it.Next() ? base::ErrStatus("Unexpected result from a query.")
                   : it.Status();
}

base::Status RunQueriesAndPrintResult(const std::string& sql_query,
                                      FILE* output) {
  PERFETTO_DLOG("Executing query: %s", sql_query.c_str());
  auto query_start = std::chrono::steady_clock::now();

  auto it = g_tp->ExecuteQuery(sql_query);
  RETURN_IF_ERROR(it.Status());

  bool has_more = it.Next();
  RETURN_IF_ERROR(it.Status());

  uint32_t prev_count = it.StatementCount() - 1;
  uint32_t prev_with_output = has_more ? it.StatementWithOutputCount() - 1
                                       : it.StatementWithOutputCount();
  uint32_t prev_without_output_count = prev_count - prev_with_output;
  if (prev_with_output > 0) {
    return base::ErrStatus(
        "Result rows were returned for multiples queries. Ensure that only the "
        "final statement is a SELECT statement or use `suppress_query_output` "
        "to prevent function invocations causing this "
        "error (see "
        "https://perfetto.dev/docs/contributing/"
        "testing#trace-processor-diff-tests).");
  }
  for (uint32_t i = 0; i < prev_without_output_count; ++i) {
    fprintf(output, "\n");
  }
  if (it.ColumnCount() == 0) {
    PERFETTO_DCHECK(!has_more);
    return base::OkStatus();
  }

  auto query_result = ExtractQueryResult(&it, has_more);
  RETURN_IF_ERROR(query_result.status());

  // We want to include the query iteration time (as it's a part of executing
  // SQL and can be non-trivial), and we want to exclude the time spent printing
  // the result (which can be significant for large results), so we materialise
  // the results first, then take the measurement, then print them.
  auto query_end = std::chrono::steady_clock::now();

  PrintQueryResultAsCsv(query_result.value(), output);

  auto dur = query_end - query_start;
  PERFETTO_ILOG(
      "Query execution time: %" PRIi64 " ms",
      static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()));
  return base::OkStatus();
}

base::Status PrintPerfFile(const std::string& perf_file_path,
                           base::TimeNanos t_load,
                           base::TimeNanos t_run) {
  char buf[128];
  size_t count = base::SprintfTrunc(buf, sizeof(buf), "%" PRId64 ",%" PRId64,
                                    static_cast<int64_t>(t_load.count()),
                                    static_cast<int64_t>(t_run.count()));
  if (count == 0) {
    return base::ErrStatus("Failed to write perf data");
  }

  auto fd(base::OpenFile(perf_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666));
  if (!fd) {
    return base::ErrStatus("Failed to open perf file");
  }
  base::WriteAll(fd.get(), buf, count);
  return base::OkStatus();
}

class MetricExtension {
 public:
  void SetDiskPath(std::string path) {
    AddTrailingSlashIfNeeded(path);
    disk_path_ = std::move(path);
  }
  void SetVirtualPath(std::string path) {
    AddTrailingSlashIfNeeded(path);
    virtual_path_ = std::move(path);
  }

  // Disk location. Ends with a trailing slash.
  const std::string& disk_path() const { return disk_path_; }
  // Virtual location. Ends with a trailing slash.
  const std::string& virtual_path() const { return virtual_path_; }

 private:
  std::string disk_path_;
  std::string virtual_path_;

  static void AddTrailingSlashIfNeeded(std::string& path) {
    if (path.length() > 0 && path[path.length() - 1] != '/') {
      path.push_back('/');
    }
  }
};

metatrace::MetatraceCategories ParseMetatraceCategories(std::string s) {
  using Cat = metatrace::MetatraceCategories;
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  base::StringSplitter splitter(s, ',');

  Cat result = Cat::NONE;
  for (; splitter.Next();) {
    std::string cur = splitter.cur_token();
    if (cur == "all" || cur == "*") {
      result = Cat::ALL;
    } else if (cur == "query_toplevel") {
      result = static_cast<Cat>(result | Cat::QUERY_TIMELINE);
    } else if (cur == "query_detailed") {
      result = static_cast<Cat>(result | Cat::QUERY_DETAILED);
    } else if (cur == "function_call") {
      result = static_cast<Cat>(result | Cat::FUNCTION_CALL);
    } else if (cur == "db") {
      result = static_cast<Cat>(result | Cat::DB);
    } else if (cur == "api") {
      result = static_cast<Cat>(result | Cat::API_TIMELINE);
    } else {
      PERFETTO_ELOG("Unknown metatrace category %s", cur.data());
      exit(1);
    }
  }
  return result;
}

struct CommandLineOptions {
  std::string trace_file_path;

  bool enable_httpd = false;
  std::string port_number;
  std::string listen_ip;
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
 --add-sql-package PACKAGE_PATH       Files from the directory will be treated
                                      as a new SQL package and can be used for
                                      INCLUDE PERFETTO MODULE statements. The
                                      name of the directory is the package name.
 --override-sql-package PACKAGE_PATH  Will override trace processor package with
                                      passed contents. The outer directory will
                                      specify the package name.

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
 --add-sql-module PACKAGE_PATH        Alias for --add-sql-package, kept for
                                      backwards compatibility. Prefer
                                      --add-sql-package.
 --override-sql-module PACKAGE_PATH   Alias for --override-sql-package, kept for
                                      backwards compatibility. Prefer
                                      --override-sql-package.

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
    OPT_STDIOD,

    OPT_FORCE_FULL_SORT,
    OPT_NO_FTRACE_RAW,

    OPT_ADD_SQL_PACKAGE,
    OPT_OVERRIDE_SQL_PACKAGE,

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
      {"stdiod", no_argument, nullptr, OPT_STDIOD},
      {"interactive", no_argument, nullptr, 'i'},

      {"full-sort", no_argument, nullptr, OPT_FORCE_FULL_SORT},
      {"no-ftrace-raw", no_argument, nullptr, OPT_NO_FTRACE_RAW},

      {"query-file", required_argument, nullptr, 'q'},
      {"query-string", required_argument, nullptr, 'Q'},
      {"add-sql-module", required_argument, nullptr, OPT_ADD_SQL_PACKAGE},
      {"add-sql-package", required_argument, nullptr, OPT_ADD_SQL_PACKAGE},
      {"override-sql-module", required_argument, nullptr,
       OPT_OVERRIDE_SQL_PACKAGE},
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

    PrintUsage(argv);
    exit(option == 'h' ? 0 : 1);
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

void ExtendPoolWithBinaryDescriptor(
    google::protobuf::DescriptorPool& pool,
    const void* data,
    int size,
    const std::vector<std::string>& skip_prefixes) {
  google::protobuf::FileDescriptorSet desc_set;
  PERFETTO_CHECK(desc_set.ParseFromArray(data, size));
  for (const auto& file_desc : desc_set.file()) {
    if (base::StartsWithAny(file_desc.name(), skip_prefixes))
      continue;
    pool.BuildFile(file_desc);
  }
}

base::Status LoadTrace(const std::string& trace_file_path, double* size_mb) {
  base::Status read_status = ReadTraceUnfinalized(
      g_tp, trace_file_path.c_str(), [&size_mb](size_t parsed_size) {
        *size_mb = static_cast<double>(parsed_size) / 1E6;
        fprintf(stderr, "\rLoading trace: %.2f MB\r", *size_mb);
      });
  g_tp->Flush();
  if (!read_status.ok()) {
    return base::ErrStatus("Could not read trace file (path: %s): %s",
                           trace_file_path.c_str(), read_status.c_message());
  }

  std::unique_ptr<profiling::Symbolizer> symbolizer =
      profiling::LocalSymbolizerOrDie(profiling::GetPerfettoBinaryPath(),
                                      getenv("PERFETTO_SYMBOLIZER_MODE"));

  if (symbolizer) {
    profiling::SymbolizeDatabase(
        g_tp, symbolizer.get(), [](const std::string& trace_proto) {
          std::unique_ptr<uint8_t[]> buf(new uint8_t[trace_proto.size()]);
          memcpy(buf.get(), trace_proto.data(), trace_proto.size());
          auto status = g_tp->Parse(std::move(buf), trace_proto.size());
          if (!status.ok()) {
            PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s",
                                    status.message().c_str());
            return;
          }
        });
    g_tp->Flush();
  }

  auto maybe_map = profiling::GetPerfettoProguardMapPath();
  if (!maybe_map.empty()) {
    profiling::ReadProguardMapsToDeobfuscationPackets(
        maybe_map, [](const std::string& trace_proto) {
          std::unique_ptr<uint8_t[]> buf(new uint8_t[trace_proto.size()]);
          memcpy(buf.get(), trace_proto.data(), trace_proto.size());
          auto status = g_tp->Parse(std::move(buf), trace_proto.size());
          if (!status.ok()) {
            PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s",
                                    status.message().c_str());
            return;
          }
        });
  }
  return g_tp->NotifyEndOfFile();
}

base::Status RunQueries(const std::string& queries, bool expect_output) {
  if (expect_output) {
    return RunQueriesAndPrintResult(queries, stdout);
  }
  return RunQueriesWithoutOutput(queries);
}

base::Status RunQueriesFromFile(const std::string& query_file_path,
                                bool expect_output) {
  std::string queries;
  if (!base::ReadFile(query_file_path, &queries)) {
    return base::ErrStatus("Unable to read file %s", query_file_path.c_str());
  }
  return RunQueries(queries, expect_output);
}

base::Status ParseSingleMetricExtensionPath(bool dev,
                                            const std::string& raw_extension,
                                            MetricExtension& parsed_extension) {
  // We cannot easily use ':' as a path separator because windows paths can have
  // ':' in them (e.g. C:\foo\bar).
  std::vector<std::string> parts = base::SplitString(raw_extension, "@");
  if (parts.size() != 2 || parts[0].length() == 0 || parts[1].length() == 0) {
    return base::ErrStatus(
        "--metric-extension-dir must be of format disk_path@virtual_path");
  }

  parsed_extension.SetDiskPath(std::move(parts[0]));
  parsed_extension.SetVirtualPath(std::move(parts[1]));

  if (parsed_extension.virtual_path() == "/") {
    if (!dev) {
      return base::ErrStatus(
          "Local development features must be enabled (using the "
          "--dev flag) to override built-in metrics");
    }
    parsed_extension.SetVirtualPath("");
  }

  if (parsed_extension.virtual_path() == "shell/") {
    return base::Status(
        "Cannot have 'shell/' as metric extension virtual path.");
  }
  return base::OkStatus();
}

base::Status CheckForDuplicateMetricExtension(
    const std::vector<MetricExtension>& metric_extensions) {
  std::unordered_set<std::string> disk_paths;
  std::unordered_set<std::string> virtual_paths;
  for (const auto& extension : metric_extensions) {
    auto ret = disk_paths.insert(extension.disk_path());
    if (!ret.second) {
      return base::ErrStatus(
          "Another metric extension is already using disk path %s",
          extension.disk_path().c_str());
    }
    ret = virtual_paths.insert(extension.virtual_path());
    if (!ret.second) {
      return base::ErrStatus(
          "Another metric extension is already using virtual path %s",
          extension.virtual_path().c_str());
    }
  }
  return base::OkStatus();
}

base::Status ParseMetricExtensionPaths(
    bool dev,
    const std::vector<std::string>& raw_metric_extensions,
    std::vector<MetricExtension>& metric_extensions) {
  for (const auto& raw_extension : raw_metric_extensions) {
    metric_extensions.push_back({});
    RETURN_IF_ERROR(ParseSingleMetricExtensionPath(dev, raw_extension,
                                                   metric_extensions.back()));
  }
  return CheckForDuplicateMetricExtension(metric_extensions);
}

base::Status IncludeSqlPackage(std::string root, bool allow_override) {
  // Remove trailing slash
  if (root.back() == '/')
    root = root.substr(0, root.length() - 1);

  if (!base::FileExists(root))
    return base::ErrStatus("Directory %s does not exist.", root.c_str());

  // Get package name
  size_t last_slash = root.rfind('/');
  if (last_slash == std::string::npos) {
    return base::ErrStatus("Package path must point to a directory: %s",
                           root.c_str());
  }

  std::string package_name = root.substr(last_slash + 1);

  std::vector<std::string> paths;
  RETURN_IF_ERROR(base::ListFilesRecursive(root, paths));
  sql_modules::NameToPackage modules;
  for (const auto& path : paths) {
    if (base::GetFileExtension(path) != ".sql") {
      continue;
    }

    std::string path_no_extension = path.substr(0, path.rfind('.'));
    if (path_no_extension.find('.') != std::string_view::npos) {
      PERFETTO_ELOG("Skipping module %s as it contains a dot in its path.",
                    path_no_extension.c_str());
      continue;
    }

    std::string filename = root + "/" + path;
    std::string file_contents;
    if (!base::ReadFile(filename, &file_contents)) {
      return base::ErrStatus("Cannot read file %s", filename.c_str());
    }

    std::string import_key =
        package_name + "." + sql_modules::GetIncludeKey(path);
    modules.Insert(package_name, {})
        .first->push_back({import_key, file_contents});
  }
  for (auto module_it = modules.GetIterator(); module_it; ++module_it) {
    RETURN_IF_ERROR(
        g_tp->RegisterSqlPackage({/*name=*/module_it.key(),
                                  /*files=*/module_it.value(),
                                  /*allow_override=*/allow_override}));
  }
  return base::OkStatus();
}

base::Status LoadOverridenStdlib(std::string root) {
  // Remove trailing slash
  if (root.back() == '/') {
    root = root.substr(0, root.length() - 1);
  }

  if (!base::FileExists(root)) {
    return base::ErrStatus("Directory '%s' does not exist.", root.c_str());
  }

  std::vector<std::string> paths;
  RETURN_IF_ERROR(base::ListFilesRecursive(root, paths));
  sql_modules::NameToPackage packages;
  for (const auto& path : paths) {
    if (base::GetFileExtension(path) != ".sql") {
      continue;
    }
    std::string filename = root + "/" + path;
    std::string module_file;
    if (!base::ReadFile(filename, &module_file)) {
      return base::ErrStatus("Cannot read file '%s'", filename.c_str());
    }
    std::string module_name = sql_modules::GetIncludeKey(path);
    std::string package_name = sql_modules::GetPackageName(module_name);
    packages.Insert(package_name, {})
        .first->push_back({module_name, module_file});
  }
  for (auto package = packages.GetIterator(); package; ++package) {
    g_tp->RegisterSqlPackage({/*name=*/package.key(),
                              /*files=*/package.value(),
                              /*allow_override=*/true});
  }

  return base::OkStatus();
}

base::Status LoadMetricExtensionProtos(const std::string& proto_root,
                                       const std::string& mount_path,
                                       google::protobuf::DescriptorPool& pool) {
  if (!base::FileExists(proto_root)) {
    return base::ErrStatus(
        "Directory %s does not exist. Metric extension directory must contain "
        "a 'sql/' and 'protos/' subdirectory.",
        proto_root.c_str());
  }
  std::vector<std::string> proto_files;
  RETURN_IF_ERROR(base::ListFilesRecursive(proto_root, proto_files));

  google::protobuf::FileDescriptorSet parsed_protos;
  for (const auto& file_path : proto_files) {
    if (base::GetFileExtension(file_path) != ".proto")
      continue;
    auto* file_desc = parsed_protos.add_file();
    ParseToFileDescriptorProto(proto_root + file_path, file_desc);
    file_desc->set_name(mount_path + file_path);
  }

  std::vector<uint8_t> serialized_filedescset;
  serialized_filedescset.resize(parsed_protos.ByteSizeLong());
  parsed_protos.SerializeToArray(
      serialized_filedescset.data(),
      static_cast<int>(serialized_filedescset.size()));

  // Extend the pool for any subsequent reflection-based operations
  // (e.g. output json)
  ExtendPoolWithBinaryDescriptor(
      pool, serialized_filedescset.data(),
      static_cast<int>(serialized_filedescset.size()), {});
  return g_tp->ExtendMetricsProto(serialized_filedescset.data(),
                                  serialized_filedescset.size());
}

base::Status LoadMetricExtensionSql(const std::string& sql_root,
                                    const std::string& mount_path) {
  if (!base::FileExists(sql_root)) {
    return base::ErrStatus(
        "Directory %s does not exist. Metric extension directory must contain "
        "a 'sql/' and 'protos/' subdirectory.",
        sql_root.c_str());
  }

  std::vector<std::string> sql_files;
  RETURN_IF_ERROR(base::ListFilesRecursive(sql_root, sql_files));
  for (const auto& file_path : sql_files) {
    if (base::GetFileExtension(file_path) != ".sql")
      continue;
    std::string file_contents;
    if (!base::ReadFile(sql_root + file_path, &file_contents)) {
      return base::ErrStatus("Cannot read file %s", file_path.c_str());
    }
    RETURN_IF_ERROR(
        g_tp->RegisterMetric(mount_path + file_path, file_contents));
  }
  return base::OkStatus();
}

base::Status LoadMetricExtension(const MetricExtension& extension,
                                 google::protobuf::DescriptorPool& pool) {
  const std::string& disk_path = extension.disk_path();
  const std::string& virtual_path = extension.virtual_path();

  if (!base::FileExists(disk_path)) {
    return base::ErrStatus("Metric extension directory %s does not exist",
                           disk_path.c_str());
  }

  // Note: Proto files must be loaded first, because we determine whether an SQL
  // file is a metric or not by checking if the name matches a field of the root
  // TraceMetrics proto.
  RETURN_IF_ERROR(LoadMetricExtensionProtos(
      disk_path + "protos/", kMetricProtoRoot + virtual_path, pool));
  RETURN_IF_ERROR(LoadMetricExtensionSql(disk_path + "sql/", virtual_path));

  return base::OkStatus();
}

base::Status PopulateDescriptorPool(
    google::protobuf::DescriptorPool& pool,
    const std::vector<MetricExtension>& metric_extensions) {
  // TODO(b/182165266): There is code duplication here with trace_processor_impl
  // SetupMetrics. This will be removed when we switch the output formatter to
  // use internal DescriptorPool.
  std::vector<std::string> skip_prefixes;
  skip_prefixes.reserve(metric_extensions.size());
  for (const auto& ext : metric_extensions) {
    skip_prefixes.push_back(kMetricProtoRoot + ext.virtual_path());
  }
  ExtendPoolWithBinaryDescriptor(pool, kMetricsDescriptor.data(),
                                 kMetricsDescriptor.size(), skip_prefixes);
  ExtendPoolWithBinaryDescriptor(pool, kAllChromeMetricsDescriptor.data(),
                                 kAllChromeMetricsDescriptor.size(),
                                 skip_prefixes);
  ExtendPoolWithBinaryDescriptor(pool, kAllWebviewMetricsDescriptor.data(),
                                 kAllWebviewMetricsDescriptor.size(),
                                 skip_prefixes);
  return base::OkStatus();
}

base::Status LoadMetrics(const std::string& raw_metric_names,
                         google::protobuf::DescriptorPool& pool,
                         std::vector<MetricNameAndPath>& name_and_path) {
  std::vector<std::string> split;
  for (base::StringSplitter ss(raw_metric_names, ','); ss.Next();) {
    split.emplace_back(ss.cur_token());
  }

  // For all metrics which are files, register them and extend the metrics
  // proto.
  for (const std::string& metric_or_path : split) {
    // If there is no extension, we assume it is a builtin metric.
    auto ext_idx = metric_or_path.rfind('.');
    if (ext_idx == std::string::npos) {
      name_and_path.emplace_back(
          MetricNameAndPath{metric_or_path, std::nullopt});
      continue;
    }

    std::string no_ext_path = metric_or_path.substr(0, ext_idx);

    // The proto must be extended before registering the metric.
    base::Status status = ExtendMetricsProto(no_ext_path + ".proto", &pool);
    if (!status.ok()) {
      return base::ErrStatus("Unable to extend metrics proto %s: %s",
                             metric_or_path.c_str(), status.c_message());
    }

    status = RegisterMetric(no_ext_path + ".sql");
    if (!status.ok()) {
      return base::ErrStatus("Unable to register metric %s: %s",
                             metric_or_path.c_str(), status.c_message());
    }
    name_and_path.emplace_back(
        MetricNameAndPath{BaseName(no_ext_path), no_ext_path});
  }
  return base::OkStatus();
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

base::Status LoadMetricsAndExtensionsSql(
    const std::vector<MetricNameAndPath>& metrics,
    const std::vector<MetricExtension>& extensions) {
  for (const MetricExtension& extension : extensions) {
    const std::string& disk_path = extension.disk_path();
    const std::string& virtual_path = extension.virtual_path();

    RETURN_IF_ERROR(LoadMetricExtensionSql(disk_path + "sql/", virtual_path));
  }

  for (const MetricNameAndPath& metric : metrics) {
    // Ignore builtin metrics.
    if (!metric.no_ext_path.has_value())
      continue;
    RETURN_IF_ERROR(RegisterMetric(metric.no_ext_path.value() + ".sql"));
  }
  return base::OkStatus();
}

void PrintShellUsage() {
  PERFETTO_ELOG(R"(
Available commands:
.quit, .q         Exit the shell.
.help             This text.
.dump FILE        Export the trace as a sqlite database.
.read FILE        Executes the queries in the FILE.
.reset            Destroys all tables/view created by the user.
.load-metrics-sql Reloads SQL from extension and custom metric paths
                  specified in command line args.
.run-metrics      Runs metrics specified in command line args
                  and prints the result.
.width WIDTH      Changes the column width of interactive query
                  output.
)");
}

struct InteractiveOptions {
  uint32_t column_width;
  MetricV1OutputFormat metric_v1_format;
  std::vector<MetricExtension> extensions;
  std::vector<MetricNameAndPath> metrics;
  const google::protobuf::DescriptorPool* pool;
};

base::Status StartInteractiveShell(const InteractiveOptions& options) {
  SetupLineEditor();

  uint32_t column_width = options.column_width;
  for (;;) {
    ScopedLine line = GetLine("> ");
    if (!line)
      break;
    if (strcmp(line.get(), "") == 0) {
      printf("If you want to quit either type .q or press CTRL-D (EOF)\n");
      continue;
    }
    if (line.get()[0] == '.') {
      char command[32] = {};
      char arg[1024] = {};
      sscanf(line.get() + 1, "%31s %1023s", command, arg);
      if (strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
        break;
      }
      if (strcmp(command, "help") == 0) {
        PrintShellUsage();
      } else if (strcmp(command, "dump") == 0 && strlen(arg)) {
        if (!ExportTraceToDatabase(arg).ok())
          PERFETTO_ELOG("Database export failed");
      } else if (strcmp(command, "reset") == 0) {
        g_tp->RestoreInitialTables();
      } else if (strcmp(command, "read") == 0 && strlen(arg)) {
        base::Status status = RunQueriesFromFile(arg, true);
        if (!status.ok()) {
          PERFETTO_ELOG("%s", status.c_message());
        }
      } else if (strcmp(command, "width") == 0 && strlen(arg)) {
        std::optional<uint32_t> width = base::CStringToUInt32(arg);
        if (!width) {
          PERFETTO_ELOG("Invalid column width specified");
          continue;
        }
        column_width = *width;
      } else if (strcmp(command, "load-metrics-sql") == 0) {
        base::Status status =
            LoadMetricsAndExtensionsSql(options.metrics, options.extensions);
        if (!status.ok()) {
          PERFETTO_ELOG("%s", status.c_message());
        }
      } else if (strcmp(command, "run-metrics") == 0) {
        if (options.metrics.empty()) {
          PERFETTO_ELOG("No metrics specified on command line");
          continue;
        }

        base::Status status =
            RunMetrics(options.metrics, options.metric_v1_format);
        if (!status.ok()) {
          fprintf(stderr, "%s\n", status.c_message());
        }
      } else {
        PrintShellUsage();
      }
      continue;
    }

    base::TimeNanos t_start = base::GetWallTimeNs();
    auto it = g_tp->ExecuteQuery(line.get());
    PrintQueryResultInteractively(&it, t_start, column_width);
  }
  return base::OkStatus();
}

base::Status MaybeWriteMetatrace(const std::string& metatrace_path) {
  if (metatrace_path.empty()) {
    return base::OkStatus();
  }
  std::vector<uint8_t> serialized;
  RETURN_IF_ERROR(g_tp->DisableAndReadMetatrace(&serialized));

  auto file = base::OpenFile(metatrace_path, O_CREAT | O_RDWR | O_TRUNC, 0600);
  if (!file)
    return base::ErrStatus("Unable to open metatrace file");

  ssize_t res = base::WriteAll(*file, serialized.data(), serialized.size());
  if (res < 0)
    return base::ErrStatus("Error while writing metatrace file");
  return base::OkStatus();
}

base::Status MaybeUpdateSqlPackages(const CommandLineOptions& options) {
  if (!options.override_stdlib_path.empty()) {
    if (!options.dev)
      return base::ErrStatus("Overriding stdlib requires --dev flag");

    auto status = LoadOverridenStdlib(options.override_stdlib_path);
    if (!status.ok())
      return base::ErrStatus("Couldn't override stdlib: %s",
                             status.c_message());
  }

  if (!options.override_sql_package_paths.empty()) {
    for (const auto& override_sql_package_path :
         options.override_sql_package_paths) {
      auto status = IncludeSqlPackage(override_sql_package_path, true);
      if (!status.ok())
        return base::ErrStatus("Couldn't override stdlib package: %s",
                               status.c_message());
    }
  }

  if (!options.sql_package_paths.empty()) {
    for (const auto& add_sql_package_path : options.sql_package_paths) {
      auto status = IncludeSqlPackage(add_sql_package_path, false);
      if (!status.ok())
        return base::ErrStatus("Couldn't add SQL package: %s",
                               status.c_message());
    }
  }
  return base::OkStatus();
}

base::Status RegisterAllFilesInFolder(const std::string& path,
                                      TraceProcessor& tp) {
  std::vector<std::string> files;
  RETURN_IF_ERROR(base::ListFilesRecursive(path, files));
  for (const std::string& file : files) {
    std::string file_full_path = path + "/" + file;
    base::ScopedMmap mmap = base::ReadMmapWholeFile(file_full_path.c_str());
    if (!mmap.IsValid()) {
      return base::ErrStatus("Failed to mmap file: %s", file_full_path.c_str());
    }
    RETURN_IF_ERROR(tp.RegisterFileContent(
        file_full_path, TraceBlobView(TraceBlob::FromMmap(std::move(mmap)))));
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

base::Status TraceProcessorMain(int argc, char** argv) {
  CommandLineOptions options = ParseCommandLineOptions(argc, argv);

  Config config;
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
  g_tp = tp.get();

  RETURN_IF_ERROR(MaybeUpdateSqlPackages(options));

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
    RETURN_IF_ERROR(LoadMetricExtension(extension, pool));
  }

  base::TimeNanos t_load{};
  if (!options.trace_file_path.empty()) {
    base::TimeNanos t_load_start = base::GetWallTimeNs();
    double size_mb = 0;
    RETURN_IF_ERROR(LoadTrace(options.trace_file_path, &size_mb));
    t_load = base::GetWallTimeNs() - t_load_start;

    double t_load_s = static_cast<double>(t_load.count()) / 1E9;
    PERFETTO_ILOG("Trace loaded: %.2f MB in %.2fs (%.1f MB/s)", size_mb,
                  t_load_s, size_mb / t_load_s);

    RETURN_IF_ERROR(PrintStats());
  }

#if PERFETTO_HAS_SIGNAL_H()
  // Set up interrupt signal to allow the user to abort query.
  signal(SIGINT, [](int) { g_tp->InterruptQuery(); });
#endif

  base::TimeNanos t_query_start = base::GetWallTimeNs();
  if (!options.pre_metrics_v1_path.empty()) {
    RETURN_IF_ERROR(RunQueriesFromFile(options.pre_metrics_v1_path, false));
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
        g_tp->Summarize(computation_config, specs, &output, output_spec));
    if (options.query_file_path.empty()) {
      fwrite(output.data(), sizeof(char), output.size(), stdout);
    }
  }

  // v1 metrics.
  std::vector<MetricNameAndPath> metrics;
  if (!options.metric_v1_names.empty()) {
    PERFETTO_CHECK(!options.summary);
    RETURN_IF_ERROR(LoadMetrics(options.metric_v1_names, pool, metrics));
  }

  MetricV1OutputFormat metric_format = ParseMetricV1OutputFormat(options);
  if (!metrics.empty()) {
    RETURN_IF_ERROR(RunMetrics(metrics, metric_format));
  }

  if (!options.query_file_path.empty()) {
    base::Status status = RunQueriesFromFile(options.query_file_path, true);
    if (!status.ok()) {
      // Write metatrace if needed before exiting.
      RETURN_IF_ERROR(MaybeWriteMetatrace(options.metatrace_path));
      return status;
    }
  }

  if (!options.query_string.empty()) {
    base::Status status = RunQueries(options.query_string, true);
    if (!status.ok()) {
      // Write metatrace if needed before exiting.
      RETURN_IF_ERROR(MaybeWriteMetatrace(options.metatrace_path));
      return status;
    }
  }

  base::TimeNanos t_query = base::GetWallTimeNs() - t_query_start;

  if (!options.export_file_path.empty()) {
    RETURN_IF_ERROR(ExportTraceToDatabase(options.export_file_path));
  }

  if (options.enable_httpd) {
#if PERFETTO_HAS_SIGNAL_H()
    if (options.metatrace_path.empty()) {
      // Restore the default signal handler to allow the user to terminate
      // httpd server via Ctrl-C.
      signal(SIGINT, SIG_DFL);
    } else {
      // Write metatrace to file before exiting.
      static std::string* metatrace_path = &options.metatrace_path;
      signal(SIGINT, [](int) {
        MaybeWriteMetatrace(*metatrace_path);
        exit(1);
      });
    }
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_TP_HTTPD)
    RunHttpRPCServer(std::move(tp), !options.trace_file_path.empty(),
                     options.listen_ip, options.port_number);
    PERFETTO_FATAL("Should never return");
#else
    PERFETTO_FATAL("HTTP not available");
#endif
  }

  if (options.enable_stdiod) {
    return RunStdioRpcServer(std::move(tp), !options.trace_file_path.empty());
  }

  if (options.launch_shell) {
    RETURN_IF_ERROR(StartInteractiveShell(
        InteractiveOptions{options.wide ? 40u : 20u, metric_format,
                           metric_extensions, metrics, &pool}));
  } else if (!options.perf_file_path.empty()) {
    RETURN_IF_ERROR(PrintPerfFile(options.perf_file_path, t_load, t_query));
  }

  RETURN_IF_ERROR(MaybeWriteMetatrace(options.metatrace_path));

  return base::OkStatus();
}

}  // namespace

}  // namespace perfetto::trace_processor

int main(int argc, char** argv) {
  auto status = perfetto::trace_processor::TraceProcessorMain(argc, argv);
  if (!status.ok()) {
    fprintf(stderr, "%s\n", status.c_message());
    return 1;
  }
  return 0;
}
