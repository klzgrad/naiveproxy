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

#ifndef SRC_PROFILING_SYMBOLIZER_BREAKPAD_SYMBOLIZER_H_
#define SRC_PROFILING_SYMBOLIZER_BREAKPAD_SYMBOLIZER_H_

#include "perfetto/ext/base/string_view.h"
#include "src/profiling/symbolizer/symbolizer.h"

namespace perfetto {
namespace profiling {

// A subclass of Symbolizer that overrides the Symbolize method to make the
// symbolization process work for breakpad files.
class BreakpadSymbolizer : public Symbolizer {
 public:
  // Takes the path to a folder containing breakpad symbol files as an argument.
  // The files in this folder will be used to symbolize a given trace. Each
  // breakpad symbol file should use the upper case hex representation of the
  // module ID, contained in the first line of the file, as the name of the
  // file, with a ".breakpad" suffix. eg: <module_id>.breakpad.
  explicit BreakpadSymbolizer(const std::string& symbol_dir_path);

  BreakpadSymbolizer(BreakpadSymbolizer&& other) = default;
  BreakpadSymbolizer& operator=(BreakpadSymbolizer&& other) = default;

  std::vector<std::vector<SymbolizedFrame>> Symbolize(
      const std::string&,
      const std::string& build_id,
      uint64_t,
      const std::vector<uint64_t>& address) override;

  void SetBreakpadFileForTesting(const std::string& path) {
    file_path_for_testing_ = path;
  }

 private:
  std::string symbol_dir_path_;
  std::string file_path_for_testing_;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_SYMBOLIZER_BREAKPAD_SYMBOLIZER_H_
