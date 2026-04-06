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

#include "src/trace_processor/util/symbolizer/breakpad_symbolizer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/util/symbolizer/breakpad_parser.h"
#include "src/trace_processor/util/symbolizer/symbolizer.h"

namespace perfetto::profiling {

namespace {

// Returns the file path for a breakpad symbol file with the given |build_id|.
std::string MakeFilePath(const std::string& build_id,
                         const std::string& symbol_dir_path) {
  constexpr char kBreakpadSuffix[] = ".breakpad";
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  constexpr char kPathSep = '\\';
#else
  constexpr char kPathSep = '/';
#endif
  std::string file_path = symbol_dir_path;
  if (!file_path.empty() && file_path.back() != kPathSep) {
    file_path += kPathSep;
  }
  file_path.append(build_id);
  file_path.append(kBreakpadSuffix);
  return file_path;
}

}  // namespace

BreakpadSymbolizer::BreakpadSymbolizer(const std::string& symbol_dir_path)
    : symbol_dir_path_(symbol_dir_path) {}

SymbolizeResult BreakpadSymbolizer::Symbolize(
    const Environment&,
    const std::string&,
    const std::string& build_id,
    uint64_t,
    const std::vector<uint64_t>& address) {
  SymbolizeResult result;
  result.frames.reserve(address.size());

  // Skip lookup if build_id is empty (e.g., kernel symbols).
  if (build_id.empty()) {
    return result;
  }

  std::string file_path;
  std::string raw_build_id = base::ToHex(build_id.c_str(), build_id.length());

  // Check to see if the |file_path_for_testing_| member is populated. If it is,
  // this file must be used.
  if (file_path_for_testing_.empty()) {
    file_path = MakeFilePath(raw_build_id, symbol_dir_path_);
  } else {
    file_path = file_path_for_testing_;
  }

  // Check if file exists first to distinguish file-not-found from parse errors.
  if (!base::FileExists(file_path)) {
    result.attempts.push_back({file_path, SymbolPathError::kFileNotFound});
    return result;
  }

  BreakpadParser parser(file_path);
  if (!parser.ParseFile()) {
    result.attempts.push_back({file_path, SymbolPathError::kParseError});
    return result;
  }

  // Record the successful lookup.
  result.attempts.push_back({file_path, SymbolPathError::kOk});

  // Add each address's function name to the |result| vector in the same order.
  for (uint64_t addr : address) {
    SymbolizedFrame frame;
    std::optional<std::string> opt_func_name = parser.GetSymbol(addr);
    if (opt_func_name == std::nullopt) {
      opt_func_name = parser.GetPublicSymbol(addr);
    }

    if (opt_func_name) {
      std::optional<std::tuple<std::string, uint32_t>> opt_file_and_line =
          parser.GetSourceLocation(addr);
      if (opt_file_and_line != std::nullopt) {
        std::tie(frame.file_name, frame.line) = *opt_file_and_line;
      }

      frame.function_name = *opt_func_name;
    }
    result.frames.push_back({std::move(frame)});
  }
  return result;
}

}  // namespace perfetto::profiling
