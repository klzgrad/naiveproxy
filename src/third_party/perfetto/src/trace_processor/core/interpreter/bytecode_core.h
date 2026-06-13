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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_CORE_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_CORE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "perfetto/ext/base/small_vector.h"

namespace perfetto::trace_processor::core::interpreter {

// Base bytecode structure representing a single instruction with operation
// code and fixed-size buffer for arguments.
struct Bytecode {
  uint32_t option = 0;                    // Opcode determining instruction type
  std::array<uint8_t, 36> args_buffer{};  // Storage for instruction arguments
};
static_assert(std::is_trivially_copyable_v<Bytecode>);
static_assert(sizeof(Bytecode) <= 40);

// Indicates that the bytecode has a fixed cost.
struct FixedCost {
  double cost;
};

// Indicates that the bytecode has `cost` multiplied by `log2(estimated row
// count)`.
struct LogPerRowCost {
  double cost;
};

// Indicates that the bytecode has `cost` multiplied by `estimated row count`.
struct LinearPerRowCost {
  double cost;
};

// Indicates that the bytecode has `cost` multiplied by `log2(estimated row
// count) * estimated row count`.
struct LogLinearPerRowCost {
  double cost;
};

// Indicates that the bytecode has `cost` multiplied by the `estimated row
// count` *after* the operation completes (as opposed to `LinearPerRowCost`
// which is *before* the operation completes).
struct PostOperationLinearPerRowCost {
  double cost;
};

// A variant used to specify the cost of a bytecode operation.
using Cost = std::variant<FixedCost,
                          LogPerRowCost,
                          LinearPerRowCost,
                          LogLinearPerRowCost,
                          PostOperationLinearPerRowCost>;

// Vector type for storing sequences of bytecode instructions.
using BytecodeVector = base::SmallVector<Bytecode, 16>;

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_CORE_H_
