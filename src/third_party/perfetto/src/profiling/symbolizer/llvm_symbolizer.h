/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_PROFILING_SYMBOLIZER_LLVM_SYMBOLIZER_H_
#define SRC_PROFILING_SYMBOLIZER_LLVM_SYMBOLIZER_H_

#include <utility>
#include <vector>

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_view.h"
#include "src/profiling/symbolizer/llvm_symbolizer_c_api.h"

namespace perfetto::profiling {

class LlvmSymbolizer;

// RAII wrapper for the results of a batch symbolization.
// This object owns the single contiguous block of memory returned by the C API
// and provides safe, non-owning views to the symbolized frames.
class SymbolizationResultBatch {
 public:
  // Returns a pair of (pointer, size) for the frames of a given request.
  std::pair<const ::LlvmSymbolizedFrame*, uint32_t> GetFramesForRequest(
      uint32_t request_index) const;

  // Returns the number of original requests.
  uint32_t size() const { return num_ranges_; }

  // Returns a pair of (pointer, size) for the errors.
  std::pair<const ::SymbolizationError*, uint32_t> GetErrors() const;

  bool has_errors() const { return num_errors_ > 0; }

 private:
  friend class LlvmSymbolizer;

  struct ScopedResult {
    BatchSymbolizationResult c_api_result;
    decltype(&::LlvmSymbolizer_FreeBatchSymbolizationResult) free_fn;
  };
  static int CleanUp(ScopedResult*);
  using ScopedResultHandle =
      base::ScopedResource<ScopedResult*, CleanUp, nullptr>;

  SymbolizationResultBatch(
      BatchSymbolizationResult c_api_result,
      decltype(&::LlvmSymbolizer_FreeBatchSymbolizationResult) free_fn);

  ScopedResultHandle scoped_result_handle_;

  // Non-owning views into the C API's flat buffer, implemented with raw
  // pointers and sizes.
  const ::LlvmSymbolizedFrame* all_frames_ptr_ = nullptr;
  uint32_t num_total_frames_ = 0;

  const SymbolizationResultRange* ranges_ptr_ = nullptr;
  uint32_t num_ranges_ = 0;

  const ::SymbolizationError* errors_ptr_ = nullptr;
  uint32_t num_errors_ = 0;
};

class LlvmSymbolizer {
 public:
  LlvmSymbolizer();

  SymbolizationResultBatch SymbolizeBatch(
      const std::vector<::SymbolizationRequest>& requests);

 private:
  // A no-op closer function for dlopen handles, as dlclose() is flaky.
  // ScopedResource requires a static function pointer for its template.
  static int NoOpDlclose(void*) { return 0; }
  using ScopedLibraryHandle = base::ScopedResource<void*, NoOpDlclose, nullptr>;

  struct ScopedSymbolizer {
    ::LlvmSymbolizer* symbolizer;
    decltype(&::LlvmSymbolizer_Destroy) destroy_fn;
  };
  static int CleanUpSymbolizer(ScopedSymbolizer*);
  using ScopedSymbolizerHandle =
      base::ScopedResource<ScopedSymbolizer*, CleanUpSymbolizer, nullptr>;

  ScopedLibraryHandle library_handle_;
  ScopedSymbolizerHandle scoped_symbolizer_handle_;

  // C API function pointers
  decltype(&::LlvmSymbolizer_Create) create_fn_ = nullptr;
  decltype(&::LlvmSymbolizer_Symbolize) symbolize_fn_ = nullptr;
  decltype(&::LlvmSymbolizer_FreeBatchSymbolizationResult) free_result_fn_ =
      nullptr;
};

}  // namespace perfetto::profiling

#endif  // SRC_PROFILING_SYMBOLIZER_LLVM_SYMBOLIZER_H_
