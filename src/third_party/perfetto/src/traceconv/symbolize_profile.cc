/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/traceconv/symbolize_profile.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/util/symbolizer/symbolize_database.h"
#include "src/traceconv/utils.h"

namespace perfetto {
namespace trace_to_text {

// Ingest profile, and emit a symbolization table for each sequence. This can
// be prepended to the profile to attach the symbol information.
int SymbolizeProfile(std::istream* input, std::ostream* output, bool verbose) {
  profiling::SymbolizerConfig sym_config;

  const char* breakpad_dir = getenv("BREAKPAD_SYMBOL_DIR");
  if (breakpad_dir != nullptr) {
    sym_config.breakpad_paths.push_back(breakpad_dir);
  } else {
    const char* mode = getenv("PERFETTO_SYMBOLIZER_MODE");
    std::vector<std::string> paths = profiling::GetPerfettoBinaryPath();
    if (mode && std::string_view(mode) == "find") {
      sym_config.find_symbol_paths = std::move(paths);
    } else {
      sym_config.index_symbol_paths = std::move(paths);
    }
  }

  if (sym_config.index_symbol_paths.empty() &&
      sym_config.find_symbol_paths.empty() &&
      sym_config.breakpad_paths.empty()) {
    PERFETTO_FATAL("No symbol paths configured");
  }

  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  if (!ReadTraceUnfinalized(tp.get(), input))
    PERFETTO_FATAL("Failed to read trace.");

  tp->Flush();
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    PERFETTO_FATAL("%s", status.c_message());
  }

  auto result =
      profiling::SymbolizeDatabaseAndLog(tp.get(), sym_config, verbose);
  if (result.error != profiling::SymbolizerError::kOk) {
    PERFETTO_FATAL("Symbolization failed: %s", result.error_details.c_str());
  }
  *output << result.symbols;

  return 0;
}

}  // namespace trace_to_text
}  // namespace perfetto
