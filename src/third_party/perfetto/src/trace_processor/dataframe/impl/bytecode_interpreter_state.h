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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_STATE_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_STATE_H_

#include <cstdint>
#include <cstring>

#include <limits>

#include "perfetto/ext/base/small_vector.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/impl/bytecode_core.h"
#include "src/trace_processor/dataframe/impl/bytecode_registers.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/types.h"

namespace perfetto::trace_processor::dataframe::impl::bytecode {

// The state of the interpreter.
struct InterpreterState {
  // The sequence of bytecode instructions to execute
  BytecodeVector bytecode;
  // Register file holding intermediate values
  base::SmallVector<reg::Value, 16> registers;
  // Pointer to the columns being processed
  const Column* const* columns;
  // Pointer to the indexes
  const dataframe::Index* indexes;
  // Pointer to the string pool (for string operations)
  const StringPool* string_pool;

  /******************************************************************
   * Helper functions for accessing the interpreter state           *
   ******************************************************************/

  void Initialize(const BytecodeVector& bytecode_,
                  uint32_t num_registers,
                  const Column* const* columns_,
                  const dataframe::Index* indexes_,
                  const StringPool* string_pool_) {
    bytecode = bytecode_;
    registers.clear();
    for (uint32_t i = 0; i < num_registers; ++i) {
      registers.emplace_back();
    }
    columns = columns_;
    indexes = indexes_;
    string_pool = string_pool_;
  }

  // Access a register for reading/writing with type safety through the
  // handle.
  template <typename T>
  PERFETTO_ALWAYS_INLINE T& ReadFromRegister(reg::RwHandle<T> r) {
    return base::unchecked_get<T>(registers[r.index]);
  }

  // Access a register for reading only with type safety through the handle.
  template <typename T>
  PERFETTO_ALWAYS_INLINE const T& ReadFromRegister(reg::ReadHandle<T> r) const {
    return base::unchecked_get<T>(registers[r.index]);
  }

  // Conditionally access a register if it contains the expected type.
  // Returns nullptr if the register holds a different type.
  template <typename T>
  PERFETTO_ALWAYS_INLINE const T* MaybeReadFromRegister(
      reg::ReadHandle<T> reg) {
    if (reg.index != std::numeric_limits<uint32_t>::max() &&
        std::holds_alternative<T>(registers[reg.index])) {
      return &base::unchecked_get<T>(registers[reg.index]);
    }
    return nullptr;
  }

  // Conditionally access a register if it contains the expected type.
  // Returns nullptr if the register holds a different type.
  template <typename T>
  PERFETTO_ALWAYS_INLINE T* MaybeReadFromRegister(reg::WriteHandle<T> reg) {
    if (reg.index != std::numeric_limits<uint32_t>::max() &&
        std::holds_alternative<T>(registers[reg.index])) {
      return &base::unchecked_get<T>(registers[reg.index]);
    }
    return nullptr;
  }

  // Writes a value to the specified register, handling type safety through
  // the handle.
  template <typename T>
  PERFETTO_ALWAYS_INLINE void WriteToRegister(reg::WriteHandle<T> r, T value) {
    registers[r.index] = std::move(value);
  }

  const Column& GetColumn(uint32_t idx) const { return *columns[idx]; }
};

}  // namespace perfetto::trace_processor::dataframe::impl::bytecode

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_STATE_H_
