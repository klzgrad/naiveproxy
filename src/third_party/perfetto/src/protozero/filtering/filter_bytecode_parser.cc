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

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/protozero/filtering/filter_bytecode_common.h"

namespace protozero {

void FilterBytecodeParser::Reset() {
  bool suppress = suppress_logs_for_fuzzer_;
  *this = FilterBytecodeParser();
  suppress_logs_for_fuzzer_ = suppress;
}

bool FilterBytecodeParser::Load(const void* filter_data, size_t len) {
  Reset();
  bool res = LoadInternal(static_cast<const uint8_t*>(filter_data), len);
  // If load fails, don't leave the parser in a half broken state.
  if (!res)
    Reset();
  return res;
}

bool FilterBytecodeParser::LoadInternal(const uint8_t* bytecode_data,
                                        size_t len) {
  // First unpack the varints into a plain uint32 vector, so it's easy to
  // iterate through them and look ahead.
  std::vector<uint32_t> words;
  bool packed_parse_err = false;
  words.reserve(len);  // An overestimation, but avoids reallocations.
  using BytecodeDecoder =
      PackedRepeatedFieldIterator<proto_utils::ProtoWireType::kVarInt,
                                  uint32_t>;
  for (BytecodeDecoder it(bytecode_data, len, &packed_parse_err); it; ++it)
    words.emplace_back(*it);

  if (packed_parse_err || words.empty())
    return false;

  perfetto::base::Hasher hasher;
  for (size_t i = 0; i < words.size() - 1; ++i)
    hasher.Update(words[i]);

  uint32_t expected_csum = static_cast<uint32_t>(hasher.digest());
  if (expected_csum != words.back()) {
    if (!suppress_logs_for_fuzzer_) {
      PERFETTO_ELOG("Filter bytecode checksum failed. Expected: %x, actual: %x",
                    expected_csum, words.back());
    }
    return false;
  }

  words.pop_back();  // Pop the checksum.

  // Temporary storage for each message. Cleared on every END_OF_MESSAGE.
  std::vector<uint32_t> direct_indexed_fields;
  std::vector<uint32_t> ranges;
  uint32_t max_msg_index = 0;

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

  bool is_eom = true;
  for (size_t i = 0; i < words.size(); ++i) {
    const uint32_t word = words[i];
    const bool has_next_word = i < words.size() - 1;
    const uint32_t opcode = word & 0x7u;
    const uint32_t field_id = word >> 3;

    is_eom = opcode == kFilterOpcode_EndOfMessage;
    if (field_id == 0 && opcode != kFilterOpcode_EndOfMessage) {
      PERFETTO_DLOG("bytecode error @ word %zu, invalid field id (0)", i);
      return false;
    }

    if (opcode == kFilterOpcode_SimpleField ||
        opcode == kFilterOpcode_NestedField ||
        opcode == kFilterOpcode_FilterString) {
      // Field words are organized as follow:
      // MSB: 1 if allowed, 0 if not allowed.
      // Remaining bits:
      //   Message index in the case of nested (non-simple) messages.
      //   0x7f..e in the case of string fields which need filtering.
      //   0x7f..f in the case of simple fields.
      uint32_t msg_id;
      if (opcode == kFilterOpcode_SimpleField) {
        msg_id = kSimpleField;
      } else if (opcode == kFilterOpcode_FilterString) {
        msg_id = kFilterStringField;
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
  FilterBytecodeParser::QueryResult res{false, 0u};
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
  PERFETTO_DCHECK(!res.nested_msg_field() ||
                  res.nested_msg_index < message_offset_.size() - 1);
  return res;
}

}  // namespace protozero
