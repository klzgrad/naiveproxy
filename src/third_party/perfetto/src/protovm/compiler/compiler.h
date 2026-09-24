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

#ifndef SRC_PROTOVM_COMPILER_COMPILER_H_
#define SRC_PROTOVM_COMPILER_COMPILER_H_

#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "protos/perfetto/protovm/compile_config.pbzero.h"
#include "protos/perfetto/protovm/vm_program.pbzero.h"
#include "src/protovm/compiler/instruction_emitter.h"
#include "src/trace_processor/util/descriptors.h"

namespace perfetto::protovm {

// Implements the compiler front-end.
//
// Parses high-level configuration commands (CompileConfig) and delegates
// the conversion and emission of low-level bytecode instructions (VmProgram)
// to the InstructionEmitter.
class Compiler {
 public:
  Compiler();

  // input:  CompileConfig textproto (config_textproto)
  // output: VmProgram binary proto
  base::StatusOr<std::string> Compile(std::string_view config_textproto,
                                      std::string_view descriptor_bytes);

 private:
  using AbortLevel = protos::pbzero::VmInstruction::AbortLevel;
  using Cursor = perfetto::protos::pbzero::VmCursorEnum;

  base::Status ParseCommands(
      const InstructionEmitter::Scope& scope,
      protozero::RepeatedFieldIterator<protozero::ConstBytes> commands) const;

  base::Status ParseSet(
      const InstructionEmitter::Scope& scope,
      const protos::pbzero::CompileCommand::Decoder& command) const;

  base::Status ParseDel(
      const InstructionEmitter::Scope& scope,
      const protos::pbzero::CompileCommand::Decoder& command) const;

  base::Status ParseMerge(
      const InstructionEmitter::Scope& scope,
      const protos::pbzero::CompileCommand::Decoder& command) const;

  base::Status ParseEnterScope(
      const InstructionEmitter::Scope& scope,
      const protos::pbzero::CompileCommand::Decoder& command) const;

  template <typename Iterator>
  std::vector<std::string_view> ParsePath(Iterator it) const {
    std::vector<std::string_view> path;
    for (; it; ++it) {
      path.push_back(std::string_view(reinterpret_cast<const char*>(it->data()),
                                      it->size()));
    }
    return path;
  }

  AbortLevel GetAbortLevel(
      const protos::pbzero::CompileCommand::Decoder& command) const;

  perfetto::trace_processor::DescriptorPool pool_;
  InstructionEmitter emitter_;
};

}  // namespace perfetto::protovm

#endif  // SRC_PROTOVM_COMPILER_COMPILER_H_
