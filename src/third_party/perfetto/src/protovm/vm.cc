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

#include "src/protovm/vm.h"

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/protovm/error_handling.h"
#include "src/protovm/ro_cursor.h"
#include "src/protovm/rw_proto.h"

namespace perfetto {
namespace protovm {

Vm::Vm(protozero::ConstBytes program,
       size_t memory_limit_bytes,
       protozero::ConstBytes initial_incremental_state)
    : owned_program_(program.ToStdString()),
      state_(std::in_place_type_t<ReadWriteState>{},
             owned_program_,
             memory_limit_bytes,
             initial_incremental_state) {}

Vm::Vm(const Vm& other)
    : owned_program_(other.SerializeProgram()),
      state_(std::in_place_type_t<ReadOnlyState>{},
             other.SerializeIncrementalStateAsString()) {}

StatusOr<void> Vm::ApplyPatch(protozero::ConstBytes packet) {
  ReadWriteState* rw_state = std::get_if<ReadWriteState>(&state_);
  if (!rw_state) {
    return StatusOr<void>::Abort();
  }
  auto src = RoCursor(packet);
  auto dst = rw_state->incremental_state.GetRoot();
  return rw_state->parser.Run(src, dst);
}

void Vm::SerializeIncrementalState(protozero::Message* proto) const {
  if (const ReadOnlyState* state = std::get_if<ReadOnlyState>(&state_); state) {
    proto->AppendRawProtoBytes(state->serialized_incremental_state.data(),
                               state->serialized_incremental_state.size());
    return;
  }

  const ReadWriteState* state = std::get_if<ReadWriteState>(&state_);
  state->incremental_state.Serialize(proto);
}

std::string Vm::SerializeIncrementalStateAsString() const {
  protozero::HeapBuffered<protozero::Message> proto;
  SerializeIncrementalState(proto.get());
  return proto.SerializeAsString();
}

std::string Vm::SerializeProgram() const {
  return owned_program_;
}

std::unique_ptr<Vm> Vm::CloneReadOnly() const {
  return std::unique_ptr<Vm>(new Vm(*this));
}

uint64_t Vm::GetMemoryUsageBytes() const {
  if (const ReadOnlyState* state = std::get_if<ReadOnlyState>(&state_); state) {
    return owned_program_.size() + state->serialized_incremental_state.size();
  }

  const ReadWriteState* state = std::get_if<ReadWriteState>(&state_);
  return owned_program_.size() + state->allocator.GetMemoryUsageBytes();
}

}  // namespace protovm
}  // namespace perfetto
