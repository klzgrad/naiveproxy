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

#include "src/trace_processor/core/interpreter/bytecode_builder.h"

#include <cstdint>
#include <limits>
#include <optional>

#include "perfetto/base/logging.h"
#include "src/trace_processor/core/interpreter/bytecode_core.h"
#include "src/trace_processor/core/interpreter/bytecode_instructions.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::interpreter {

std::optional<uint32_t> BytecodeBuilder::FindBestFitSlot(uint32_t size) const {
  std::optional<uint32_t> best;
  uint32_t best_size = std::numeric_limits<uint32_t>::max();
  for (uint32_t i = 0; i < scratch_slots_.size(); ++i) {
    const auto& slot = scratch_slots_[i];
    if (!slot.in_use && slot.size >= size && slot.size < best_size) {
      best = i;
      best_size = slot.size;
    }
  }
  return best;
}

std::optional<uint32_t> BytecodeBuilder::FindSlotByRegisters(
    ScratchRegisters regs) const {
  for (uint32_t i = 0; i < scratch_slots_.size(); ++i) {
    if (scratch_slots_[i].slab.index == regs.slab.index) {
      return i;
    }
  }
  return std::nullopt;
}

BytecodeBuilder::ScratchRegisters BytecodeBuilder::GetOrCreateScratchRegisters(
    uint32_t size) {
  auto best = FindBestFitSlot(size);
  if (best.has_value()) {
    auto& slot = scratch_slots_[*best];
    PERFETTO_CHECK(!slot.in_use);
    return ScratchRegisters{slot.slab, slot.span};
  }
  auto slab = AllocateRegister<Slab<uint32_t>>();
  auto span = AllocateRegister<Span<uint32_t>>();
  scratch_slots_.push_back(ScratchIndices{size, slab, span, false});
  return ScratchRegisters{slab, span};
}

BytecodeBuilder::ScratchRegisters BytecodeBuilder::AllocateScratch(
    uint32_t size) {
  auto regs = GetOrCreateScratchRegisters(size);

  auto& alloc = AddOpcode<AllocateIndices>(Index<AllocateIndices>());
  alloc.arg<AllocateIndices::size>() = size;
  alloc.arg<AllocateIndices::dest_slab_register>() = regs.slab;
  alloc.arg<AllocateIndices::dest_span_register>() = regs.span;

  // Find the slot and mark it in use.
  auto slot_idx = FindSlotByRegisters(regs);
  PERFETTO_CHECK(slot_idx.has_value());
  scratch_slots_[*slot_idx].in_use = true;

  return regs;
}

void BytecodeBuilder::MarkScratchInUse(ScratchRegisters regs) {
  auto slot_idx = FindSlotByRegisters(regs);
  PERFETTO_CHECK(slot_idx.has_value());
  scratch_slots_[*slot_idx].in_use = true;
}

void BytecodeBuilder::ReleaseScratch(ScratchRegisters regs) {
  auto slot_idx = FindSlotByRegisters(regs);
  if (slot_idx.has_value()) {
    scratch_slots_[*slot_idx].in_use = false;
  }
}

bool BytecodeBuilder::IsScratchInUse(ScratchRegisters regs) const {
  auto slot_idx = FindSlotByRegisters(regs);
  return slot_idx.has_value() && scratch_slots_[*slot_idx].in_use;
}

Bytecode& BytecodeBuilder::AddRawOpcode(uint32_t option) {
  bytecode_.emplace_back();
  bytecode_.back().option = option;
  return bytecode_.back();
}

}  // namespace perfetto::trace_processor::core::interpreter
