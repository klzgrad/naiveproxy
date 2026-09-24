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

#include "src/protozero/filtering/filter_bytecode_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/public/compiler.h"
#include "src/protozero/filtering/filter_bytecode_common.h"

namespace protozero {

namespace {

// Parses varint-encoded bytecode and verifies checksum.
// Returns true on success, with the checksum removed from |words|.
bool ParseAndVerifyChecksum(const uint8_t* data,
                            size_t len,
                            std::vector<uint32_t>* words,
                            bool suppress_logs) {
  if (len == 0) {
    return false;
  }
  words->reserve(len);  // An overestimation, but avoids reallocations.

  // Parse varints while computing checksum in a single pass.
  // The last word is the checksum itself and should not be hashed.
  perfetto::base::FnvHasher hasher;
  uint32_t actual_csum = 0;

  using BytecodeDecoder =
      PackedRepeatedFieldIterator<proto_utils::ProtoWireType::kVarInt,
                                  uint32_t>;
  bool has_checksum = false;
  bool packed_parse_err = false;
  for (BytecodeDecoder it(data, len, &packed_parse_err); it;) {
    uint32_t word = *it++;
    if (bool is_last_word = !it; is_last_word) {
      actual_csum = word;
      has_checksum = true;
      break;
    }
    words->emplace_back(word);
    hasher.Update(word);
  }
  if (packed_parse_err || !has_checksum) {
    words->clear();
    return false;
  }
  auto expected_csum = static_cast<uint32_t>(hasher.digest());
  if (expected_csum != actual_csum) {
    if (!suppress_logs) {
      PERFETTO_ELOG("Filter bytecode checksum failed. Expected: %x, actual: %x",
                    expected_csum, actual_csum);
    }
    words->clear();
    return false;
  }
  return true;
}

struct OverlayEntry {
  uint32_t msg_index;
  uint32_t field_id;
  uint32_t message_id;
};

// Returns the message_id for an overlay entry based on opcode and argument.
// Returns 0 if the opcode is invalid.
uint32_t GetMessageIdForOverlay(uint32_t opcode, uint32_t argument) {
  switch (opcode) {
    case kFilterOpcode_SimpleField:
      return FilterBytecodeParser::kSimpleField;
    case kFilterOpcode_FilterString:
      return FilterBytecodeParser::kFilterStringField;
    case kFilterOpcode_FilterStringWithType:
      // For FilterStringWithType, incorporate semantic type into message_id.
      return FilterBytecodeParser::kFilterStringFieldWithType |
             (argument & FilterBytecodeParser::kSemanticTypeMask);
    default:
      return 0;  // Invalid opcode
  }
}

}  // namespace

void FilterBytecodeParser::Reset() {
  bool suppress = suppress_logs_for_fuzzer_;
  *this = FilterBytecodeParser();
  suppress_logs_for_fuzzer_ = suppress;
}

bool FilterBytecodeParser::Load(const void* filter_data,
                                size_t len,
                                const void* overlay_data,
                                size_t overlay_len) {
  Reset();
  bool res =
      LoadInternal(static_cast<const uint8_t*>(filter_data), len,
                   static_cast<const uint8_t*>(overlay_data), overlay_len);
  // If load fails, don't leave the parser in a half broken state.
  if (!res)
    Reset();
  return res;
}

bool FilterBytecodeParser::LoadInternal(const uint8_t* filter_data,
                                        size_t len,
                                        const uint8_t* overlay_data,
                                        size_t overlay_len) {
  // First unpack the varints into a plain uint32 vector, so it's easy to
  // iterate through them and look ahead.
  std::vector<uint32_t> words;
  if (!ParseAndVerifyChecksum(filter_data, len, &words,
                              suppress_logs_for_fuzzer_))
    return false;

  // Parse the overlay (if provided).
  std::vector<OverlayEntry> overlay;
  if (overlay_data && overlay_len > 0) {
    std::vector<uint32_t> overlay_words;
    if (!ParseAndVerifyChecksum(overlay_data, overlay_len, &overlay_words,
                                suppress_logs_for_fuzzer_)) {
      return false;
    }

    // Each entry is exactly 3 words: [msg_index, field_word, argument]
    // where field_id = field_word >> 3. The argument is 0 when not needed.
    if (overlay_words.size() % 3 != 0) {
      PERFETTO_DLOG("overlay error: size %zu not multiple of 3",
                    overlay_words.size());
      return false;
    }

    for (size_t i = 0; i < overlay_words.size(); i += 3) {
      overlay.emplace_back();

      uint32_t opcode = overlay_words[i + 1] & kOpcodeMask;
      uint32_t message_id =
          GetMessageIdForOverlay(opcode, overlay_words[i + 2]);
      if (message_id == 0) {
        PERFETTO_DLOG("overlay error: invalid opcode %u at index %zu", opcode,
                      i + 1);
        return false;
      }

      OverlayEntry& entry = overlay.back();
      entry.msg_index = overlay_words[i];
      entry.field_id = overlay_words[i + 1] >> kOpcodeShift;
      entry.message_id = message_id;

      if (overlay.size() == 1) {
        continue;
      }
      // Validate that overlay entries are sorted by (msg_index, field_id).
      const OverlayEntry& prev_entry = overlay[overlay.size() - 2];
      if (entry.msg_index < prev_entry.msg_index ||
          (entry.msg_index == prev_entry.msg_index &&
           entry.field_id <= prev_entry.field_id)) {
        PERFETTO_DLOG(
            "overlay error: entries not sorted at index %zu (msg %u, "
            "field %u) after (msg %u, field %u)",
            i, entry.msg_index, entry.field_id, prev_entry.msg_index,
            prev_entry.field_id);
        return false;
      }
    }
  }

  // Temporary storage for each message. Cleared on every END_OF_MESSAGE.
  std::vector<uint32_t> direct_indexed_fields;
  std::vector<uint32_t> ranges;
  uint32_t max_msg_index = 0;
  uint32_t current_msg_index = 0;
  size_t overlay_idx = 0;

  auto add_directly_indexed_field = [&](uint32_t field_id, uint32_t msg_id) {
    PERFETTO_DCHECK(field_id > 0 && field_id < kDirectlyIndexLimit);
    direct_indexed_fields.resize(std::max(direct_indexed_fields.size(),
                                          static_cast<size_t>(field_id) + 1));
    direct_indexed_fields[field_id] = kAllowed | msg_id;
  };

  auto add_range = [&](uint32_t id_start, uint32_t id_end, uint32_t msg_id) {
    PERFETTO_DCHECK(id_end > id_start);
    PERFETTO_DCHECK(id_start >= kDirectlyIndexLimit);
    ranges.emplace_back(id_start);
    ranges.emplace_back(id_end);
    ranges.emplace_back(kAllowed | msg_id);
  };

  // Merges overlay entries into the current message being built.
  //
  // This function processes overlay entries for the current message up to (and
  // including) the given |field_id|. Since both base bytecode and overlay are
  // sorted by (msg_index, field_id), we can use a two-pointer merge approach:
  // - Entries with field_id < the given field_id are ADDed as new fields
  // - An entry with field_id == the given field_id is an UPGRADE (returned)
  // - Entries with field_id > the given field_id are left for later
  //
  // Pass std::numeric_limits<uint32_t>::max() as field_id to drain all
  // remaining entries for the current message (called at EndOfMessage).
  //
  // Returns:
  // - The msg_id to use if there's an exact match (upgrade case)
  // - 0 if no match (use the base bytecode's msg_id)
  // - std::numeric_limits<uint32_t>::max() on error
  constexpr uint32_t kOverlayError = std::numeric_limits<uint32_t>::max();
  auto process_overlay = [&](uint32_t field_id) -> uint32_t {
    while (overlay_idx < overlay.size()) {
      const OverlayEntry& entry = overlay[overlay_idx];

      // Stop if this entry is for a later message or a later field.
      if (entry.msg_index > current_msg_index ||
          (entry.msg_index == current_msg_index && entry.field_id > field_id)) {
        break;
      }

      // Message indexes are dense and we verified above that we are sorted
      // so this must be for the current message.
      PERFETTO_DCHECK(entry.msg_index == current_msg_index);

      // Exact match - this is an upgrade. Return the msg_id for the caller.
      if (entry.field_id == field_id) {
        ++overlay_idx;
        return entry.message_id;
      }

      // entry_field < field_id: This is a new field to ADD.
      if (entry.field_id < kDirectlyIndexLimit) {
        add_directly_indexed_field(entry.field_id, entry.message_id);
      } else {
        add_range(entry.field_id, entry.field_id + 1, entry.message_id);
      }
      ++overlay_idx;
    }
    // No match found.
    return 0;
  };

  bool is_eom = true;
  for (size_t i = 0; i < words.size(); ++i) {
    const uint32_t word = words[i];
    const bool has_next_word = i < words.size() - 1;
    const uint32_t opcode = word & kOpcodeMask;
    const uint32_t field_id = word >> kOpcodeShift;

    is_eom = opcode == kFilterOpcode_EndOfMessage;
    if (field_id == 0 && opcode != kFilterOpcode_EndOfMessage) {
      PERFETTO_DLOG("bytecode error @ word %zu, invalid field id (0)", i);
      return false;
    }

    if (opcode == kFilterOpcode_SimpleField ||
        opcode == kFilterOpcode_NestedField ||
        opcode == kFilterOpcode_FilterString ||
        opcode == kFilterOpcode_FilterStringWithType) {
      // Field words are organized as follow:
      // MSB: 1 if allowed, 0 if not allowed.
      // Remaining bits:
      //   Message index in the case of nested (non-simple) messages.
      //   0x7fff0000-0x7ffffffe for string fields with semantic type.
      //   0x7ffffffe in the case of string fields which need filtering.
      //   0x7fffffff in the case of simple fields.
      uint32_t msg_id;
      if (opcode == kFilterOpcode_SimpleField) {
        msg_id = kSimpleField;
      } else if (opcode == kFilterOpcode_FilterString) {
        msg_id = kFilterStringField;
      } else if (opcode == kFilterOpcode_FilterStringWithType) {
        // The next word in the bytecode contains the semantic type.
        if (!has_next_word) {
          PERFETTO_DLOG(
              "bytecode error @ word %zu: unterminated filter string with type",
              i);
          return false;
        }
        uint32_t semantic_type = words[++i];
        msg_id =
            kFilterStringFieldWithType | (semantic_type & kSemanticTypeMask);
      } else {  // FILTER_OPCODE_NESTED_FIELD
        // The next word in the bytecode contains the message index.
        if (!has_next_word) {
          PERFETTO_DLOG("bytecode error @ word %zu: unterminated nested field",
                        i);
          return false;
        }
        msg_id = words[++i];
        max_msg_index = std::max(max_msg_index, msg_id);
      }

      // Process overlay: add any fields that come before this one, and check
      // if this field should be upgraded.
      uint32_t overlay_msg_id = process_overlay(field_id);
      if (overlay_msg_id == kOverlayError) {
        return false;
      }
      if (overlay_msg_id != 0) {
        msg_id = overlay_msg_id;
      }

      if (field_id < kDirectlyIndexLimit) {
        add_directly_indexed_field(field_id, msg_id);
      } else {
        // In the case of a large field id (rare) we waste an extra word and
        // represent it as a range. Doesn't make sense to introduce extra
        // complexity to deal with rare cases like this.
        add_range(field_id, field_id + 1, msg_id);
      }
    } else if (opcode == kFilterOpcode_SimpleFieldRange) {
      if (!has_next_word) {
        PERFETTO_DLOG("bytecode error @ word %zu: unterminated range", i);
        return false;
      }
      const uint32_t range_len = words[++i];
      const uint32_t range_end = field_id + range_len;  // STL-style, excl.
      uint32_t id = field_id;

      // Here's the subtle complexity: at the bytecode level, we don't know
      // anything about the kDirectlyIndexLimit. It is legit to define a range
      // that spans across the direct-indexing threshold (e.g. 126-132). In that
      // case we want to add all the elements < the indexing to the O(1) bucket
      // and add only the remaining range as a non-indexed range.
      for (; id < range_end && id < kDirectlyIndexLimit; ++id)
        add_directly_indexed_field(id, kAllowed | kSimpleField);
      PERFETTO_DCHECK(id >= kDirectlyIndexLimit || id == range_end);
      if (id < range_end)
        add_range(id, range_end, kSimpleField);
    } else if (opcode == kFilterOpcode_EndOfMessage) {
      // Drain any remaining overlay entries for this message.
      if (process_overlay(std::numeric_limits<uint32_t>::max()) ==
          kOverlayError) {
        return false;
      }

      // Verify that the ranges are non-overlapping. Assumes that the ranges
      // are sorted (they are, because the bytecode is sorted).
      for (size_t r = 0; r + 3 < ranges.size(); r += 3) {
        const uint32_t prev_range_end = ranges[r + 1];
        const uint32_t curr_range_start = ranges[r + 3];
        if (curr_range_start < prev_range_end) {
          PERFETTO_DLOG(
              "bytecode error @ message %u: overlapping ranges [%u, %u) "
              "and [%u, ...)",
              current_msg_index, ranges[r], prev_range_end, curr_range_start);
          return false;
        }
      }

      // For each message append:
      // 1. The "header" word telling how many directly indexed fields there
      //    are.
      // 2. The words for the directly indexed fields (id < 128).
      // 3. The rest of the fields, encoded as ranges.
      // Also update the |message_offset_| index to remember the word offset for
      // the current message.
      message_offset_.emplace_back(static_cast<uint32_t>(words_.size()));
      words_.emplace_back(static_cast<uint32_t>(direct_indexed_fields.size()));
      words_.insert(words_.end(), direct_indexed_fields.begin(),
                    direct_indexed_fields.end());
      words_.insert(words_.end(), ranges.begin(), ranges.end());
      direct_indexed_fields.clear();
      ranges.clear();
      ++current_msg_index;
    } else {
      PERFETTO_DLOG("bytecode error @ word %zu: invalid opcode (%x)", i, word);
      return false;
    }
  }  // (for word in bytecode).

  if (!is_eom) {
    PERFETTO_DLOG(
        "bytecode error: end of message not the last word in the bytecode");
    return false;
  }

  if (overlay_idx != overlay.size()) {
    PERFETTO_DLOG("bytecode error: overlay contains %zu unconsumed entries",
                  overlay.size() - overlay_idx);
    return false;
  }

  if (max_msg_index > 0 && max_msg_index >= message_offset_.size()) {
    PERFETTO_DLOG(
        "bytecode error: a message index (%u) is out of range "
        "(num_messages=%zu)",
        max_msg_index, message_offset_.size());
    return false;
  }

  // Add a final entry to |message_offset_| so we can tell where the last
  // message ends without an extra branch in the Query() hotpath.
  message_offset_.emplace_back(static_cast<uint32_t>(words_.size()));

  return true;
}

FilterBytecodeParser::QueryResult FilterBytecodeParser::Query(
    uint32_t msg_index,
    uint32_t field_id) const {
  FilterBytecodeParser::QueryResult res{false, 0u, 0u};
  if (static_cast<uint64_t>(msg_index) + 1 >=
      static_cast<uint64_t>(message_offset_.size())) {
    return res;
  }
  const uint32_t start_offset = message_offset_[msg_index];
  // These are DCHECKs and not just CHECKS because the |words_| is populated
  // by the LoadInternal call above. These cannot be violated with a malformed
  // bytecode.
  PERFETTO_DCHECK(start_offset < words_.size());
  const uint32_t* word = &words_[start_offset];
  const uint32_t end_off = message_offset_[msg_index + 1];
  const uint32_t* const end = words_.data() + end_off;
  PERFETTO_DCHECK(end > word && end <= words_.data() + words_.size());
  const uint32_t num_directly_indexed = *(word++);
  PERFETTO_DCHECK(num_directly_indexed <= kDirectlyIndexLimit);
  PERFETTO_DCHECK(word + num_directly_indexed <= end);
  uint32_t field_state = 0;
  if (PERFETTO_LIKELY(field_id < num_directly_indexed)) {
    PERFETTO_DCHECK(&word[field_id] < end);
    field_state = word[field_id];
  } else {
    for (word = word + num_directly_indexed; word + 2 < end;) {
      const uint32_t range_start = *(word++);
      const uint32_t range_end = *(word++);
      const uint32_t range_state = *(word++);
      if (field_id >= range_start && field_id < range_end) {
        field_state = range_state;
        break;
      }
    }  // for (word in ranges)
  }  // if (field_id >= num_directly_indexed)

  res.allowed = (field_state & kAllowed) != 0;
  res.nested_msg_index = field_state & ~kAllowed;
  // Extract semantic type if this is a FilterStringFieldWithType.
  // Exclude kFilterStringField (0x7ffffffe) which doesn't have semantic type.
  if ((res.nested_msg_index & kFilterStringFieldWithTypeMask) ==
          kFilterStringFieldWithType &&
      res.nested_msg_index != kFilterStringField) {
    res.semantic_type = res.nested_msg_index & kSemanticTypeMask;
  }
  PERFETTO_DCHECK(!res.nested_msg_field() ||
                  res.nested_msg_index < message_offset_.size() - 1);
  return res;
}

}  // namespace protozero
