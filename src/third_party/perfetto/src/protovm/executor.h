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

#ifndef SRC_PROTOVM_EXECUTOR_H_
#define SRC_PROTOVM_EXECUTOR_H_

#include <cstdint>
#include <optional>

#include "protos/perfetto/protovm/vm_program.pbzero.h"

#include "src/protovm/error_handling.h"
#include "src/protovm/ro_cursor.h"
#include "src/protovm/rw_proto.h"

namespace perfetto {
namespace protovm {

using CursorEnum = perfetto::protos::pbzero::VmCursorEnum;

struct Cursors {
  RoCursor src;
  RwProto::Cursor dst;
  CursorEnum selected{CursorEnum::VM_CURSOR_UNSPECIFIED};
  bool create_if_not_exist{false};
};

// Executor's methods are virtual to be overridden in tests
class Executor {
 public:
  virtual ~Executor();
  virtual StatusOr<void> EnterField(Cursors* cursors, uint32_t field_id) const;
  virtual StatusOr<void> EnterRepeatedFieldAt(Cursors* cursors,
                                              uint32_t field_id,
                                              uint32_t index) const;
  virtual StatusOr<void> EnterRepeatedFieldByKey(Cursors* cursors,
                                                 uint32_t field_id,
                                                 uint32_t map_key_field_id,
                                                 uint64_t key) const;
  virtual StatusOr<RoCursor::RepeatedFieldIterator> IterateRepeatedField(
      RoCursor* src,
      uint32_t field_id) const;
  virtual StatusOr<RwProto::Cursor::RepeatedFieldIterator> IterateRepeatedField(
      RwProto::Cursor* dst,
      uint32_t field_id) const;
  virtual StatusOr<uint64_t> ReadRegister(uint8_t reg_id) const;
  virtual StatusOr<void> WriteRegister(const Cursors& cursors, uint8_t reg_id);
  virtual StatusOr<void> Delete(RwProto::Cursor* dst) const;
  virtual StatusOr<void> Merge(Cursors* cursors) const;
  virtual StatusOr<void> Set(Cursors* cursors) const;

 private:
  std::array<std::optional<uint64_t>, 32> registers_;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_EXECUTOR_H_
