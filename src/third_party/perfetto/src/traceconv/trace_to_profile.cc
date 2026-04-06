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

#include "src/traceconv/trace_to_profile.h"

#include <cerrno>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/profiling/pprof_builder.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/util/deobfuscation/deobfuscator.h"
#include "src/trace_processor/util/symbolizer/symbolize_database.h"
#include "src/traceconv/utils.h"

namespace {
constexpr const char* kDefaultTmp = "/tmp";

std::string GetTemp() {
  if (auto tmp = getenv("TMPDIR"); tmp)
    return tmp;
  if (auto tmp = getenv("TEMP"); tmp)
    return tmp;
  return kDefaultTmp;
}
}  // namespace

namespace perfetto {
namespace trace_to_text {
namespace {

uint64_t ToConversionFlags(bool annotate_frames) {
  return static_cast<uint64_t>(annotate_frames
                                   ? ConversionFlags::kAnnotateFrames
                                   : ConversionFlags::kNone);
}

void MaybeSymbolize(trace_processor::TraceProcessor* tp, bool verbose) {
  profiling::SymbolizerConfig sym_config;
  const char* mode = getenv("PERFETTO_SYMBOLIZER_MODE");
  std::vector<std::string> paths = profiling::GetPerfettoBinaryPath();
  if (paths.empty()) {
    return;
  }
  if (mode && std::string_view(mode) == "find") {
    sym_config.find_symbol_paths = std::move(paths);
  } else {
    sym_config.index_symbol_paths = std::move(paths);
  }
  auto result = profiling::SymbolizeDatabaseAndLog(tp, sym_config, verbose);
  if (result.error == profiling::SymbolizerError::kOk &&
      !result.symbols.empty()) {
    IngestTraceOrDie(tp, result.symbols);
  }
  tp->Flush();
}

void MaybeDeobfuscate(trace_processor::TraceProcessor* tp) {
  auto maybe_map = profiling::GetPerfettoProguardMapPath();
  if (maybe_map.empty()) {
    return;
  }
  profiling::ReadProguardMapsToDeobfuscationPackets(
      maybe_map, [tp](const std::string& trace_proto) {
        IngestTraceOrDie(tp, trace_proto);
      });
  tp->Flush();
}

std::string GetRandomString(size_t n) {
  std::random_device r;
  auto rng = std::default_random_engine(r());
  std::string result(n, ' ');
  for (size_t i = 0; i < n; ++i) {
    result[i] = 'a' + (rng() % ('z' - 'a'));
  }
  return result;
}

// Creates the destination directory.
// If |output_dir| is not empty, it is used as the destination directory.
// Otherwise, a random temporary directory is created using
// |fallback_dirname_prefix|.
std::string GetDestinationDirectory(
    const std::string& output_dir,
    const std::string& fallback_dirname_prefix) {
  std::string dst_dir;
  if (!output_dir.empty()) {
    dst_dir = output_dir;
  } else {
    dst_dir = GetTemp() + "/" + fallback_dirname_prefix +
              base::GetTimeFmt("%y%m%d%H%M%S") + GetRandomString(5);
  }
  if (!base::Mkdir(dst_dir) && errno != EEXIST) {
    PERFETTO_FATAL("Failed to create output directory %s", dst_dir.c_str());
  }
  return dst_dir;
}

std::optional<ConversionMode> DetectConversionMode(
    trace_processor::TraceProcessor* tp) {
  auto it = tp->ExecuteQuery(R"(
  SELECT
    EXISTS (SELECT 1 FROM __intrinsic_heap_profile_allocation LIMIT 1),
    EXISTS (SELECT 1 FROM perf_sample LIMIT 1),
    EXISTS (SELECT 1 FROM __intrinsic_heap_graph_object LIMIT 1)
  )");
  PERFETTO_CHECK(it.Next());

  int64_t alloc_present = it.Get(0).AsLong();
  int64_t perf_present = it.Get(1).AsLong();
  int64_t graph_present = it.Get(2).AsLong();

  int64_t count = alloc_present + perf_present + graph_present;
  if (count == 0) {
    PERFETTO_LOG("No profiles found.");
    return std::nullopt;
  } else if (count > 1) {
    std::string err_msg =
        "More than one type of profile found in the trace, pass an explicit "
        "disambiguation flag:\n";
    if (alloc_present)
      err_msg += "  --alloc: allocator profile\n";
    if (perf_present)
      err_msg += "  --perf: perf profile\n";
    if (graph_present)
      err_msg += "  --java-heap: java heap graph\n";

    PERFETTO_ELOG("%s", err_msg.c_str());
    return std::nullopt;
  }
  return alloc_present  ? ConversionMode::kHeapProfile
         : perf_present ? ConversionMode::kPerfProfile
                        : ConversionMode::kJavaHeapProfile;
}

}  // namespace

int TraceToProfile(std::istream* input,
                   uint64_t pid,
                   const std::vector<uint64_t>& timestamps,
                   bool annotate_frames,
                   const std::string& output_dir,
                   std::optional<ConversionMode> explicit_mode,
                   bool verbose) {
  // Pre-parse trace.
  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);
  if (!ReadTraceUnfinalized(tp.get(), input))
    return -1;
  tp->Flush();

