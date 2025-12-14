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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/version.h"
#include "src/protozero/text_to_proto/text_to_proto.h"
#include "src/traceconv/deobfuscate_profile.h"
#include "src/traceconv/symbolize_profile.h"
#include "src/traceconv/trace.descriptor.h"
#include "src/traceconv/trace_to_bundle.h"
#include "src/traceconv/trace_to_firefox.h"
#include "src/traceconv/trace_to_hprof.h"
#include "src/traceconv/trace_to_json.h"
#include "src/traceconv/trace_to_profile.h"
#include "src/traceconv/trace_to_systrace.h"
#include "src/traceconv/trace_to_text.h"
#include "src/traceconv/trace_unpack.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace perfetto::trace_to_text {
namespace {

int Usage(const char* argv0) {
  fprintf(stderr, R"(
Trace format conversion tool.
Usage: %s MODE [OPTIONS] [input_file] [output_file]

CONVERSION MODES AND THEIR SUPPORTED OPTIONS:

 systrace                             Converts to systrace HTML format
   --truncate [start|end]             Truncates trace to keep start or end
   --full-sort                        Forces full trace sorting

 json                                 Converts to Chrome JSON format
   --truncate [start|end]             Truncates trace to keep start or end
   --full-sort                        Forces full trace sorting

 ctrace                               Converts to compressed systrace format
   --truncate [start|end]             Truncates trace to keep start or end
   --full-sort                        Forces full trace sorting

 text                                 Converts to human-readable text format
   (no additional options)

 profile                              Converts heap profiles to pprof format
                                      (profile.proto - default: heap profiles)
   --perf                             Extract perf/CPU profiles instead
   --no-annotations                   Don't add derived annotations to frames
   --timestamps T1,T2,...             Generate profiles for specific timestamps
   --pid PID                          Generate profiles for specific process

 java_heap_profile                    Converts Java heap profiles to pprof format
                                      (profile.proto)
   --no-annotations                   Don't add derived annotations to frames
   --timestamps T1,T2,...             Generate profiles for specific timestamps
   --pid PID                          Generate profiles for specific process

 hprof                                Converts heap profile to hprof format
   --timestamps T1,T2,...             Generate profiles for specific timestamps
   --pid PID                          Generate profiles for specific process

 symbolize                            Symbolizes addresses in profiles
   (no additional options)

 deobfuscate                          Deobfuscates obfuscated profiles
   (no additional options)

 firefox                              Converts to Firefox profiler format
   (no additional options)

 decompress_packets                   Decompresses compressed trace packets
   (no additional options)

 bundle                               Creates bundle with trace + debug data
                                      (outputs TAR with symbols/deobfuscation mappings)
                                      Requires input and output file paths (no stdin/stdout)
   --symbol-paths PATH1,PATH2,...     Additional paths to search for symbols
                                      (beyond automatic discovery)
   --no-auto-symbol-paths             Disable automatic symbol path discovery

 binary                               Converts text proto to binary format
   (no additional options)

NOTES:
 - If no input file is specified, reads from stdin
 - If no output file is specified, writes to stdout
 - Input/output files can be '-' to explicitly use stdin/stdout
)",
          argv0);
  return 1;
}

uint64_t StringToUint64OrDie(const char* str) {
  char* end;
  uint64_t number = static_cast<uint64_t>(strtoll(str, &end, 10));
  if (*end != '\0') {
    PERFETTO_ELOG("Invalid %s. Expected decimal integer.", str);
    exit(1);
  }
  return number;
}

int TextToTrace(std::istream* input, std::ostream* output) {
  std::string trace_text(std::istreambuf_iterator<char>{*input},
                         std::istreambuf_iterator<char>{});
  auto proto_status =
      protozero::TextToProto(kTraceDescriptor.data(), kTraceDescriptor.size(),
                             ".perfetto.protos.Trace", "trace", trace_text);
  if (!proto_status.ok()) {
    PERFETTO_ELOG("Failed to parse trace: %s",
                  proto_status.status().c_message());
    return 1;
  }
  const std::vector<uint8_t>& trace_proto = proto_status.value();
  output->write(reinterpret_cast<const char*>(trace_proto.data()),
                static_cast<int64_t>(trace_proto.size()));
  return 0;
}

