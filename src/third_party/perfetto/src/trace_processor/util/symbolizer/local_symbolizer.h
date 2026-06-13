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

#ifndef SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_LOCAL_SYMBOLIZER_H_
#define SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_LOCAL_SYMBOLIZER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "src/trace_processor/util/symbolizer/subprocess.h"
#include "src/trace_processor/util/symbolizer/symbolizer.h"

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

// Reason why a path lookup failed.
enum class BinaryPathError : uint8_t {
  // Path lookup succeeded (not an error).
  kOk,
  // The file path didn't exist on disk.
  kFileNotFound,
  // A file was found but had the wrong build ID.
  kBuildIdMismatch,
  // A directory was indexed but didn't contain a binary with the requested
  // build ID.
  kBuildIdNotInIndex,
};

// Record of a single path attempt during binary lookup.
struct BinaryPathAttempt {
  std::string path;
  BinaryPathError error = BinaryPathError::kOk;
};

// Result of a binary lookup operation.
struct BinaryLookupResult {
  std::optional<FoundBinary> binary;
  // All paths that were tried (including the successful one, if any).
  // Empty only if no paths were applicable (e.g., empty roots list).
  std::vector<BinaryPathAttempt> attempts;

  // Returns true if a binary was found.
  bool ok() const { return binary.has_value(); }
};

class BinaryFinder {
 public:
  virtual ~BinaryFinder();
  virtual BinaryLookupResult FindBinary(const std::string& abspath,
                                        const std::string& build_id) = 0;
};

class LocalBinaryIndexer : public BinaryFinder {
 public:
  LocalBinaryIndexer(std::vector<std::string> directories,
                     std::vector<std::string> individual_files);

  BinaryLookupResult FindBinary(const std::string& abspath,
                                const std::string& build_id) override;
  ~LocalBinaryIndexer() override;

 private:
  std::vector<std::string> indexed_directories_;
  std::set<std::string> symbol_files_;
  std::map<std::string, FoundBinary> buildid_to_file_;
};

class LocalBinaryFinder : public BinaryFinder {
 public:
  explicit LocalBinaryFinder(std::vector<std::string> roots);

  BinaryLookupResult FindBinary(const std::string& abspath,
                                const std::string& build_id) override;

  ~LocalBinaryFinder() override;

 private:
  std::vector<std::string> roots_;
  std::map<std::string, BinaryLookupResult> cache_;
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

  SymbolizeResult Symbolize(const Environment& env,
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

#endif  // SRC_TRACE_PROCESSOR_UTIL_SYMBOLIZER_LOCAL_SYMBOLIZER_H_
