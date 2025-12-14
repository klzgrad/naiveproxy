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

#ifndef SRC_PROFILING_SYMBOLIZER_LOCAL_SYMBOLIZER_H_
#define SRC_PROFILING_SYMBOLIZER_LOCAL_SYMBOLIZER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/profiling/symbolizer/subprocess.h"
#include "src/profiling/symbolizer/symbolizer.h"

namespace perfetto::profiling {

bool ParseLlvmSymbolizerJsonLine(const std::string& line,
                                 std::vector<SymbolizedFrame>* result);
enum BinaryType : uint8_t {
  kElf,
  kMachO,
  kMachODsym,
};

struct FoundBinary {
  std::string file_name;
  uint64_t p_vaddr;
  uint64_t p_offset;
  BinaryType type;
};

class BinaryFinder {
 public:
  virtual ~BinaryFinder();
  virtual std::optional<FoundBinary> FindBinary(
      const std::string& abspath,
      const std::string& build_id) = 0;
};

class LocalBinaryIndexer : public BinaryFinder {
 public:
  LocalBinaryIndexer(std::vector<std::string> directories,
                     std::vector<std::string> individual_files);

  std::optional<FoundBinary> FindBinary(const std::string& abspath,
                                        const std::string& build_id) override;
  ~LocalBinaryIndexer() override;

 private:
  std::map<std::string, FoundBinary> buildid_to_file_;
};

class LocalBinaryFinder : public BinaryFinder {
 public:
  explicit LocalBinaryFinder(std::vector<std::string> roots);

  std::optional<FoundBinary> FindBinary(const std::string& abspath,
                                        const std::string& build_id) override;

  ~LocalBinaryFinder() override;

 private:
  std::vector<std::string> roots_;
  std::map<std::string, std::optional<FoundBinary>> cache_;
};

class LLVMSymbolizerProcess {
 public:
  explicit LLVMSymbolizerProcess(const std::string& symbolizer_path);

  std::vector<SymbolizedFrame> Symbolize(const std::string& binary,
                                         uint64_t address);

 private:
  Subprocess subprocess_;
};

class LocalSymbolizer : public Symbolizer {
 public:
  LocalSymbolizer(const std::string& symbolizer_path,
                  std::unique_ptr<BinaryFinder> finder);

  explicit LocalSymbolizer(std::unique_ptr<BinaryFinder> finder);

  std::vector<std::vector<SymbolizedFrame>> Symbolize(
      const Environment& env,
      const std::string& mapping_name,
      const std::string& build_id,
      uint64_t load_bias,
      const std::vector<uint64_t>& address) override;

  ~LocalSymbolizer() override;

 private:
  LLVMSymbolizerProcess llvm_symbolizer_;
  std::unique_ptr<BinaryFinder> finder_;
};

std::unique_ptr<Symbolizer> MaybeLocalSymbolizer(
    const std::vector<std::string>& directories,
    const std::vector<std::string>& individual_files,
    const char* mode);

}  // namespace perfetto::profiling

#endif  // SRC_PROFILING_SYMBOLIZER_LOCAL_SYMBOLIZER_H_
