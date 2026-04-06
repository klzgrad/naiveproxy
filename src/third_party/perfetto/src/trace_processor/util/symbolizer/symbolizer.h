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

#ifndef SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_SYMBOLIZER_H_
#define SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_SYMBOLIZER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace perfetto {
namespace profiling {

struct SymbolizedFrame {
  std::string function_name;
  std::string file_name;
  uint32_t line = 0;
};

// Reason why a path lookup failed during symbolization.
enum class SymbolPathError : uint8_t {
  // Path lookup succeeded (not an error).
  kOk,
  // The file path didn't exist on disk.
  kFileNotFound,
  // A file was found but had the wrong build ID.
  kBuildIdMismatch,
  // The file exists but couldn't be parsed (e.g., invalid breakpad file).
  kParseError,
  // A directory was indexed but didn't contain a binary with the requested
  // build ID.
  kBuildIdNotInIndex,
};

// Record of a single path attempt during symbolization.
struct SymbolPathAttempt {
  std::string path;
  SymbolPathError error = SymbolPathError::kOk;
};

// Result of a symbolization operation for a single mapping.
struct SymbolizeResult {
  // For each input address, the symbolized frames (may be empty if address
  // couldn't be symbolized even though binary was found).
  std::vector<std::vector<SymbolizedFrame>> frames;

  // All paths that were tried to find the binary. Empty if binary was found
  // without searching (e.g., kernel symbols). On failure, contains all
  // attempted paths with their individual errors.
  std::vector<SymbolPathAttempt> attempts;

  // Returns true if symbolization produced frames.
  bool ok() const { return !frames.empty(); }
};

class Symbolizer {
 public:
  struct Environment {
    // The kernel version; on Linux, corresponds to `uname -r` output
    // (e.g. 6.12.27-1rodete1-amd64).
    std::optional<std::string> os_release;
  };

  // For each address in the input vector, output a vector of SymbolizedFrame
  // representing the functions corresponding to that address. When inlining
  // occurs, this can be more than one function for a single address.
  //
  // On failure, returns empty frames with an error code.
  virtual SymbolizeResult Symbolize(const Environment& env,
                                    const std::string& mapping_name,
                                    const std::string& build_id,
                                    uint64_t load_bias,
                                    const std::vector<uint64_t>& address) = 0;
  virtual ~Symbolizer();
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_SYMBOLIZER_H_
