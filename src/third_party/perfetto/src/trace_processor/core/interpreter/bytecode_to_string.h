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

#ifndef SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_TO_STRING_H_
#define SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_TO_STRING_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/core/common/sort_types.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"

namespace perfetto::trace_processor::core::interpreter {

struct Bytecode;

// String conversion utilities for bytecode arguments.
base::StackString<32> ArgToString(uint32_t value);
base::StackString<64> ArgToString(const HandleBase& value);
base::StackString<64> ArgToString(NonNullOp value);
base::StackString<64> ArgToString(FilterValueHandle value);
base::StackString<64> ArgToString(BoundModifier bound);
base::StackString<64> ArgToString(SortDirection direction);
base::StackString<64> ArgToString(NullsLocation location);
void BytecodeFieldToString(std::string_view name,
                           const char* value,
                           std::vector<std::string>& fields);
std::string BytecodeFieldsFormat(const std::vector<std::string>& fields);

// Converts a bytecode instruction to string representation.
std::string ToString(const Bytecode& op);

}  // namespace perfetto::trace_processor::core::interpreter

#endif  // SRC_TRACE_PROCESSOR_CORE_INTERPRETER_BYTECODE_TO_STRING_H_