  // Detect type of profile.
  std::optional<ConversionMode> mode = explicit_mode.has_value()
                                           ? explicit_mode
                                           : DetectConversionMode(tp.get());

  if (!mode.has_value()) {
    return -1;
  }

  int file_idx = 0;
  std::function<std::string(const SerializedProfile&)> filename_fn;
  std::string dir_prefix;
  switch (*mode) {
    case ConversionMode::kHeapProfile:
      filename_fn = [&file_idx](const SerializedProfile& profile) {
        return "heap_dump." + std::to_string(++file_idx) + "." +
               std::to_string(profile.pid) + "." + profile.heap_name + ".pb";
      };
      dir_prefix = "heap_profile-";
      break;
    case ConversionMode::kPerfProfile:
      filename_fn = [&file_idx](const SerializedProfile& profile) {
        return "profile." + std::to_string(++file_idx) + ".pid." +
               std::to_string(profile.pid) + ".pb";
      };
      dir_prefix = "perf_profile-";
      break;
    case ConversionMode::kJavaHeapProfile:
      filename_fn = [&file_idx](const SerializedProfile& profile) {
        return "java_heap_dump." + std::to_string(++file_idx) + "." +
               std::to_string(profile.pid) + ".pb";
      };
      dir_prefix = "heap_profile-";
      break;
  }

  // Add symbolisation and deobfuscation packets.
  MaybeSymbolize(tp.get(), verbose);
  MaybeDeobfuscate(tp.get());
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    return -1;
  }

  // Generate profiles.
  std::vector<SerializedProfile> profiles;
  TraceToPprof(tp.get(), &profiles, *mode, ToConversionFlags(annotate_frames),
               pid, timestamps);
  if (profiles.empty()) {
    return 0;
  }

  // Write profiles to files.
  std::string dst_dir = GetDestinationDirectory(output_dir, dir_prefix);
  for (const auto& profile : profiles) {
    std::string filename = dst_dir + "/" + filename_fn(profile);
    base::ScopedFile fd(base::OpenFile(filename, O_CREAT | O_WRONLY, 0700));
    if (!fd)
      PERFETTO_FATAL("Failed to open %s", filename.c_str());
    PERFETTO_CHECK(base::WriteAll(*fd, profile.serialized.c_str(),
                                  profile.serialized.size()) ==
                   static_cast<ssize_t>(profile.serialized.size()));
  }
  PERFETTO_LOG("Wrote profiles to %s", dst_dir.c_str());
  return 0;
}

}  // namespace trace_to_text
}  // namespace perfetto
