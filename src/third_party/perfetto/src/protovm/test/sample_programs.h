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

#ifndef SRC_PROTOVM_TEST_SAMPLE_PROGRAMS_H_
#define SRC_PROTOVM_TEST_SAMPLE_PROGRAMS_H_

#include "protos/perfetto/protovm/vm_program.pb.h"

#include "src/protovm/test/protos/incremental_trace.pb.h"

namespace perfetto {
namespace protovm {
namespace test {

class SamplePrograms {
 public:
  static perfetto::protos::VmProgram NoInstructions() {
    perfetto::protos::VmProgram program;
    return program;
  }

  static perfetto::protos::VmProgram Select_AllCursorTypes() {
    perfetto::protos::VmProgram program;

    // default
    {
      auto* instruction = program.add_instructions();
      auto* select = instruction->mutable_select();
      auto* component = select->add_relative_path();
      component->set_field_id(1);
    }

    // SRC
    {
      auto* instruction = program.add_instructions();
      auto* select = instruction->mutable_select();
      select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_SRC);
      auto* component = select->add_relative_path();
      component->set_field_id(2);
    }

    // DST
    {
      auto* instruction = program.add_instructions();
      auto* select = instruction->mutable_select();
      select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
      auto* component = select->add_relative_path();
      component->set_field_id(3);
    }

    return program;
  }

  static perfetto::protos::VmProgram Select_AllFieldTypes() {
    perfetto::protos::VmProgram program;

    auto* instruction = program.add_instructions();

    auto* select = instruction->mutable_select();

    select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_SRC);

    // enter field
    {
      auto* component = select->add_relative_path();
      component->set_field_id(1);
    }

    // enter repeated field (array index)
    {
      auto* component_field_id = select->add_relative_path();
      component_field_id->set_field_id(2);

      auto* component_index = select->add_relative_path();
      component_index->set_array_index(1);
    }

    // enter mapped repeated field
    {
      auto* component_field_id = select->add_relative_path();
      component_field_id->set_field_id(4);

      auto* component_map_key = select->add_relative_path();
      component_map_key->set_map_key_field_id(5);
      component_map_key->set_register_to_match(0);
    }

    // iterate repeated field
    {
      auto* component = select->add_relative_path();
      component->set_field_id(3);
      component->set_is_repeated(true);
    }

    // nested instruction (reg_load)
    {
      auto* nested_instruction = instruction->add_nested_instructions();
      auto* reg_load = nested_instruction->mutable_reg_load();
      reg_load->set_dst_register(0);
    }

    return program;
  }

  static perfetto::protos::VmProgram Select_ExecutesNestedInstructions() {
    perfetto::protos::VmProgram program;

    auto* instruction = program.add_instructions();

    // iterate repeated field
    {
      auto* component = instruction->mutable_select()->add_relative_path();
      component->set_field_id(3);
      component->set_is_repeated(true);
    }

    // nested instruction #1 (reg_load)
    {
      auto* nested_instruction = instruction->add_nested_instructions();
      auto* reg_load = nested_instruction->mutable_reg_load();
      reg_load->set_dst_register(10);
    }

    // nested instruction #2 (reg_load)
    {
      auto* nested_instruction = instruction->add_nested_instructions();
      auto* reg_load = nested_instruction->mutable_reg_load();
      reg_load->set_dst_register(11);
    }

    return program;
  }

  static perfetto::protos::VmProgram Select_CanBreakOuterNestedInstructions() {
    perfetto::protos::VmProgram program;

    auto* instruction = program.add_instructions();

    instruction->mutable_reg_load()->set_dst_register(10);

    // nested instruction #1 (reg_load)
    {
      auto* nested_instruction = instruction->add_nested_instructions();
      auto* nested_reg_load = nested_instruction->mutable_reg_load();
      nested_reg_load->set_dst_register(10);
    }

    // nested instruction #2 (failing select)
    {
      auto* nested_instruction = instruction->add_nested_instructions();
      nested_instruction->set_abort_level(
          ::perfetto::protos::
              VmInstruction_AbortLevel_SKIP_CURRENT_INSTRUCTION_AND_BREAK_OUTER);
      auto* nested_select = nested_instruction->mutable_select();
      auto* component = nested_select->add_relative_path();
      component->set_field_id(1);
    }

    // nested instruction #2 (reg_load)
    {
      auto* nested_instruction = instruction->add_nested_instructions();
      auto* nested_reg_load = nested_instruction->mutable_reg_load();
      nested_reg_load->set_dst_register(11);
    }

    return program;
  }

  static perfetto::protos::VmProgram RegLoad() {
    perfetto::protos::VmProgram program;
    program.add_instructions()->mutable_reg_load()->set_dst_register(10);
    return program;
  }

