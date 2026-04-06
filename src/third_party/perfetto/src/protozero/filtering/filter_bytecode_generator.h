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

#ifndef SRC_PROTOZERO_FILTERING_FILTER_BYTECODE_GENERATOR_H_
#define SRC_PROTOZERO_FILTERING_FILTER_BYTECODE_GENERATOR_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace protozero {

// Creates a filter bytecode that can be passed to the FilterBytecodeParser.
// This class is typically only used by offline tools (e.g. the proto_filter
// cmdline tool). See go/trace-filtering for the full filtering design.
class FilterBytecodeGenerator {
 public:
  // Result of Serialize(). Contains the main bytecode and an optional overlay.
  // The v54_overlay contains entries for opcodes that were rewritten to be
  // backwards-compatible in the main bytecode. Newer parsers can apply the
  // overlay to get the full functionality.
  struct SerializeResult {
    std::string bytecode;
    std::string v54_overlay;
  };

  // NOTE: When adding new versions, also update the default value of
  // --min-bytecode-parser in src/tools/proto_filter/proto_filter.cc.
  enum class BytecodeVersion : uint8_t {
    // Initial version. Supported proto structural opcodes only, no string
    // filtering.
    kV1 = 0,
    // Added string filtering opcodes, with no semantic type support.
    kV2 = 1,
    // Added string filtering with semantic type support.
    kV54 = 2,

    // Alias for the latest version.
    kLatest = kV54,
  };

  // Constructs a FilterBytecodeGenerator.
  //
  // The generator will produce bytecode that is compatible with parsers of at
  // least |min_version|.
  explicit FilterBytecodeGenerator(
      BytecodeVersion min_version = BytecodeVersion::kLatest);
  ~FilterBytecodeGenerator();

  // Call at the end of every message. It implicitly starts a new message, there
  // is no corresponding BeginMessage().
  void EndMessage();

  // All the methods below must be called in monotonic field_id order or the
  // generator will CHECK() and crash.

  // Allows a simple field (varint, fixed32/64, string or bytes).
  void AddSimpleField(uint32_t field_id);

  // Allows a string field which needs to be filtered. Optionally specifies a
  // semantic type (0 = none) that tells the filter what kind of data the field
  // contains.
  // If `allow_in_v1` is true, the field is added to v1 bytecode as a simple
  // field (string filtering not available in v1).
  // If `allow_in_v2` is true and semantic_type != 0, the field is added to v2
  // bytecode (without type) in addition to the v54 overlay (with type).
  void AddFilterStringField(uint32_t field_id,
                            uint32_t semantic_type,
                            bool allow_in_v1,
                            bool allow_in_v2);

  // Allows a range of simple fields. |range_start| is the id of the first field
  // in range, |range_len| the number of fields in the range.
  // AddSimpleFieldRange(N,1) is semantically equivalent to AddSimpleField(N)
  // (but it takes 2 words to encode, rather than just one).
  void AddSimpleFieldRange(uint32_t range_start, uint32_t range_len);

  // Adds a nested field. |message_index| is the index of the message that the
  // parser must recurse into. This implies that at least |message_index| calls
  // to Begin/EndMessage will be made.
  // The Serialize() method will fail if any field points to an index that is
  // out of range (e.g., if message_index = 5 but only 3 EndMessage() calls were
  // made).
  void AddNestedField(uint32_t field_id, uint32_t message_index);

  // Returns the filter bytecode and overlay. The bytecode is a buffer
  // containing a sequence of varints and a checksum. The returned bytecode
  // can be passed to FilterBytecodeParser.Load().
  // The v54_overlay may be empty if no backwards-incompatible opcodes were
  // used. If non-empty, it should be passed alongside the bytecode to Load().
  SerializeResult Serialize();

 private:
  uint32_t num_messages_ = 0;
  uint32_t last_field_id_ = 0;
  uint32_t max_msg_index_ = 0;
  bool endmessage_called_ = false;
  BytecodeVersion min_version_ = BytecodeVersion::kLatest;

  std::vector<uint32_t> bytecode_;

  // Overlay entries for v54. Each entry is [msg_index, field_word, argument]
  // where field_word = (field_id << 3) | opcode. The overlay contains entries
  // for opcodes that were rewritten to be backwards-compatible in the main
  // bytecode. Sorted by (msg_index, field_id).
  std::vector<uint32_t> v54_overlay_;
};

}  // namespace protozero

#endif  // SRC_PROTOZERO_FILTERING_FILTER_BYTECODE_GENERATOR_H_
