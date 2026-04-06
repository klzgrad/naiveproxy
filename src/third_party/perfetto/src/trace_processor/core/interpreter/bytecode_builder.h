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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_BUILDER_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_BUILDER_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::interpreter {

// Low-level builder for bytecode instructions.
//
// This class provides generic bytecode building capabilities. It handles:
// - Register allocation
// - Scratch register management (best-fit allocation)
// - Raw opcode emission
//
// Higher-level builders (like QueryPlanBuilder for dataframes or
// TreeTransformer for trees) use this class internally and add their own
// domain-specific logic.
class BytecodeBuilder {
 public:
  BytecodeBuilder() = default;

  // === Register allocation ===

  // Allocates a new register of type T and returns a read-write handle.
  template <typename T>
  RwHandle<T> AllocateRegister() {
    return RwHandle<T>{register_count_++};
  }

  // Returns the total number of registers allocated.
  uint32_t register_count() const { return register_count_; }

  // === Scratch register management ===
  //
  // These methods manage scratch register state for operations that need
  // temporary storage. Scratch slots are allocated using a best-fit strategy:
  // when requesting scratch of a given size, the smallest existing free slot
  // that can accommodate the request is reused. If no suitable slot exists,
  // a new one is allocated.

  // Result from GetOrCreateScratchRegisters.
  struct ScratchRegisters {
    RwHandle<Slab<uint32_t>> slab;
    RwHandle<Span<uint32_t>> span;
  };

  // Gets or creates scratch registers of the given size using best-fit.
  // Finds the smallest free slot with size >= requested, or allocates new.
  // Does NOT emit AllocateIndices - caller must emit it separately.
  ScratchRegisters GetOrCreateScratchRegisters(uint32_t size);

  // Allocates scratch using best-fit and emits AllocateIndices bytecode.
  // This is the preferred method - combines register allocation + bytecode
  // emission.
  ScratchRegisters AllocateScratch(uint32_t size);

  // Marks the given scratch registers as being in use.
  void MarkScratchInUse(ScratchRegisters regs);

  // Releases the given scratch registers so they can be reused.
  void ReleaseScratch(ScratchRegisters regs);

  // Returns true if the given scratch registers are currently in use.
  bool IsScratchInUse(ScratchRegisters regs) const;

  // === Opcode emission ===

  // Adds a new bytecode instruction of type T with the given option.
  // For simple bytecodes, use Index<T>() from bytecode_instructions.h.
  // For templated bytecodes, use Index<T>(params...) from
  // bytecode_instructions.h.
  template <typename T>
  T& AddOpcode(uint32_t option) {
    return static_cast<T&>(AddRawOpcode(option));
  }

  // Adds a raw bytecode with the given option value.
  Bytecode& AddRawOpcode(uint32_t option);

  // === Bytecode access ===

  BytecodeVector& bytecode() { return bytecode_; }
  const BytecodeVector& bytecode() const { return bytecode_; }

 private:
  // Scratch indices state.
  struct ScratchIndices {
    uint32_t size;
    RwHandle<Slab<uint32_t>> slab;
    RwHandle<Span<uint32_t>> span;
    bool in_use = false;
  };

  // Finds the index of the best-fit free slot for the given size.
  // Returns nullopt if no suitable slot exists.
  std::optional<uint32_t> FindBestFitSlot(uint32_t size) const;

  // Finds the slot index matching the given slab register.
  // Returns nullopt if not found.
  std::optional<uint32_t> FindSlotByRegisters(ScratchRegisters regs) const;

  BytecodeVector bytecode_;
  uint32_t register_count_ = 0;

  // Scratch management - slots indexed internally.
  std::vector<ScratchIndices> scratch_slots_;
};

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_BUILDER_H_