  static perfetto::protos::VmProgram AbortLevel_default() {
    perfetto::protos::VmProgram program;
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(10);
    }
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(11);
    }
    return program;
  }

  static perfetto::protos::VmProgram AbortLevel_SKIP_CURRENT_INSTRUCTION() {
    perfetto::protos::VmProgram program;
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(10);
      instruction->set_abort_level(
          ::perfetto::protos::
              VmInstruction_AbortLevel_SKIP_CURRENT_INSTRUCTION);
    }
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(11);
    }
    return program;
  }

  static perfetto::protos::VmProgram
  AbortLevel_SKIP_CURRENT_INSTRUCTION_AND_BREAK_OUTER() {
    perfetto::protos::VmProgram program;
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(10);
      instruction->set_abort_level(
          ::perfetto::protos::
              VmInstruction_AbortLevel_SKIP_CURRENT_INSTRUCTION_AND_BREAK_OUTER);
    }
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(11);
    }
    return program;
  }

  static perfetto::protos::VmProgram AbortLevel_ABORT() {
    perfetto::protos::VmProgram program;
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(10);
      instruction->set_abort_level(
          ::perfetto::protos::VmInstruction_AbortLevel_ABORT);
    }
    {
      auto* instruction = program.add_instructions();
      instruction->mutable_reg_load()->set_dst_register(11);
    }
    return program;
  }

  static perfetto::protos::VmProgram Delete() {
    perfetto::protos::VmProgram program;
    program.add_instructions()->mutable_del();
    return program;
  }

  static perfetto::protos::VmProgram Merge() {
    perfetto::protos::VmProgram program;
    program.add_instructions()->mutable_merge();
    return program;
  }

  static perfetto::protos::VmProgram Merge_DelIfSrcEmpty() {
    perfetto::protos::VmProgram program;
    auto* instruction = program.add_instructions();
    auto* merge = instruction->mutable_merge();
    merge->set_del_if_src_empty(true);
    return program;
  }

  static perfetto::protos::VmProgram Set() {
    perfetto::protos::VmProgram program;
    program.add_instructions()->mutable_set();
    return program;
  }

  static perfetto::protos::VmProgram IncrementalTraceInstructions() {
    perfetto::protos::VmProgram program;

    constexpr uint32_t REGISTER_HOLDING_ELEMENT_ID = 0;

    // Process elements_to_delete
    {
      // select element to delete (src)
      auto* instr_src_select = program.add_instructions();
      auto* src_select = instr_src_select->mutable_select();
      auto* src_element = src_select->add_relative_path();
      src_element->set_field_id(protos::Patch::kElementsToDeleteFieldNumber);
      src_element->set_is_repeated(true);

      {
        // load element id to delete (src)
        auto* instr_reg_load = instr_src_select->add_nested_instructions();
        auto* reg_load = instr_reg_load->mutable_reg_load();
        reg_load->set_dst_register(REGISTER_HOLDING_ELEMENT_ID);

        // select element to delete (dst)
        auto* instr_dst_select = instr_src_select->add_nested_instructions();
        auto* dst_select = instr_dst_select->mutable_select();
        dst_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
        dst_select->set_create_if_not_exist(false);
        auto* component_elements = dst_select->add_relative_path();
        component_elements->set_field_id(
            protos::TraceEntry::kElementsFieldNumber);
        auto* component_map_key = dst_select->add_relative_path();
        component_map_key->set_map_key_field_id(
            protos::Element::kIdFieldNumber);
        component_map_key->set_register_to_match(REGISTER_HOLDING_ELEMENT_ID);

        // delete
        {
          auto* instr_del = instr_dst_select->add_nested_instructions();
          instr_del->mutable_del();
        }
      }
    }

    // process elements to merge
    {
      // select element to merge (src)
      auto* instr_src_select = program.add_instructions();
      auto* src_select = instr_src_select->mutable_select();
      auto* src_element = src_select->add_relative_path();
      src_element->set_field_id(protos::Patch::kElementsToMergeFieldNumber);
      src_element->set_is_repeated(true);

      {
        // select element id to merge (src)
        auto* instr_src_select_id = instr_src_select->add_nested_instructions();
        auto* src_select_id = instr_src_select_id->mutable_select();
        auto* src_id = src_select_id->add_relative_path();
        src_id->set_field_id(protos::Element::kIdFieldNumber);

        // load element id to merge (src)
        {
          auto* instr_reg_load = instr_src_select_id->add_nested_instructions();
          auto* reg_load = instr_reg_load->mutable_reg_load();
          reg_load->set_dst_register(REGISTER_HOLDING_ELEMENT_ID);
        }

        // select element to merge (dst)
        auto* instr_dst_select = instr_src_select->add_nested_instructions();
        auto* dst_select = instr_dst_select->mutable_select();
        dst_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
        dst_select->set_create_if_not_exist(true);
        auto* component_elements = dst_select->add_relative_path();
        component_elements->set_field_id(
            protos::TraceEntry::kElementsFieldNumber);
        auto* component_map_key = dst_select->add_relative_path();
        component_map_key->set_map_key_field_id(
            protos::Element::kIdFieldNumber);
        component_map_key->set_register_to_match(REGISTER_HOLDING_ELEMENT_ID);

        // merge
        {
          auto* instr_set = instr_dst_select->add_nested_instructions();
          instr_set->mutable_merge();
        }
      }
    }

    // process elements to set
    {
      // select element to set (src)
      auto* instr_src_select = program.add_instructions();
      auto* src_select = instr_src_select->mutable_select();
      auto* src_element = src_select->add_relative_path();
      src_element->set_field_id(protos::Patch::kElementsToSetFieldNumber);
      src_element->set_is_repeated(true);

      {
        // select element id to set (src)
        auto* instr_src_select_id = instr_src_select->add_nested_instructions();
        auto* src_select_id = instr_src_select_id->mutable_select();
        auto* src_id = src_select_id->add_relative_path();
        src_id->set_field_id(protos::Element::kIdFieldNumber);

        // load element id to set (src)
        {
          auto* instr_reg_load = instr_src_select_id->add_nested_instructions();
          auto* reg_load = instr_reg_load->mutable_reg_load();
          reg_load->set_dst_register(REGISTER_HOLDING_ELEMENT_ID);
        }

        // select element to set (dst)
        auto* instr_dst_select = instr_src_select->add_nested_instructions();
        auto* dst_select = instr_dst_select->mutable_select();
        dst_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
        dst_select->set_create_if_not_exist(true);
        auto* component_elements = dst_select->add_relative_path();
        component_elements->set_field_id(
            protos::TraceEntry::kElementsFieldNumber);
        auto* component_map_key = dst_select->add_relative_path();
        component_map_key->set_map_key_field_id(
            protos::Element::kIdFieldNumber);
        component_map_key->set_register_to_match(REGISTER_HOLDING_ELEMENT_ID);

        // set
        {
          auto* instr_set = instr_dst_select->add_nested_instructions();
          instr_set->mutable_set();
        }
      }
    }

    return program;
  }

  static perfetto::protos::VmProgram DelAliasedRoot() {
    perfetto::protos::VmProgram program;
    auto* outer = program.add_instructions();
    auto* outer_select = outer->mutable_select();
    outer_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
    outer->add_nested_instructions()->mutable_del();
    return program;
  }

  static perfetto::protos::VmProgram DelAliasedDstInsideSrcSelect() {
    // The outer select walks dst to a non-root child. The inner select uses
    // cursor=SRC with a non-empty relative_path, so dst remains pinned at
    // the outer's child while src walks elsewhere. A nested `del` targets
    // that pinned dst node. The parser must abort rather than restore a
    // stale dst pointer.
    perfetto::protos::VmProgram program;
    auto* outer = program.add_instructions();
    auto* outer_select = outer->mutable_select();
    outer_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
    outer_select->set_create_if_not_exist(true);
    outer_select->add_relative_path()->set_field_id(1);

    auto* inner = outer->add_nested_instructions();
    auto* inner_select = inner->mutable_select();
    inner_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_SRC);
    inner_select->add_relative_path()->set_field_id(
        protos::Patch::kElementsToSetFieldNumber);
    inner->add_nested_instructions()->mutable_del();
    return program;
  }

  static perfetto::protos::VmProgram DelAliasedDstInsideEmptyPathDstSelect() {
    // The outer select walks dst to a non-root child; the inner select has
    // an empty relative_path so its snapshot aliases that same child; a
    // nested `del` then targets the child. The parser must abort rather
    // than restore a stale dst pointer.
    perfetto::protos::VmProgram program;
    auto* outer = program.add_instructions();
    auto* outer_select = outer->mutable_select();
    outer_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
    outer_select->set_create_if_not_exist(true);
    outer_select->add_relative_path()->set_field_id(1);

    auto* inner = outer->add_nested_instructions();
    auto* inner_select = inner->mutable_select();
    inner_select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
    inner->add_nested_instructions()->mutable_del();

    outer->add_nested_instructions()->mutable_del();
    return program;
  }

  static perfetto::protos::VmProgram DelAndNestedDel() {
    perfetto::protos::VmProgram program;
    auto* outer = program.add_instructions();
    auto* select = outer->mutable_select();
    select->set_cursor(perfetto::protos::VmCursorEnum::VM_CURSOR_DST);
    select->set_create_if_not_exist(true);
    select->add_relative_path()->set_field_id(1);

    auto* outer_del = outer->add_nested_instructions();
    outer_del->mutable_del();

    auto* inner_del = outer_del->add_nested_instructions();
    inner_del->mutable_del();

    return program;
  }
};

}  // namespace test
}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_TEST_SAMPLE_PROGRAMS_H_
