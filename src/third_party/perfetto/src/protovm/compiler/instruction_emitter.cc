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

#include "src/protovm/compiler/instruction_emitter.h"

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/status_macros.h"
#include "protos/perfetto/common/descriptor.pbzero.h"
#include "protos/perfetto/protovm/vm_program.pbzero.h"

namespace perfetto::protovm {

InstructionEmitter::Scope InstructionEmitter::Scope::AddNestedInstruction()
    const {
  perfetto::protos::pbzero::VmInstruction* nested_instruction = nullptr;
  auto* program =
      std::get_if<perfetto::protos::pbzero::VmProgram*>(&instruction);
  if (program) {
    nested_instruction = (*program)->add_instructions();
  } else {
    nested_instruction =
        std::get<perfetto::protos::pbzero::VmInstruction*>(instruction)
            ->add_nested_instructions();
  }
  return {nested_instruction, src_proto, dst_proto};
}

perfetto::protos::pbzero::VmInstruction*
InstructionEmitter::Scope::GetInstruction() const {
  return std::get<perfetto::protos::pbzero::VmInstruction*>(instruction);
}

InstructionEmitter::InstructionEmitter(
    perfetto::trace_processor::DescriptorPool* pool)
    : pool_(pool) {
  PERFETTO_CHECK(pool_);
}

base::StatusOr<InstructionEmitter::Scope> InstructionEmitter::Select(
    const Scope& scope,
    const std::vector<std::string_view>& relative_path,
    Cursor cursor,
    bool create_if_not_exist,
    AbortLevel abort_level) const {
  if (relative_path.empty()) {
    return scope;
  }

  auto new_scope = scope.AddNestedInstruction();
  MaybeSetAbortLevel(abort_level, new_scope.GetInstruction());

  auto* select = new_scope.GetInstruction()->set_select();

  const perfetto::trace_processor::ProtoDescriptor** current_proto = nullptr;
  if (cursor == Cursor::VM_CURSOR_SRC) {
    current_proto = &new_scope.src_proto;
  } else {
    PERFETTO_CHECK(cursor == Cursor::VM_CURSOR_DST);
    current_proto = &new_scope.dst_proto;
    select->set_cursor(cursor);
    if (create_if_not_exist) {
      select->set_create_if_not_exist(true);
    }
  }

  for (const auto& field_name : relative_path) {
    ASSIGN_OR_RETURN(auto field, LookupProtoField(*current_proto, field_name));
    auto* path_component = select->add_relative_path();
    path_component->set_field_id(field.id);
    if (field.descriptor->is_repeated()) {
      path_component->set_is_repeated(true);
    }
    *current_proto = field.proto;
  }

  return new_scope;
}

base::StatusOr<InstructionEmitter::Scope> InstructionEmitter::SelectByKey(
    const Scope& scope,
    const std::vector<std::string_view>& dst_relative_path,
    std::string_view key_field_name,
    uint32_t register_id,
    bool create_if_not_exist,
    AbortLevel abort_level) const {
  auto new_scope = scope.AddNestedInstruction();
  MaybeSetAbortLevel(abort_level, new_scope.GetInstruction());

  auto* select = new_scope.GetInstruction()->set_select();
  select->set_cursor(Cursor::VM_CURSOR_DST);
  if (create_if_not_exist) {
    select->set_create_if_not_exist(true);
  }

  for (const auto& field_name : dst_relative_path) {
    ASSIGN_OR_RETURN(auto field,
                     LookupProtoField(new_scope.dst_proto, field_name));
    select->add_relative_path()->set_field_id(field.id);
    new_scope.dst_proto = field.proto;
  }

  ASSIGN_OR_RETURN(auto key_field,
                   LookupProtoField(new_scope.dst_proto, key_field_name));
  auto* last_path_component = select->add_relative_path();
  last_path_component->set_map_key_field_id(key_field.id);
  last_path_component->set_register_to_match(register_id);

  return new_scope;
}

base::Status InstructionEmitter::Set(
    const Scope& scope,
    const std::vector<std::string_view>& src_relative_path,
    const std::vector<std::string_view>& dst_relative_path,
    AbortLevel abort_level) const {
  ASSIGN_OR_RETURN(auto src, Select(scope, src_relative_path,
                                    Cursor::VM_CURSOR_SRC, false, abort_level));
  ASSIGN_OR_RETURN(auto dst,
                   Select(src, dst_relative_path, Cursor::VM_CURSOR_DST, true,
                          kDefaultAbortLevel));
  auto new_scope = dst.AddNestedInstruction();
  new_scope.GetInstruction()->set_set();
  return base::OkStatus();
}

base::Status InstructionEmitter::Merge(
    const Scope& scope,
    const std::vector<std::string_view>& src_relative_path,
    const std::vector<std::string_view>& dst_relative_path,
    bool is_recursive,
    AbortLevel abort_level) const {
  ASSIGN_OR_RETURN(auto src, Select(scope, src_relative_path,
                                    Cursor::VM_CURSOR_SRC, false, abort_level));
  ASSIGN_OR_RETURN(auto dst,
                   Select(src, dst_relative_path, Cursor::VM_CURSOR_DST, true,
                          kDefaultAbortLevel));

  if (is_recursive) {
    const auto* proto = dst.src_proto;
    PERFETTO_CHECK(dst.src_proto == dst.dst_proto);
    PERFETTO_CHECK(proto);

    for (const auto& [field_id, field_descriptor] : proto->fields()) {
      if (field_descriptor.type() !=
          perfetto::protos::pbzero::FieldDescriptorProto::TYPE_MESSAGE) {
        continue;
      }
      if (field_descriptor.is_repeated()) {
        continue;
      }
      if (IsDeprecated(field_descriptor)) {
        // TODO(keanmariotti): currently the recursion includes only
        // non-deprecated fields. Going forward we might want a more flexible
        // filtering mechanism. E.g. configure an allow/deny list of field
        // options to be included/skipped.
        continue;
      }
      RETURN_IF_ERROR(Merge(dst, {field_descriptor.name()},
                            {field_descriptor.name()}, true,
                            AbortLevel::SKIP_CURRENT_INSTRUCTION));
    }
  }

  auto new_scope = dst.AddNestedInstruction();
  auto* merge = new_scope.GetInstruction()->set_merge();
  merge->set_del_if_src_empty(true);
  if (is_recursive) {
    merge->set_skip_submessages(true);
  }
  return base::OkStatus();
}

base::Status InstructionEmitter::MergeByKey(
    const Scope& scope,
    const std::vector<std::string_view>& src_relative_path,
    const std::vector<std::string_view>& dst_relative_path,
    std::string_view key_field_name,
    bool is_recursive,
    AbortLevel abort_level) const {
  static constexpr uint32_t REGISTER_ID = 0;

  ASSIGN_OR_RETURN(auto src_message_scope,
                   Select(scope, src_relative_path, Cursor::VM_CURSOR_SRC,
                          false, abort_level));
  ASSIGN_OR_RETURN(auto src_key_field_scope,
                   Select(src_message_scope, {key_field_name},
                          Cursor::VM_CURSOR_SRC, false, AbortLevel::ABORT));

  auto reg_load_scope = src_key_field_scope.AddNestedInstruction();
  reg_load_scope.GetInstruction()->set_reg_load()->set_dst_register(
      REGISTER_ID);

  ASSIGN_OR_RETURN(
      auto dst_message_scope,
      SelectByKey(src_message_scope, dst_relative_path, key_field_name,
                  REGISTER_ID, true, kDefaultAbortLevel));
  RETURN_IF_ERROR(
      Merge(dst_message_scope, {}, {}, is_recursive, kDefaultAbortLevel));
  return base::OkStatus();
}

base::Status InstructionEmitter::DeleteIfPresent(
    const Scope& scope,
    const std::vector<std::string_view>& src_relative_path,
    const std::vector<std::string_view>& dst_relative_path,
    AbortLevel abort_level) const {
  ASSIGN_OR_RETURN(auto src_scope,
                   Select(scope, src_relative_path, Cursor::VM_CURSOR_SRC,
                          false, abort_level));
  ASSIGN_OR_RETURN(auto dst_scope,
                   Select(src_scope, dst_relative_path, Cursor::VM_CURSOR_DST,
                          false, kDefaultAbortLevel));
  dst_scope.AddNestedInstruction().GetInstruction()->set_del();
  return base::OkStatus();
}

base::Status InstructionEmitter::DeleteByKey(
    const Scope& scope,
    const std::vector<std::string_view>& src_relative_path,
    const std::vector<std::string_view>& dst_relative_path,
    std::string_view key_field_name,
    AbortLevel abort_level) const {
  static constexpr uint32_t REGISTER_ID = 0;

  ASSIGN_OR_RETURN(auto src_scope,
                   Select(scope, src_relative_path, Cursor::VM_CURSOR_SRC,
                          false, abort_level));

  auto reg_load_scope = src_scope.AddNestedInstruction();
  reg_load_scope.GetInstruction()->set_reg_load()->set_dst_register(
      REGISTER_ID);

  ASSIGN_OR_RETURN(auto dst_scope,
                   SelectByKey(src_scope, dst_relative_path, key_field_name,
                               REGISTER_ID, false, kDefaultAbortLevel));

  auto del_scope = dst_scope.AddNestedInstruction();
  del_scope.GetInstruction()->set_del();
  return base::OkStatus();
}

void InstructionEmitter::MaybeSetAbortLevel(
    AbortLevel abort_level,
    protos::pbzero::VmInstruction* instruction) const {
  if (abort_level != kDefaultAbortLevel) {
    instruction->set_abort_level(abort_level);
  }
}

base::StatusOr<InstructionEmitter::Field> InstructionEmitter::LookupProtoField(
    const perfetto::trace_processor::ProtoDescriptor* proto,
    std::string_view field_name) const {
  PERFETTO_CHECK(proto);
  auto* field_descriptor = proto->FindFieldByName(std::string(field_name));
  if (!field_descriptor) {
    return base::ErrStatus("Failed to lookup field name: %s",
                           std::string(field_name).c_str());
  }

  if (field_descriptor->type() !=
      perfetto::protos::pbzero::FieldDescriptorProto::TYPE_MESSAGE) {
    return Field{field_descriptor->number(), field_descriptor, nullptr};
  }

  auto new_proto_idx =
      pool_->FindDescriptorIdx(field_descriptor->resolved_type_name());
  if (!new_proto_idx) {
    return base::ErrStatus("Failed to find descriptor for type: %s",
                           field_descriptor->resolved_type_name().c_str());
  }
  auto* new_proto = &pool_->descriptors()[*new_proto_idx];

  return Field{field_descriptor->number(), field_descriptor, new_proto};
}

bool InstructionEmitter::IsDeprecated(
    const perfetto::trace_processor::FieldDescriptor& field_descriptor) const {
  protozero::ProtoDecoder options_decoder(field_descriptor.options().data(),
                                          field_descriptor.options().size());
  auto deprecated_field = options_decoder.FindField(3);
  return deprecated_field.valid() && deprecated_field.as_bool();
}

}  // namespace perfetto::protovm
