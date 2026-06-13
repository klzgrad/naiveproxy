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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INTERPRETER_STATE_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INTERPRETER_STATE_H_

#include <cstdint>
#include <cstring>

#include <limits>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"

namespace perfetto::trace_processor::core::interpreter {

// The state of the interpreter.
struct InterpreterState {
  // The sequence of bytecode instructions to execute
  BytecodeVector bytecode;
  // Register file holding intermediate values
  base::SmallVector<RegValue, 16> registers;
  // Pointer to the string pool (for string operations)
  const StringPool* string_pool;

  /******************************************************************
   * Helper functions for accessing the interpreter state           *
   ******************************************************************/

  void Initialize(const BytecodeVector& bytecode_,
                  uint32_t num_registers,
                  const StringPool* string_pool_) {
    bytecode = bytecode_;
    registers.clear();
    for (uint32_t i = 0; i < num_registers; ++i) {
      registers.emplace_back();
    }
    string_pool = string_pool_;
  }

  // Access a register for reading/writing with type safety through the
  // handle.
  template <typename T>
  PERFETTO_ALWAYS_INLINE T& ReadFromRegister(RwHandle<T> r) {
    return base::unchecked_get<T>(registers[r.index]);
  }

  // Access a register for reading only with type safety through the handle.
  template <typename T>
  PERFETTO_ALWAYS_INLINE const T& ReadFromRegister(ReadHandle<T> r) const {
    return base::unchecked_get<T>(registers[r.index]);
  }

  // Conditionally access a register if it contains the expected type.
  // Returns nullptr if the register holds a different type.
  template <typename T>
  PERFETTO_ALWAYS_INLINE const T* MaybeReadFromRegister(ReadHandle<T> reg) {
    if (reg.index != std::numeric_limits<uint32_t>::max() &&
        std::holds_alternative<T>(registers[reg.index])) {
      return &base::unchecked_get<T>(registers[reg.index]);
    }
    return nullptr;
  }

  // Conditionally access a register if it contains the expected type.
  // Returns nullptr if the register holds a different type.
  template <typename T>
  PERFETTO_ALWAYS_INLINE T* MaybeReadFromRegister(WriteHandle<T> reg) {
    if (reg.index != std::numeric_limits<uint32_t>::max() &&
        std::holds_alternative<T>(registers[reg.index])) {
      return &base::unchecked_get<T>(registers[reg.index]);
    }
    return nullptr;
  }

  template <typename T>
  PERFETTO_ALWAYS_INLINE const auto* ReadStorageFromRegister(
      ReadHandle<StoragePtr> reg) {
    // For Id columns, the register contains a StoragePtr with nullptr.
    // The caller is expected to handle this case (the row index IS the value).
    return static_cast<const typename T::cpp_type*>(ReadFromRegister(reg).ptr);
  }

  // Writes a value to the specified register, handling type safety through
  // the handle.
  template <typename T>
  PERFETTO_ALWAYS_INLINE void WriteToRegister(WriteHandle<T> r, T value) {
    registers[r.index] = std::move(value);
  }
  PERFETTO_ALWAYS_INLINE void WriteToRegister(HandleBase r, RegValue value) {
    registers[r.index] = std::move(value);
  }
};

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_INTERPRETER_STATE_H_
