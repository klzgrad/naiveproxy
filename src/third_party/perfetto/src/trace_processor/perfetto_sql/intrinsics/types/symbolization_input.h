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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_SYMBOLIZATION_INPUT_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_SYMBOLIZATION_INPUT_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/profiling/symbolizer/llvm_symbolizer_c_api.h"

namespace perfetto::trace_processor::perfetto_sql {

// Represents a collection of frames to be symbolized, prepared for the
// LlvmSymbolizer.
struct SymbolizationInput {
  static constexpr char kName[] = "SYMBOLIZATION_INPUT";

  // The requests to be sent to the symbolizer. The `binary_path` pointers
  // in this vector point to the strings stored in `binary_paths`.
  std::vector<::SymbolizationRequest> requests;

  // Storage for the binary path strings.
  std::vector<std::string> binary_paths;

  // Data to be able to join the symbolization results back to the
  // original data. The index corresponds to the index in `requests`.
  std::vector<std::pair<uint64_t, uint64_t>> mapping_id_and_address;
};

}  // namespace perfetto::trace_processor::perfetto_sql

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TYPES_SYMBOLIZATION_INPUT_H_
