/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/profiling/symbolizer/breakpad_symbolizer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/profiling/symbolizer/breakpad_parser.h"
#include "src/profiling/symbolizer/symbolizer.h"

namespace perfetto::profiling {

namespace {

// Returns the file path for a breakpad symbol file with the given |build_id|.
std::string MakeFilePath(const std::string& build_id,
                         const std::string& symbol_dir_path) {
  // The directory of the symbol file is stored in an environment variable.
  constexpr char kBreakpadSuffix[] = ".breakpad";
  std::string file_path;
  // Append file name to symbol directory path using |build_id| and
  // |kBreakpadSuffix|.
  file_path.append(symbol_dir_path);
// TODO: Add a path utility for perfetto to use here.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  file_path.append("\\");
#else
  file_path.append("/");
#endif
  file_path.append(build_id);
  file_path.append(kBreakpadSuffix);

  return file_path;
}

}  // namespace

BreakpadSymbolizer::BreakpadSymbolizer(const std::string& symbol_dir_path)
    : symbol_dir_path_(symbol_dir_path) {}

std::vector<std::vector<SymbolizedFrame>> BreakpadSymbolizer::Symbolize(
    const Environment&,
    const std::string&,
    const std::string& build_id,
    uint64_t,
    const std::vector<uint64_t>& address) {
  std::vector<std::vector<SymbolizedFrame>> result;
  size_t num_symbolized_frames = 0;
  result.reserve(address.size());
  std::string file_path;
  std::string raw_build_id = base::ToHex(build_id.c_str(), build_id.length());

  // Check to see if the |file_path_for_testing_| member is populated. If it is,
  // this file must be used.
  if (file_path_for_testing_.empty()) {
    file_path = MakeFilePath(raw_build_id, symbol_dir_path_);
  } else {
    file_path = file_path_for_testing_;
  }

  BreakpadParser parser(file_path);
  if (!parser.ParseFile()) {
    PERFETTO_ELOG("Failed to parse file %s.", file_path.c_str());
    PERFETTO_PLOG("Symbolized %zu of %zu frames.", num_symbolized_frames,
                  address.size());
    return result;
  }

  // Add each address's function name to the |result| vector in the same order.
  for (uint64_t addr : address) {
    SymbolizedFrame frame;
    std::optional<std::string> opt_func_name = parser.GetSymbol(addr);
    if (opt_func_name == std::nullopt) {
      opt_func_name = parser.GetPublicSymbol(addr);
    }

    if (opt_func_name) {
      frame.function_name = *opt_func_name;
      num_symbolized_frames++;
    }
    result.push_back({std::move(frame)});
  }
  PERFETTO_PLOG("Symbolized %zu of %zu frames on symbol file %s.",
                num_symbolized_frames, address.size(), file_path.c_str());
  return result;
}

}  // namespace perfetto::profiling
