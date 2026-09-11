/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef SRC_PROTOZERO_FILTERING_FILTER_BYTECODE_COMMON_H_
#define SRC_PROTOZERO_FILTERING_FILTER_BYTECODE_COMMON_H_

#include <stdint.h>

namespace protozero {

enum FilterOpcode : uint32_t {
  // The immediate value is 0 in this case.
  kFilterOpcode_EndOfMessage = 0,

  // The immediate value is the id of the allowed field.
  kFilterOpcode_SimpleField = 1,

  // The immediate value is the start of the range. The next word (without
  // any shifting) is the length of the range.
  kFilterOpcode_SimpleFieldRange = 2,

  // The immediate value is the id of the allowed field. The next word
  // (without any shifting) is the index of the filter that should be used to
  // recurse into the nested message.
  kFilterOpcode_NestedField = 3,

  // The immediate value is the id of the allowed field. The behaviour of this
  // opcode is the same as kFilterOpcode_SimpleField, with the further semantic
  // that the field is a string and needs to be processed using the string
  // filtering rules.
  kFilterOpcode_FilterString = 4,

  // The immediate value is the id of the allowed field. The next word (without
  // any shifting) is the semantic type of the string field. The behaviour is
  // similar to kFilterOpcode_FilterString, but carries semantic type info so
  // that the string filter can apply type-specific rules.
  kFilterOpcode_FilterStringWithType = 5,
};

// Constants for extracting opcode and field_id from bytecode words.
// Bytecode words are encoded as: (field_id << kOpcodeShift) | opcode
constexpr uint32_t kOpcodeMask = 0x7u;
constexpr uint32_t kOpcodeShift = 3;

}  // namespace protozero

#endif  // SRC_PROTOZERO_FILTERING_FILTER_BYTECODE_COMMON_H_
