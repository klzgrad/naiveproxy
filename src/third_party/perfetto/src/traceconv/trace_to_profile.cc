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

#include <random>
#include <string>
#include <vector>

#include "perfetto/trace_processor/trace_processor.h"
#include "src/profiling/symbolizer/local_symbolizer.h"
#include "src/profiling/symbolizer/symbolize_database.h"
#include "src/traceconv/utils.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/temp_file.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/profiling/pprof_builder.h"
#include "src/profiling/symbolizer/symbolizer.h"

namespace {
constexpr const char* kDefaultTmp = "/tmp";

std::string GetTemp() {
  const char* tmp = nullptr;
  if ((tmp = getenv("TMPDIR")))
    return tmp;
  if ((tmp = getenv("TEMP")))
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

std::string GetRandomString(size_t n) {
  std::random_device r;
  auto rng = std::default_random_engine(r());
  std::string result(n, ' ');
  for (size_t i = 0; i < n; ++i) {
    result[i] = 'a' + (rng() % ('z' - 'a'));
  }
  return result;
}

void MaybeSymbolize(trace_processor::TraceProcessor* tp) {
  std::unique_ptr<profiling::Symbolizer> symbolizer =
      profiling::LocalSymbolizerOrDie(profiling::GetPerfettoBinaryPath(),
                                      getenv("PERFETTO_SYMBOLIZER_MODE"));
  if (!symbolizer)
    return;
  profiling::SymbolizeDatabase(tp, symbolizer.get(),
                               [tp](const std::string& trace_proto) {
                                 IngestTraceOrDie(tp, trace_proto);
                               });
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

int TraceToProfile(
    std::istream* input,
    std::ostream* output,
    uint64_t pid,
    std::vector<uint64_t> timestamps,
    ConversionMode conversion_mode,
    uint64_t conversion_flags,
    std::string dirname_prefix,
    std::function<std::string(const SerializedProfile&)> filename_fn) {
  std::vector<SerializedProfile> profiles;
  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  if (!ReadTraceUnfinalized(tp.get(), input))
    return -1;
  tp->Flush();
  MaybeSymbolize(tp.get());
  MaybeDeobfuscate(tp.get());
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    return -1;
  }
  TraceToPprof(tp.get(), &profiles, conversion_mode, conversion_flags, pid,
               timestamps);
  if (profiles.empty()) {
    return 0;
  }

  std::string temp_dir = GetTemp() + "/" + dirname_prefix +
                         base::GetTimeFmt("%y%m%d%H%M%S") + GetRandomString(5);
  PERFETTO_CHECK(base::Mkdir(temp_dir));
  for (const auto& profile : profiles) {
    std::string filename = temp_dir + "/" + filename_fn(profile);
    base::ScopedFile fd(base::OpenFile(filename, O_CREAT | O_WRONLY, 0700));
    if (!fd)
      PERFETTO_FATAL("Failed to open %s", filename.c_str());
    PERFETTO_CHECK(base::WriteAll(*fd, profile.serialized.c_str(),
                                  profile.serialized.size()) ==
                   static_cast<ssize_t>(profile.serialized.size()));
  }
  *output << "Wrote profiles to " << temp_dir << std::endl;
  return 0;
}

}  // namespace

int TraceToHeapProfile(std::istream* input,
                       std::ostream* output,
                       uint64_t pid,
                       std::vector<uint64_t> timestamps,
                       bool annotate_frames) {
  int file_idx = 0;
  auto filename_fn = [&file_idx](const SerializedProfile& profile) {
    return "heap_dump." + std::to_string(++file_idx) + "." +
           std::to_string(profile.pid) + "." + profile.heap_name + ".pb";
  };

  return TraceToProfile(
      input, output, pid, timestamps, ConversionMode::kHeapProfile,
      ToConversionFlags(annotate_frames), "heap_profile-", filename_fn);
}

int TraceToPerfProfile(std::istream* input,
                       std::ostream* output,
                       uint64_t pid,
                       std::vector<uint64_t> timestamps,
                       bool annotate_frames) {
  int file_idx = 0;
  auto filename_fn = [&file_idx](const SerializedProfile& profile) {
    return "profile." + std::to_string(++file_idx) + ".pid." +
           std::to_string(profile.pid) + ".pb";
  };

  return TraceToProfile(
      input, output, pid, timestamps, ConversionMode::kPerfProfile,
      ToConversionFlags(annotate_frames), "perf_profile-", filename_fn);
}

int TraceToJavaHeapProfile(std::istream* input,
                           std::ostream* output,
                           const uint64_t pid,
                           const std::vector<uint64_t>& timestamps,
                           const bool annotate_frames) {
  int file_idx = 0;
  auto filename_fn = [&file_idx](const SerializedProfile& profile) {
    return "java_heap_dump." + std::to_string(++file_idx) + "." +
           std::to_string(profile.pid) + ".pb";
  };

  return TraceToProfile(
      input, output, pid, timestamps, ConversionMode::kJavaHeapProfile,
      ToConversionFlags(annotate_frames), "heap_profile-", filename_fn);
}
}  // namespace trace_to_text
}  // namespace perfetto
