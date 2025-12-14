/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "src/profiling/perf/frame_pointer_unwinder.h"

#include <cinttypes>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace profiling {

void FramePointerUnwinder::Unwind() {
  if (!IsArchSupported()) {
    PERFETTO_ELOG("Unsupported architecture: %d", arch_);
    last_error_.code = unwindstack::ErrorCode::ERROR_UNSUPPORTED;
    return;
  }

  if (maps_ == nullptr || maps_->Total() == 0) {
    PERFETTO_ELOG("No maps provided");
    last_error_.code = unwindstack::ErrorCode::ERROR_INVALID_MAP;
    return;
  }

  PERFETTO_DCHECK(stack_size_ > 0u);

  frames_.reserve(max_frames_);
  ClearErrors();
  TryUnwind();
}

void FramePointerUnwinder::TryUnwind() {
  uint64_t fp = 0;
  switch (arch_) {
    case unwindstack::ARCH_ARM64:
      fp = reinterpret_cast<uint64_t*>(
          regs_->RawData())[unwindstack::Arm64Reg::ARM64_REG_R29];
      break;
    case unwindstack::ARCH_X86_64:
      fp = reinterpret_cast<uint64_t*>(
          regs_->RawData())[unwindstack::X86_64Reg::X86_64_REG_RBP];
      break;
    case unwindstack::ARCH_RISCV64:
      fp = reinterpret_cast<uint64_t*>(
          regs_->RawData())[unwindstack::Riscv64Reg::RISCV64_REG_S0];
      break;
    case unwindstack::ARCH_UNKNOWN:
    case unwindstack::ARCH_ARM:
    case unwindstack::ARCH_X86:
        // not supported
        ;
  }
  uint64_t sp = regs_->sp();
  uint64_t pc = regs_->pc();
  for (size_t i = 0; i < max_frames_; i++) {
    if (!IsFrameValid(fp, sp))
      return;

    // retrieve the map info and elf info
    std::shared_ptr<unwindstack::MapInfo> map_info = maps_->Find(pc);
    if (map_info == nullptr) {
      last_error_.code = unwindstack::ErrorCode::ERROR_INVALID_MAP;
      return;
    }

    unwindstack::FrameData frame;
    frame.num = i;
    frame.rel_pc = pc;
    frame.pc = pc;
    frame.map_info = map_info;
    unwindstack::Elf* elf = map_info->GetElf(process_memory_, arch_);
    if (elf != nullptr) {
      uint64_t relative_pc = elf->GetRelPc(pc, map_info.get());
      uint64_t pc_adjustment = GetPcAdjustment(relative_pc, elf, arch_);
      frame.rel_pc = relative_pc - pc_adjustment;
      frame.pc = pc - pc_adjustment;
      if (!resolve_names_ ||
          !elf->GetFunctionName(frame.rel_pc, &frame.function_name,
                                &frame.function_offset)) {
        frame.function_name = "";
        frame.function_offset = 0;
      }
    }
    frames_.push_back(frame);
    // move to the next frame
    fp = DecodeFrame(fp, &pc, &sp);
  }
}

uint64_t FramePointerUnwinder::DecodeFrame(uint64_t fp,
                                           uint64_t* next_pc,
                                           uint64_t* next_sp) {
  uint64_t next_fp;
  if (!process_memory_->ReadFully(static_cast<uint64_t>(fp), &next_fp,
                                  sizeof(next_fp)))
    return 0;

  uint64_t pc;
  if (!process_memory_->ReadFully(static_cast<uint64_t>(fp + sizeof(uint64_t)),
                                  &pc, sizeof(pc)))
    return 0;

  // Ensure there's not a stack overflow.
  if (__builtin_add_overflow(fp, sizeof(uint64_t) * 2, next_sp))
    return 0;

  *next_pc = static_cast<uint64_t>(pc);
  return next_fp;
}

bool FramePointerUnwinder::IsFrameValid(uint64_t fp, uint64_t sp) {
  uint64_t align_mask = 0;
  switch (arch_) {
    case unwindstack::ARCH_ARM64:
      align_mask = 0x1;
      break;
    case unwindstack::ARCH_X86_64:
      align_mask = 0xf;
      break;
    case unwindstack::ARCH_RISCV64:
      align_mask = 0x7;
      break;
    case unwindstack::ARCH_UNKNOWN:
    case unwindstack::ARCH_ARM:
    case unwindstack::ARCH_X86:
        // not supported
        ;
  }

  if (fp == 0 || fp <= sp)
    return false;

  // Ensure there's space on the stack to read two values: the caller's
  // frame pointer and the return address.
  uint64_t result;
  if (__builtin_add_overflow(fp, sizeof(uint64_t) * 2, &result))
    return false;

  return result <= stack_end_ && (fp & align_mask) == 0;
}

}  // namespace profiling
}  // namespace perfetto
