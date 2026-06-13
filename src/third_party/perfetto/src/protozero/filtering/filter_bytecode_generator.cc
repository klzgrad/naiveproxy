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

#include "src/protozero/filtering/filter_bytecode_generator.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "src/protozero/filtering/filter_bytecode_common.h"

namespace protozero {

FilterBytecodeGenerator::FilterBytecodeGenerator(BytecodeVersion min_version)
    : min_version_(min_version) {}
FilterBytecodeGenerator::~FilterBytecodeGenerator() = default;

void FilterBytecodeGenerator::EndMessage() {
  endmessage_called_ = true;
  bytecode_.push_back(kFilterOpcode_EndOfMessage);
  last_field_id_ = 0;
  ++num_messages_;
}

// Allows a simple field (varint, fixed32/64, string or bytes).
void FilterBytecodeGenerator::AddSimpleField(uint32_t field_id) {
  PERFETTO_CHECK(field_id > last_field_id_);
  bytecode_.push_back(field_id << kOpcodeShift | kFilterOpcode_SimpleField);
  last_field_id_ = field_id;
  endmessage_called_ = false;
}

// Allows a string field which needs to be filtered. Optionally specifies a
// semantic type (0 = none).
void FilterBytecodeGenerator::AddFilterStringField(uint32_t field_id,
                                                   uint32_t semantic_type,
                                                   bool allow_in_v1,
                                                   bool allow_in_v2) {
  // String filtering is only available in bytecode v2+.
  if (min_version_ < BytecodeVersion::kV2) {
    if (allow_in_v1) {
      // If allowed in v1, just add as a simple field.
      AddSimpleField(field_id);
    }
    // Otherwise just deny the field by doing nothing.
    return;
  }

  PERFETTO_CHECK(field_id > last_field_id_);

  // No semantic type: just use the basic filter string opcode.
  if (semantic_type == 0) {
    bytecode_.push_back(field_id << kOpcodeShift | kFilterOpcode_FilterString);
    last_field_id_ = field_id;
    endmessage_called_ = false;
    return;
  }

  // Has semantic type: use v54 opcode or overlay.
  // If min_version is at least v54, we can just use the new opcode directly.
  if (min_version_ >= BytecodeVersion::kV54) {
    bytecode_.push_back(field_id << kOpcodeShift |
                        kFilterOpcode_FilterStringWithType);
    bytecode_.push_back(semantic_type);
  } else {
    // On v2 bytecode, if allow_in_v2 is true, emit the field as a simple
    // filter string (without semantic type). On v54 it will be enhanced via
    // overlay.
    if (allow_in_v2) {
      bytecode_.push_back(field_id << kOpcodeShift |
                          kFilterOpcode_FilterString);
    }
    // Otherwise the field will just be totally denied in v2.

    // On v54 it will be allowed with semantic type.
    v54_overlay_.push_back(num_messages_);
    v54_overlay_.push_back(field_id << kOpcodeShift |
                           kFilterOpcode_FilterStringWithType);
    v54_overlay_.push_back(semantic_type);
  }

  last_field_id_ = field_id;
  endmessage_called_ = false;
}

// Allows a range of simple fields. |range_start| is the id of the first field
// in range, |range_len| the number of fields in the range.
// AddSimpleFieldRange(N,1) is semantically equivalent to AddSimpleField(N).
void FilterBytecodeGenerator::AddSimpleFieldRange(uint32_t range_start,
                                                  uint32_t range_len) {
  PERFETTO_CHECK(range_start > last_field_id_);
  PERFETTO_CHECK(range_len > 0);
  bytecode_.push_back(range_start << kOpcodeShift |
                      kFilterOpcode_SimpleFieldRange);
  bytecode_.push_back(range_len);
  last_field_id_ = range_start + range_len - 1;
  endmessage_called_ = false;
}

// Adds a nested field. |message_index| is the index of the message that the
// parser must recurse into. This implies that at least |message_index| + 1
// calls to EndMessage() will be made.
// The Serialize() method will fail if any field points to an out of range
// index.
void FilterBytecodeGenerator::AddNestedField(uint32_t field_id,
                                             uint32_t message_index) {
  PERFETTO_CHECK(field_id > last_field_id_);
  bytecode_.push_back(field_id << kOpcodeShift | kFilterOpcode_NestedField);
  bytecode_.push_back(message_index);
  last_field_id_ = field_id;
  max_msg_index_ = std::max(max_msg_index_, message_index);
  endmessage_called_ = false;
}

// Returns the bytes that can be used into TraceConfig.trace_filter.bytecode.
// The returned bytecode is a binary buffer which consists of a sequence of
// varints (the opcodes) and a checksum.
// The returned string can be passed as-is to FilterBytecodeParser.Load().
FilterBytecodeGenerator::SerializeResult FilterBytecodeGenerator::Serialize() {
  PERFETTO_CHECK(endmessage_called_);
  PERFETTO_CHECK(max_msg_index_ < num_messages_);

  SerializeResult result;

  // Serialize main bytecode.
  {
    protozero::PackedVarInt words;
    perfetto::base::FnvHasher hasher;
    for (uint32_t word : bytecode_) {
      words.Append(word);
      hasher.Update(word);
    }
    words.Append(static_cast<uint32_t>(hasher.digest()));
    result.bytecode =
        std::string(reinterpret_cast<const char*>(words.data()), words.size());
  }

  // Serialize v54 overlay if non-empty.
  if (!v54_overlay_.empty()) {
    protozero::PackedVarInt words;
    perfetto::base::FnvHasher hasher;
    for (uint32_t word : v54_overlay_) {
      words.Append(word);
      hasher.Update(word);
    }
    words.Append(static_cast<uint32_t>(hasher.digest()));
    result.v54_overlay =
        std::string(reinterpret_cast<const char*>(words.data()), words.size());
  }
  return result;
}

}  // namespace protozero
