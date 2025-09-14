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

#include "src/profiling/symbolizer/llvm_symbolizer.h"

#include <dlfcn.h>

#include <utility>
#include <vector>

#include "perfetto/base/logging.h"

namespace perfetto::profiling {
// dlclose() was not used as it rarely works and is flaky
int SymbolizationResultBatch::CleanUp(ScopedResult* result) {
  if (result && result->c_api_result.ranges && result->free_fn) {
    result->free_fn(result->c_api_result);
  }
  delete result;
  return 0;
}

SymbolizationResultBatch::SymbolizationResultBatch(
    BatchSymbolizationResult c_api_result,
    decltype(&::LlvmSymbolizer_FreeBatchSymbolizationResult) free_fn) {
  if (c_api_result.ranges) {
    scoped_result_handle_.reset(new (std::nothrow)
                                    ScopedResult{c_api_result, free_fn});
    if (!scoped_result_handle_) {
      free_fn(c_api_result);
      return;
    }
    all_frames_ptr_ = c_api_result.frames;
    num_total_frames_ = c_api_result.total_frames;
    ranges_ptr_ = c_api_result.ranges;
    num_ranges_ = c_api_result.num_ranges;
    errors_ptr_ = c_api_result.errors;
    num_errors_ = c_api_result.num_errors;
  }
}

std::pair<const ::LlvmSymbolizedFrame*, uint32_t>
SymbolizationResultBatch::GetFramesForRequest(uint32_t request_index) const {
  if (request_index >= num_ranges_) {
    return {nullptr, 0};
  }
  const auto& range = ranges_ptr_[request_index];
  // Ensure we don't read past the end of the frames buffer.
  if (range.offset + range.num_frames > num_total_frames_) {
    PERFETTO_DFATAL("Invalid range in symbolization result.");
    return {nullptr, 0};
  }
  return {all_frames_ptr_ + range.offset, range.num_frames};
}

std::pair<const ::SymbolizationError*, uint32_t>
SymbolizationResultBatch::GetErrors() const {
  return {errors_ptr_, num_errors_};
}

int LlvmSymbolizer::CleanUpSymbolizer(ScopedSymbolizer* s) {
  if (s && s->symbolizer) {
    s->destroy_fn(s->symbolizer);
  }
  delete s;
  return 0;
}

LlvmSymbolizer::LlvmSymbolizer() {
  library_handle_.reset(dlopen("libllvm_symbolizer_wrapper.so", RTLD_NOW));
  if (!library_handle_) {
    PERFETTO_ELOG("Failed to open libllvm_symbolizer_wrapper.so: %s",
                  dlerror());
    return;
  }

  create_fn_ = reinterpret_cast<decltype(create_fn_)>(
      dlsym(*library_handle_, "LlvmSymbolizer_Create"));
  auto destroy_fn = reinterpret_cast<decltype(&::LlvmSymbolizer_Destroy)>(
      dlsym(*library_handle_, "LlvmSymbolizer_Destroy"));
  symbolize_fn_ = reinterpret_cast<decltype(symbolize_fn_)>(
      dlsym(*library_handle_, "LlvmSymbolizer_Symbolize"));
  free_result_fn_ = reinterpret_cast<decltype(free_result_fn_)>(
      dlsym(*library_handle_, "LlvmSymbolizer_FreeBatchSymbolizationResult"));

  if (!create_fn_ || !destroy_fn || !symbolize_fn_ || !free_result_fn_) {
    PERFETTO_ELOG("Failed to look up symbols in libllvm_symbolizer_wrapper.so");
    library_handle_.reset();  // Release the handle on failure.
    return;
  }

  ::LlvmSymbolizer* symbolizer = create_fn_();
  if (!symbolizer) {
    PERFETTO_ELOG("LlvmSymbolizer_Create() failed.");
    library_handle_.reset();
    create_fn_ = nullptr;
    return;
  }
  scoped_symbolizer_handle_.reset(new ScopedSymbolizer{symbolizer, destroy_fn});
}

SymbolizationResultBatch LlvmSymbolizer::SymbolizeBatch(
    const std::vector<::SymbolizationRequest>& requests) {
  if (!scoped_symbolizer_handle_) {
    return SymbolizationResultBatch({}, free_result_fn_);
  }

  BatchSymbolizationResult batch_result =
      symbolize_fn_(scoped_symbolizer_handle_.get()->symbolizer,
                    requests.data(), static_cast<uint32_t>(requests.size()));
  SymbolizationResultBatch result_batch(batch_result, free_result_fn_);

  if (result_batch.has_errors()) {
    auto errors = result_batch.GetErrors();
    for (uint32_t i = 0; i < errors.second; ++i) {
      const auto& err = errors.first[i];
      PERFETTO_ELOG("LLVM symbolizer failed for request %zu: %s",
                    err.request_index, err.message);
    }
  }

  return result_batch;
}

}  // namespace perfetto::profiling
