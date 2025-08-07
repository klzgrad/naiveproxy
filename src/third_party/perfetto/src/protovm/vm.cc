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
#include "src/protovm/error_handling.h"
#include "src/protovm/ro_cursor.h"
#include "src/protovm/rw_proto.h"

namespace perfetto {
namespace protovm {

Vm::Vm(protozero::ConstBytes program, size_t memory_limit_bytes)
    : executor_{},
      parser_(program, &executor_),
      allocator_{memory_limit_bytes},
      incremental_state_{&allocator_} {}

StatusOr<void> Vm::ApplyPatch(protozero::ConstBytes packet) {
  auto src = RoCursor(packet);
  auto dst = incremental_state_.GetRoot();
  return parser_.Run(src, dst);
}

std::string Vm::SerializeIncrementalState() const {
  return incremental_state_.SerializeAsString();
}

}  // namespace protovm
}  // namespace perfetto
