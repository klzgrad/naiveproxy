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

#ifndef SRC_PROTOVM_PARSER_H_
#define SRC_PROTOVM_PARSER_H_

#include "perfetto/protozero/field.h"
#include "protos/perfetto/protovm/vm_program.pbzero.h"

#include "src/protovm/error_handling.h"
#include "src/protovm/executor.h"
#include "src/protovm/ro_cursor.h"
#include "src/protovm/rw_proto.h"

namespace perfetto {
namespace protovm {

class Parser {
 public:
  Parser(protozero::ConstBytes program, Executor* executor);
  StatusOr<void> Run(RoCursor src, RwProto::Cursor dst);

 private:
  StatusOr<void> ParseInstructions(
      protozero::RepeatedFieldIterator<protozero::ConstBytes> it_instruction);
  StatusOr<void> ParseInstruction(
      const protos::pbzero::VmInstruction::Decoder& instruction);
  StatusOr<void> ParseRegLoad(
      const protos::pbzero::VmInstruction::Decoder& instruction);
  StatusOr<void> ParseSelect(
      const protos::pbzero::VmInstruction::Decoder& instruction);
  StatusOr<void> ParseSelectRec(
      const protos::pbzero::VmInstruction::Decoder& instruction,
      protozero::RepeatedFieldIterator<protozero::ConstBytes>
          it_path_component);

  protos::pbzero::VmProgram::Decoder program_;
  Cursors cursors_;
  Executor* executor_;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_PARSER_H_
