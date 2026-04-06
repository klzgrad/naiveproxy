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

#include "src/trace_processor/shell/interactive.h"

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/shell/metrics.h"
#include "src/trace_processor/shell/query.h"
#include "src/trace_processor/shell/shell_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_LINENOISE)
#include <linenoise.h>
#include <pwd.h>
#include <sys/types.h>
#endif

namespace perfetto::trace_processor {

namespace {

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
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
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

}  // namespace

base::Status StartInteractiveShell(TraceProcessor* trace_processor,
                                   const InteractiveOptions& options) {
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
        if (!ExportTraceToDatabase(trace_processor, arg).ok())
          PERFETTO_ELOG("Database export failed");
      } else if (strcmp(command, "reset") == 0) {
        trace_processor->RestoreInitialTables();
      } else if (strcmp(command, "read") == 0 && strlen(arg)) {
        base::Status status = RunQueriesFromFile(trace_processor, arg, true);
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
        base::Status status = LoadMetricsAndExtensionsSql(
            trace_processor, options.metrics, options.extensions);
        if (!status.ok()) {
          PERFETTO_ELOG("%s", status.c_message());
        }
      } else if (strcmp(command, "run-metrics") == 0) {
        if (options.metrics.empty()) {
          PERFETTO_ELOG("No metrics specified on command line");
          continue;
        }

        base::Status status = RunMetrics(trace_processor, options.metrics,
                                         options.metric_v1_format);
        if (!status.ok()) {
          fprintf(stderr, "%s\n", status.c_message());
        }
      } else {
        PrintShellUsage();
      }
      continue;
    }

    base::TimeNanos t_start = base::GetWallTimeNs();
    auto it = trace_processor->ExecuteQuery(line.get());
    PrintQueryResultInteractively(&it, t_start, column_width);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
