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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_H_

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/impl/bytecode_core.h"
#include "src/trace_processor/dataframe/impl/bytecode_interpreter_state.h"
#include "src/trace_processor/dataframe/impl/bytecode_registers.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/types.h"
#include "src/trace_processor/dataframe/value_fetcher.h"

namespace perfetto::trace_processor::dataframe::impl::bytecode {

// The Interpreter class implements a virtual machine that executes bytecode
// instructions for dataframe query operations. It maintains an internal
// register state, processes sequences of bytecode operations, and applies
// filter and transformation operations to data columns. The interpreter is
// designed for high-performance data filtering and manipulation, with
// specialized handling for different data types and comparison operations.
//
// This class is templated on a subclass of ValueFetcher, which is used to
// fetch filter values for each filter spec.
template <typename FilterValueFetcherImpl>
class Interpreter {
 public:
  static_assert(std::is_base_of_v<ValueFetcher, FilterValueFetcherImpl>,
                "FilterValueFetcherImpl must be a subclass of ValueFetcher");

  Interpreter() = default;

  void Initialize(const BytecodeVector& bytecode,
                  uint32_t num_registers,
                  const Column* const* columns,
                  const dataframe::Index* indexes,
                  const StringPool* string_pool) {
    state_.Initialize(bytecode, num_registers, columns, indexes, string_pool);
  }

  // Not movable because it's a very large object and the move cost would be
  // high. Prefer constructing in place.
  Interpreter(Interpreter&&) = delete;
  Interpreter& operator=(Interpreter&&) = delete;

  // Executes the bytecode sequence, processing each bytecode instruction in
  // turn, and dispatching to the appropriate function in this class.
  PERFETTO_ALWAYS_INLINE void Execute(
      FilterValueFetcherImpl& filter_value_fetcher);

  // Returns the value of the specified register if it contains the expected
  // type. Returns nullptr if the register holds a different type or is empty.
  template <typename T>
  PERFETTO_ALWAYS_INLINE const T* GetRegisterValue(reg::ReadHandle<T> reg) {
    return state_.MaybeReadFromRegister(reg);
  }

  // Sets the value of the specified register for testing purposes.
  //
  // Makes it easier to test certain bytecode instructions which depend on
  // the preexisting value of a register.
  template <typename T>
  void SetRegisterValueForTesting(reg::WriteHandle<T> reg, T value) {
    state_.WriteToRegister(reg, std::move(value));
  }

 private:
  InterpreterState state_;
};

}  // namespace perfetto::trace_processor::dataframe::impl::bytecode

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_H_
