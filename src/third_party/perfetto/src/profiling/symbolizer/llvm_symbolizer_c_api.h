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

#ifndef SRC_PROFILING_SYMBOLIZER_LLVM_SYMBOLIZER_C_API_H_
#define SRC_PROFILING_SYMBOLIZER_LLVM_SYMBOLIZER_C_API_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LLVM_SYMBOLIZER_C_API __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the underlying C++ symbolizer object.
struct LlvmSymbolizer;
typedef struct LlvmSymbolizer LlvmSymbolizer;

// Represents a single symbolization request.
typedef struct {
  // Path to the binary file.
  const char* binary_path;
  // The length of binary_path. If the caller does not know the size and the
  // string is null-terminated, this should be set to uint32_t::max().
  // The implementation currently assumes that the string is null-terminated,
  // but the length is passed for future optimizations.
  uint32_t binary_path_len;
  // The address to be symbolized.
  uint64_t address;
} SymbolizationRequest;

// Represents a single symbolized stack frame.
typedef struct {
  const char* function_name;
  const char* file_name;
  uint32_t line_number;
} LlvmSymbolizedFrame;

// Represents the result of a single symbolization operation as a range in a
// flattened array of frames.
typedef struct {
  // The offset in the `frames` array of the `BatchSymbolizationResult`.
  uint32_t offset;
  // The number of frames for this result.
  uint32_t num_frames;
} SymbolizationResultRange;

// Represents a single error that occurred during symbolization.
typedef struct {
  // The index of the original request that failed.
  size_t request_index;
  // A pointer to the null-terminated error message within the single buffer.
  const char* message;
} SymbolizationError;

// Represents the result of a batch of symbolization operations.
// All pointers point into a single contiguous memory block allocated by
// the symbolizer. The base of this allocation is the `ranges` pointer.
typedef struct {
  // A flat array of all symbolized frames for the entire batch.
  LlvmSymbolizedFrame* frames;
  // The total number of frames in the `frames` array.
  uint32_t total_frames;
  // An array of `SymbolizationResultRange` structs, each representing a range
  // in the `frames` array.
  SymbolizationResultRange* ranges;
  // The number of ranges, corresponding to the number of original requests.
  uint32_t num_ranges;
  // An array of `SymbolizationError` structs, each representing an error that
  // occurred during symbolization.
  SymbolizationError* errors;
  // The number of errors that occurred.
  uint32_t num_errors;
} BatchSymbolizationResult;

// Creates an instance of the LLVM symbolizer.
// Returns NULL on failure.
LLVM_SYMBOLIZER_C_API LlvmSymbolizer* LlvmSymbolizer_Create(void);

// Destroys an instance of the LLVM symbolizer.
LLVM_SYMBOLIZER_C_API void LlvmSymbolizer_Destroy(LlvmSymbolizer* sym);

// Symbolizes a batch of addresses.
// The caller is responsible for freeing the result with
// LlvmSymbolizer_FreeBatchSymbolizationResult.
LLVM_SYMBOLIZER_C_API BatchSymbolizationResult
LlvmSymbolizer_Symbolize(LlvmSymbolizer* sym,
                         const SymbolizationRequest* requests,
                         uint32_t num_requests);

// Frees the memory allocated for a BatchSymbolizationResult.
LLVM_SYMBOLIZER_C_API void LlvmSymbolizer_FreeBatchSymbolizationResult(
    BatchSymbolizationResult result);

#ifdef __cplusplus
}
#endif

#endif  // SRC_PROFILING_SYMBOLIZER_LLVM_SYMBOLIZER_C_API_H_
