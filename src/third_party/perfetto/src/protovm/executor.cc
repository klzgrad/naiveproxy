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

#include "src/protovm/executor.h"

namespace perfetto {
namespace protovm {

Executor::~Executor() = default;

StatusOr<void> Executor::EnterField(Cursors* cursors, uint32_t field_id) const {
  if (cursors->selected == CursorEnum::VM_CURSOR_SRC) {
    return cursors->src.EnterField(field_id);
  }

  auto has_field = cursors->dst.HasField(field_id);
  PROTOVM_RETURN_IF_NOT_OK(has_field);

  if (!has_field.value() && !cursors->create_if_not_exist) {
    return StatusOr<void>::Error();
  }

  return cursors->dst.EnterField(field_id);
}

StatusOr<void> Executor::EnterRepeatedFieldAt(Cursors* cursors,
                                              uint32_t field_id,
                                              uint32_t index) const {
  if (cursors->selected == CursorEnum::VM_CURSOR_SRC) {
    return cursors->src.EnterRepeatedFieldAt(field_id, index);
  }

  return cursors->dst.EnterRepeatedFieldAt(field_id, index);
}

StatusOr<void> Executor::EnterRepeatedFieldByKey(Cursors* cursors,
                                                 uint32_t field_id,
                                                 uint32_t map_key_field_id,
                                                 uint64_t key) const {
  if (cursors->selected == CursorEnum::VM_CURSOR_SRC) {
    PROTOVM_ABORT(
        "Mapped repeated fields are currently supported only in RwProto");
  }

  auto has_field = cursors->dst.HasField(field_id);
  PROTOVM_RETURN_IF_NOT_OK(has_field);

  if (!has_field.value() && !cursors->create_if_not_exist) {
    return StatusOr<void>::Error();
  }

  return cursors->dst.EnterRepeatedFieldByKey(field_id, map_key_field_id, key);
}

StatusOr<RoCursor::RepeatedFieldIterator> Executor::IterateRepeatedField(
    RoCursor* src,
    uint32_t field_id) const {
  return src->IterateRepeatedField(field_id);
}

StatusOr<RwProto::Cursor::RepeatedFieldIterator> Executor::IterateRepeatedField(
    RwProto::Cursor* dst,
    uint32_t field_id) const {
  return dst->IterateRepeatedField(field_id);
}

StatusOr<uint64_t> Executor::ReadRegister(uint8_t reg_id) const {
  if (reg_id >= registers_.size()) {
    PROTOVM_ABORT("Register (id = %d) is out of bounds", reg_id);
  }
  if (!registers_[reg_id]) {
    PROTOVM_ABORT("Register (id = %d) is not initialized)", reg_id);
  }
  return StatusOr<uint64_t>{*registers_[reg_id]};
}

StatusOr<void> Executor::WriteRegister(const Cursors& cursors, uint8_t reg_id) {
  if (reg_id >= registers_.size()) {
    PROTOVM_ABORT("Register (id = %d) is out of bounds", reg_id);
  }

  auto status_or_scalar = cursors.selected == CursorEnum::VM_CURSOR_SRC
                              ? cursors.src.GetScalar()
                              : cursors.dst.GetScalar();
  PROTOVM_RETURN_IF_NOT_OK(status_or_scalar);

  registers_[reg_id] = status_or_scalar->value;
  return StatusOr<void>::Ok();
}

StatusOr<void> Executor::Delete(RwProto::Cursor* dst) const {
  return dst->Delete();
}

StatusOr<void> Executor::Merge(Cursors* cursors) const {
  if (!cursors->src.IsBytes()) {
    PROTOVM_ABORT(
        "Attempted MERGE operation but src cursor has incompatible data "
        "type");
  }

  return cursors->dst.Merge(*cursors->src.GetBytes());
}

StatusOr<void> Executor::Set(Cursors* cursors) const {
  if (cursors->src.IsScalar()) {
    auto status_or_scalar = cursors->src.GetScalar();
    PROTOVM_RETURN_IF_NOT_OK(status_or_scalar);
    return cursors->dst.SetScalar(*status_or_scalar);
  }

  if (cursors->src.IsBytes()) {
    auto status_or_bytes = cursors->src.GetBytes();
    PROTOVM_RETURN_IF_NOT_OK(status_or_bytes);
    return cursors->dst.SetBytes(*status_or_bytes);
  }

  PROTOVM_ABORT("Attempted SET operation but src cursor has no valid data");
}

}  // namespace protovm
}  // namespace perfetto