int Main(int argc, char** argv) {
  std::vector<const char*> positional_args;
  Keep truncate_keep = Keep::kAll;
  uint64_t pid = 0;
  std::vector<uint64_t> timestamps;
  bool full_sort = false;
  bool perf_profile = false;
  bool profile_no_annotations = false;
  std::vector<std::string> symbol_paths;
  bool no_auto_symbol_paths = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("%s\n", base::GetVersionString());
      return 0;
    }
    if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--truncate") == 0) {
      i++;
      if (i <= argc && strcmp(argv[i], "start") == 0) {
        truncate_keep = Keep::kStart;
      } else if (i <= argc && strcmp(argv[i], "end") == 0) {
        truncate_keep = Keep::kEnd;
      } else {
        PERFETTO_ELOG(
            "--truncate must specify whether to keep the end or the "
            "start of the trace.");
        return Usage(argv[0]);
      }
    } else if (i <= argc && strcmp(argv[i], "--pid") == 0) {
      i++;
      pid = StringToUint64OrDie(argv[i]);
    } else if (i <= argc && strcmp(argv[i], "--timestamps") == 0) {
      i++;
      std::vector<std::string> ts_strings = base::SplitString(argv[i], ",");
      for (const std::string& ts : ts_strings) {
        timestamps.emplace_back(StringToUint64OrDie(ts.c_str()));
      }
    } else if (strcmp(argv[i], "--perf") == 0) {
      perf_profile = true;
    } else if (strcmp(argv[i], "--no-annotations") == 0) {
      profile_no_annotations = true;
    } else if (strcmp(argv[i], "--full-sort") == 0) {
      full_sort = true;
    } else if (i < argc && strcmp(argv[i], "--symbol-paths") == 0) {
      i++;
      symbol_paths = base::SplitString(argv[i], ",");
    } else if (strcmp(argv[i], "--no-auto-symbol-paths") == 0) {
      no_auto_symbol_paths = true;
    } else {
      positional_args.push_back(argv[i]);
    }
  }

  if (positional_args.empty())
    return Usage(argv[0]);

  std::istream* input_stream;
  std::ifstream file_istream;
  if (positional_args.size() > 1) {
    const char* file_path = positional_args[1];
    file_istream.open(file_path, std::ios_base::in | std::ios_base::binary);
    if (!file_istream.is_open())
      PERFETTO_FATAL("Could not open %s", file_path);
    input_stream = &file_istream;
  } else {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    if (isatty(STDIN_FILENO)) {
      PERFETTO_ELOG("Reading from stdin but it's connected to a TTY");
      PERFETTO_LOG("It is unlikely that you want to type in some binary.");
      PERFETTO_LOG("Either pass a file path to the cmdline or pipe stdin");
      return Usage(argv[0]);
    }
#endif
    input_stream = &std::cin;
  }

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // We don't want the runtime to replace "\n" with "\r\n" on `std::cout`.
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  std::ostream* output_stream;
  std::ofstream file_ostream;
  if (positional_args.size() > 2) {
    const char* file_path = positional_args[2];
    file_ostream.open(file_path, std::ios_base::out | std::ios_base::trunc |
                                     std::ios_base::binary);
    if (!file_ostream.is_open())
      PERFETTO_FATAL("Could not open %s", file_path);
    output_stream = &file_ostream;
  } else {
    output_stream = &std::cout;
  }

  std::string format(positional_args[0]);

  if ((format != "profile" && format != "hprof" &&
       format != "java_heap_profile") &&
      (pid != 0 || !timestamps.empty())) {
    PERFETTO_ELOG(
        "--pid and --timestamps are supported only for profile, hprof, "
        "and java_heap_profile formats.");
    return 1;
  }
  if (perf_profile && format != "profile") {
    PERFETTO_ELOG("--perf requires profile format.");
    return 1;
  }

  if (format == "binary") {
    return TextToTrace(input_stream, output_stream);
  }

  if (format == "json")
    return TraceToJson(input_stream, output_stream, /*compress=*/false,
                       truncate_keep, full_sort);

  if (format == "systrace")
    return TraceToSystrace(input_stream, output_stream, /*ctrace=*/false,
                           truncate_keep, full_sort);

  if (format == "ctrace")
    return TraceToSystrace(input_stream, output_stream, /*ctrace=*/true,
                           truncate_keep, full_sort);

  if (truncate_keep != Keep::kAll) {
    PERFETTO_ELOG(
        "--truncate is unsupported for "
        "text|profile|symbolize|decompress_packets format.");
    return 1;
  }

  if (full_sort) {
    PERFETTO_ELOG(
        "--full-sort is unsupported for "
        "text|profile|symbolize|decompress_packets format.");
    return 1;
  }

  if (format == "text") {
    return TraceToText(input_stream, output_stream) ? 0 : 1;
  }

  if (format == "profile") {
    return perf_profile
               ? TraceToPerfProfile(input_stream, output_stream, pid,
                                    timestamps, !profile_no_annotations)
               : TraceToHeapProfile(input_stream, output_stream, pid,
                                    timestamps, !profile_no_annotations);
  }

  if (format == "java_heap_profile") {
    return TraceToJavaHeapProfile(input_stream, output_stream, pid, timestamps,
                                  !profile_no_annotations);
  }

  if (format == "hprof")
    return TraceToHprof(input_stream, output_stream, pid, timestamps);

  if (format == "symbolize")
    return SymbolizeProfile(input_stream, output_stream);

  if (format == "deobfuscate")
    return DeobfuscateProfile(input_stream, output_stream);

  if (format == "firefox")
    return TraceToFirefoxProfile(input_stream, output_stream);

  if (format == "decompress_packets")
    return UnpackCompressedPackets(input_stream, output_stream);

  if (format == "bundle") {
    // Bundle mode requires both input and output file paths
    if (positional_args.size() < 3) {
      PERFETTO_ELOG("Bundle mode requires both input and output file paths");
      return Usage(argv[0]);
    }

    const char* input_file = positional_args[1];
    const char* output_file = positional_args[2];

    // Validate that stdin/stdout are not used for bundle mode
    if (strcmp(input_file, "-") == 0) {
      PERFETTO_ELOG(
          "Bundle mode does not support stdin input, provide file path");
      return 1;
    }
    if (strcmp(output_file, "-") == 0) {
      PERFETTO_ELOG(
          "Bundle mode does not support stdout output, provide file path");
      return 1;
    }

    // Validate input file exists and is readable
    if (!base::FileExists(input_file)) {
      PERFETTO_ELOG("Input file does not exist: %s", input_file);
      return 1;
    }

    BundleContext context;
    context.symbol_paths = symbol_paths;
    context.no_auto_symbol_paths = no_auto_symbol_paths;
    return TraceToBundle(input_file, output_file, context);
  }

  return Usage(argv[0]);
}

}  // namespace
}  // namespace perfetto::trace_to_text

int main(int argc, char** argv) {
  return perfetto::trace_to_text::Main(argc, argv);
}
