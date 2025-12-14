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

#include "src/protovm/parser.h"

namespace perfetto {
namespace protovm {

Parser::Parser(protozero::ConstBytes program, Executor* executor)
    : program_{program}, executor_(executor) {}

StatusOr<void> Parser::Run(RoCursor src, RwProto::Cursor dst) {
  cursors_.src = src;
  cursors_.dst = dst;
  return ParseInstructions(program_.instructions());
}

StatusOr<void> Parser::ParseInstructions(
    protozero::RepeatedFieldIterator<protozero::ConstBytes> it_instruction) {
  int instruction_index = 0;
  for (; it_instruction; ++it_instruction) {
    auto instruction = protos::pbzero::VmInstruction::Decoder(*it_instruction);
    auto status = ParseInstruction(instruction);

    if (status.IsAbort()) {
      PROTOVM_RETURN(status, "instruction[%d]", instruction_index);
    }

    if (status.IsError()) {
      using AbortLevel = perfetto::protos::pbzero::VmInstruction::AbortLevel;
      auto abort_level =
          instruction.has_abort_level()
              ? static_cast<AbortLevel>(instruction.abort_level())
              : AbortLevel::SKIP_CURRENT_INSTRUCTION_AND_BREAK_OUTER;
      if (abort_level == AbortLevel::SKIP_CURRENT_INSTRUCTION_AND_BREAK_OUTER) {
        break;
      }
      if (abort_level == AbortLevel::ABORT) {
        PROTOVM_ABORT(
            "instruction[%d]: returned status = error and instruction's "
            "abort level = 'abort'",
            instruction_index);
      }
    }

    ++instruction_index;
  }

  return StatusOr<void>::Ok();
}

StatusOr<void> Parser::ParseInstruction(
    const protos::pbzero::VmInstruction::Decoder& instruction) {
  if (instruction.has_select()) {
    auto status = ParseSelect(instruction);
    PROTOVM_RETURN(status, "select");
  }

  if (instruction.has_reg_load()) {
    auto status = ParseRegLoad(instruction);
    PROTOVM_RETURN_IF_NOT_OK(status, "reg_load");
  } else if (instruction.has_del()) {
    auto status = executor_->Delete(&cursors_.dst);
    PROTOVM_RETURN_IF_NOT_OK(status, "del");
  } else if (instruction.has_merge()) {
    auto status = executor_->Merge(&cursors_);
    PROTOVM_RETURN_IF_NOT_OK(status, "merge");
  } else if (instruction.has_set()) {
    auto status = executor_->Set(&cursors_);
    PROTOVM_RETURN_IF_NOT_OK(status, "set");
  } else {
    PROTOVM_ABORT("Unsupported instruction");
  }

  return ParseInstructions(instruction.nested_instructions());
}

StatusOr<void> Parser::ParseRegLoad(
    const protos::pbzero::VmInstruction::Decoder& instruction) {
  auto saved_selected = cursors_.selected;

  protos::pbzero::VmOpRegLoad::Decoder reg_load(instruction.reg_load());
  cursors_.selected = !reg_load.has_cursor()
                          ? CursorEnum::VM_CURSOR_SRC
                          : static_cast<CursorEnum>(reg_load.cursor());
  auto status = executor_->WriteRegister(
      cursors_, static_cast<uint8_t>(reg_load.dst_register()));

  cursors_.selected = saved_selected;

  PROTOVM_RETURN_IF_NOT_OK(status);

  return status;
}

StatusOr<void> Parser::ParseSelect(
    const protos::pbzero::VmInstruction::Decoder& instruction) {
  auto saved_cursors = cursors_;

  protos::pbzero::VmOpSelect::Decoder select(instruction.select());
  cursors_.selected = !select.has_cursor()
                          ? CursorEnum::VM_CURSOR_SRC
                          : static_cast<CursorEnum>(select.cursor());
  cursors_.create_if_not_exist = select.create_if_not_exist();

  if (cursors_.selected == CursorEnum::VM_CURSOR_SRC &&
      cursors_.create_if_not_exist) {
    PROTOVM_ABORT(
        "incompatible params: src cursor (read only) + create_if_not_exist");
  }

  auto status = ParseSelectRec(instruction, select.relative_path());
  cursors_ = saved_cursors;

  return status;
}

StatusOr<void> Parser::ParseSelectRec(
    const protos::pbzero::VmInstruction::Decoder& instruction,
    protozero::RepeatedFieldIterator<protozero::ConstBytes> it_path_component) {
  protos::pbzero::VmOpSelect::Decoder select(instruction.select());

  bool has_entered_all_path_components = !it_path_component;
  if (has_entered_all_path_components) {
    return ParseInstructions(instruction.nested_instructions());
  }

  auto curr_component =
      protos::pbzero::VmOpSelect::PathComponent::Decoder(*it_path_component);
  ++it_path_component;

  std::optional<protos::pbzero::VmOpSelect::PathComponent::Decoder>
      next_component =
          it_path_component
              ? std::make_optional(
                    protos::pbzero::VmOpSelect::PathComponent::Decoder(
                        *it_path_component))
              : std::nullopt;

  if (!curr_component.has_field_id()) {
    PROTOVM_ABORT("Invalid path. Expected path component with field_id.");
  }

  // iterate repeated field
  if (curr_component.is_repeated()) {
    if (cursors_.selected == CursorEnum::VM_CURSOR_SRC) {
      auto status_or_it = executor_->IterateRepeatedField(
          &cursors_.src, curr_component.field_id());
      PROTOVM_RETURN_IF_NOT_OK(status_or_it, "iterate repeated field (id = %d)",
                               static_cast<int>(curr_component.field_id()));
      int index = 0;
      for (auto it = *status_or_it; it; ++it) {
        cursors_.src = *it;
        auto status = ParseSelectRec(instruction, it_path_component);
        PROTOVM_RETURN_IF_NOT_OK(status, "repeated field (id = %d, index = %d)",
                                 static_cast<int>(curr_component.field_id()),
                                 index);
        ++index;
      }
    } else if (cursors_.selected == CursorEnum::VM_CURSOR_DST) {
      auto status_or_it = executor_->IterateRepeatedField(
          &cursors_.dst, curr_component.field_id());
      PROTOVM_RETURN_IF_NOT_OK(status_or_it, "iterate repeated field (id = %d)",
                               static_cast<int>(curr_component.field_id()));
      int index = 0;
      for (auto it = *status_or_it; it; ++it) {
        cursors_.dst = it.GetCursor();
        auto status = ParseSelectRec(instruction, it_path_component);
        PROTOVM_RETURN_IF_NOT_OK(status, "repeated field (id = %d, index = %d)",
                                 static_cast<int>(curr_component.field_id()),
                                 index);
        ++index;
      }
    } else {
      PROTOVM_ABORT(
          "Iteration over selected cursor (%d) is not supported. Should be "
          "either SRC or DST cursor.",
          static_cast<int>(cursors_.selected));
    }
    return StatusOr<void>::Ok();
  }

  // enter indexed repeated field
  if (next_component && next_component->has_array_index()) {
    ++it_path_component;
    auto status_enter = executor_->EnterRepeatedFieldAt(
        &cursors_, curr_component.field_id(), next_component->array_index());
    PROTOVM_RETURN_IF_NOT_OK(status_enter, "enter indexed repeated field");
    auto status_select = ParseSelectRec(instruction, it_path_component);
    PROTOVM_RETURN(status_select, "repeated field (id = %d, index = %d)",
                   static_cast<int>(curr_component.field_id()),
                   static_cast<int>(next_component->array_index()));
  }

  // enter mapped repeated field
  if (next_component && next_component->has_map_key_field_id()) {
    ++it_path_component;
    if (!next_component->has_register_to_match()) {
      PROTOVM_ABORT(
          "enter mapped repeated field: expected field 'register_to_match'");
    }
    auto reg_id = static_cast<uint8_t>(next_component->register_to_match());
    auto key = executor_->ReadRegister(reg_id);
    PROTOVM_RETURN_IF_NOT_OK(key, "enter mapped repeated field");
    auto status_enter = executor_->EnterRepeatedFieldByKey(
        &cursors_, curr_component.field_id(),
        next_component->map_key_field_id(), static_cast<uint32_t>(*key));
    PROTOVM_RETURN_IF_NOT_OK(status_enter, "enter mapped repeated field");
    auto status_select = ParseSelectRec(instruction, it_path_component);
    PROTOVM_RETURN(status_select, "mapped repeated field (id = %d, key = %d)",
                   static_cast<int>(curr_component.field_id()),
                   static_cast<int>(*key));
  }

  // enter field
  auto status = executor_->EnterField(&cursors_, curr_component.field_id());
  PROTOVM_RETURN_IF_NOT_OK(status, "enter field (id = %d)",
                           static_cast<int>(curr_component.field_id()));

  return ParseSelectRec(instruction, it_path_component);
}

}  // namespace protovm
}  // namespace perfetto
