/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_PROTOVM_COMPILER_INSTRUCTION_EMITTER_H_
#define SRC_PROTOVM_COMPILER_INSTRUCTION_EMITTER_H_

#include <string_view>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "protos/perfetto/protovm/vm_program.pbzero.h"
#include "src/trace_processor/util/descriptors.h"

namespace perfetto::protovm {

// Implements the compiler back-end.
//
// Translates high-level commands (CompileConfig) into corresponding sequences
// of low-level instructions (VmProgram/VmInstruction).
class InstructionEmitter {
 public:
  using AbortLevel = protos::pbzero::VmInstruction::AbortLevel;
  using Cursor = perfetto::protos::pbzero::VmCursorEnum;

  static constexpr auto kDefaultAbortLevel = protos::pbzero::VmInstruction::
      AbortLevel::SKIP_CURRENT_INSTRUCTION_AND_BREAK_OUTER;

  struct Scope {
    Scope AddNestedInstruction() const;
    perfetto::protos::pbzero::VmInstruction* GetInstruction() const;

    std::variant<perfetto::protos::pbzero::VmInstruction*,
                 perfetto::protos::pbzero::VmProgram*>
        instruction;
    const perfetto::trace_processor::ProtoDescriptor* src_proto;
    const perfetto::trace_processor::ProtoDescriptor* dst_proto;
  };

  explicit InstructionEmitter(perfetto::trace_processor::DescriptorPool* pool);

  base::StatusOr<Scope> Select(
      const Scope& scope,
      const std::vector<std::string_view>& relative_path,
      Cursor cursor,
      bool create_if_not_exist,
      AbortLevel abort_level) const;

  base::StatusOr<Scope> SelectByKey(
      const Scope& scope,
      const std::vector<std::string_view>& dst_relative_path,
      std::string_view key_field_name,
      uint32_t register_id,
      bool create_if_not_exist,
      AbortLevel abort_level) const;

  base::Status Set(const Scope& scope,
                   const std::vector<std::string_view>& src_relative_path,
                   const std::vector<std::string_view>& dst_relative_path,
                   AbortLevel abort_level) const;

  base::Status Merge(const Scope& scope,
                     const std::vector<std::string_view>& src_relative_path,
                     const std::vector<std::string_view>& dst_relative_path,
                     bool is_recursive,
                     AbortLevel abort_level) const;

  base::Status MergeByKey(
      const Scope& scope,
      const std::vector<std::string_view>& src_relative_path,
      const std::vector<std::string_view>& dst_relative_path,
      std::string_view key_field_name,
      bool is_recursive,
      AbortLevel abort_level) const;

  base::Status DeleteIfPresent(
      const Scope& scope,
      const std::vector<std::string_view>& src_relative_path,
      const std::vector<std::string_view>& dst_relative_path,
      AbortLevel abort_level) const;

  base::Status DeleteByKey(
      const Scope& scope,
      const std::vector<std::string_view>& src_relative_path,
      const std::vector<std::string_view>& dst_relative_path,
      std::string_view key_field_name,
      AbortLevel abort_level) const;

 private:
  struct Field {
    uint32_t id;
    const perfetto::trace_processor::FieldDescriptor* descriptor;
    const perfetto::trace_processor::ProtoDescriptor* proto;
  };

  void MaybeSetAbortLevel(AbortLevel abort_level,
                          protos::pbzero::VmInstruction* instruction) const;

  base::StatusOr<Field> LookupProtoField(
      const perfetto::trace_processor::ProtoDescriptor* proto,
      std::string_view field_name) const;

  bool IsDeprecated(
      const perfetto::trace_processor::FieldDescriptor& field_descriptor) const;

  perfetto::trace_processor::DescriptorPool* pool_;
};

}  // namespace perfetto::protovm

#endif  // SRC_PROTOVM_COMPILER_INSTRUCTION_EMITTER_H_
