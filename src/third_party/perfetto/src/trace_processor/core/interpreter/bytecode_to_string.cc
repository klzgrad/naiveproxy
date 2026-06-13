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

#include "src/trace_processor/core/interpreter/bytecode_to_string.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/core/common/sort_types.h"
#include "src/trace_processor/core/interpreter/bytecode_instructions.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"

namespace perfetto::trace_processor::core::interpreter {

base::StackString<32> ArgToString(uint32_t value) {
  return base::StackString<32>("%u", value);
}

base::StackString<64> ArgToString(const HandleBase& value) {
  return base::StackString<64>("Register(%u)", value.index);
}

base::StackString<64> ArgToString(NonNullOp value) {
  return base::StackString<64>("NonNullOp(%u)", value.index());
}

base::StackString<64> ArgToString(FilterValueHandle value) {
  return base::StackString<64>("FilterValue(%u)", value.index);
}

base::StackString<64> ArgToString(BoundModifier bound) {
  return base::StackString<64>("BoundModifier(%u)", bound.index());
}

base::StackString<64> ArgToString(SortDirection direction) {
  return base::StackString<64>("SortDirection(%u)",
                               static_cast<uint32_t>(direction));
}

base::StackString<64> ArgToString(NullsLocation location) {
  return base::StackString<64>("NullsLocation(%u)", location.index());
}

void BytecodeFieldToString(std::string_view name,
                           const char* value,
                           std::vector<std::string>& fields) {
  if (name.compare(0, 3, "pad") == 0) {
    return;
  }
  base::StackString<64> str("%.*s=%s", int(name.size()), name.data(), value);
  fields.push_back(str.ToStdString());
}

std::string BytecodeFieldsFormat(const std::vector<std::string>& fields) {
  std::string res;
  res.append("[");
  res.append(base::Join(fields, ", "));
  res.append("]");
  return res;
}

std::string ToString(const Bytecode& op) {
#define PERFETTO_DATAFRAME_BYTECODE_CASE_TO_STRING(...)            \
  case base::variant_index<BytecodeVariant, __VA_ARGS__>(): {      \
    __VA_ARGS__ typed_op;                                          \
    typed_op.option = op.option;                                   \
    typed_op.args_buffer = op.args_buffer;                         \
    return std::string(#__VA_ARGS__) + ": " + typed_op.ToString(); \
  }
  switch (op.option) {
    PERFETTO_DATAFRAME_BYTECODE_LIST(PERFETTO_DATAFRAME_BYTECODE_CASE_TO_STRING)
    default:
      PERFETTO_FATAL("Unknown opcode %u", op.option);
  }
#undef PERFETTO_DATAFRAME_BYTECODE_CASE_TO_STRING
}

}  // namespace perfetto::trace_processor::core::interpreter
