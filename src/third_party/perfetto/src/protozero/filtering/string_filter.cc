/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/protozero/filtering/string_filter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/public/compiler.h"

namespace protozero {
namespace {

constexpr std::string_view kRedacted = "P60REDACTED";
constexpr char kRedactedDash = '-';

// Returns a pointer to the first character after the tgid pipe character in
// the atrace string given by [ptr, end). Returns null if no such character
// exists.
//
// Examples:
// E|1024 -> nullptr
// foobarbaz -> nullptr
// B|1024|x -> pointer to x
PERFETTO_ALWAYS_INLINE const char* FindAtracePayloadPtr(const char* ptr,
                                                        const char* end) {
  // Don't even bother checking any strings which are so short that they could
  // not contain a post-tgid section. This filters out strings like "E|" which
  // emitted by Bionic.
  //
  // Also filter out any other strings starting with "E" as they never contain
  // anything past the tgid: this removes >half of the strings for ~zero cost.
  static constexpr size_t kEarliestSecondPipeIndex = 2;
  const char* search_start = ptr + kEarliestSecondPipeIndex;
  if (PERFETTO_UNLIKELY(search_start >= end || *ptr == 'E')) {
    return nullptr;
  }

  // We skipped past the first '|' character by starting at the character at
  // index 2. Just find the next pipe character (i.e. the one after tgid) using
  // memchr.
  const char* pipe = static_cast<const char*>(
      memchr(search_start, '|', size_t(end - search_start)));
  return pipe ? pipe + 1 : nullptr;
}

PERFETTO_ALWAYS_INLINE bool StartsWith(const char* ptr,
                                       const char* end,
                                       const std::string& starts_with) {
  // Verify that the atrace string has enough characters to match against all
  // the characters in the "starts with" string.
  size_t len = starts_with.size();
  if (PERFETTO_UNLIKELY(ptr + len > end))
    return false;

  // Empty string matches everything.
  if (PERFETTO_UNLIKELY(len == 0))
    return true;

  // Quick rejection: check first character before expensive memcmp.
  // This is very effective since most strings don't match.
  if (PERFETTO_LIKELY(*ptr != *starts_with.data()))
    return false;

  // If first char matches, do full memcmp for remaining characters.
  return memcmp(ptr + 1, starts_with.data() + 1, len - 1) == 0;
}

void RedactMatches(const std::vector<std::string_view>& matches) {
  // Go through every group in the matches.
  for (size_t i = 1; i < matches.size(); ++i) {
    const auto& match = matches[i];
    // Skip unmatched optional groups: empty string_view may have nullptr
    // data(), and passing nullptr to memcpy/memset is UB even with size 0.
    if (match.empty())
      continue;

    // Overwrite the match with characters from |kRedacted|. If match is
    // smaller, we will not use all of |kRedacted| but that's fine (i.e. we
    // will overwrite with a truncated |kRedacted|).
    size_t match_len = match.size();
    size_t redacted_len = std::min(match_len, kRedacted.size());
    memcpy(const_cast<char*>(match.data()), kRedacted.data(), redacted_len);

    // Overwrite any characters after |kRedacted| with |kRedactedDash|.
    memset(const_cast<char*>(match.data()) + redacted_len, kRedactedDash,
           match_len - redacted_len);
  }
}

}  // namespace

void StringFilter::AddRule(Policy policy,
                           std::string_view pattern_str,
                           std::string atrace_payload_starts_with,
                           std::string name,
                           SemanticTypeMask semantic_type_mask) {
  perfetto::base::CopyableRegex re(
      perfetto::base::Regex::CreateOrCheck(std::string(pattern_str)));
  Rule new_rule{policy, std::move(re), std::move(atrace_payload_starts_with),
                std::move(name), semantic_type_mask};
  // If name is non-empty, look for existing rule with same name and replace.
  if (!new_rule.name.empty()) {
    for (Rule& existing : rules_) {
      if (existing.name == new_rule.name) {
        existing = std::move(new_rule);
        return;
      }
    }
  }
  rules_.push_back(std::move(new_rule));
}

bool StringFilter::MaybeFilterInternal(char* ptr,
                                       size_t len,
                                       uint32_t semantic_type) const {
  bool atrace_find_tried = false;
  const char* atrace_payload_ptr = nullptr;
  std::string_view input(ptr, len);

  for (const Rule& rule : rules_) {
    if (!rule.semantic_type_mask.IsSet(semantic_type)) {
      continue;
    }
    switch (rule.policy) {
      case Policy::kMatchBreak:
        if (PERFETTO_UNLIKELY(rule.pattern->FullMatch(input)))
          return false;
        break;
      case Policy::kMatchRedactGroups:
        if (PERFETTO_UNLIKELY(
                rule.pattern->FullMatchWithGroups(input, matches_))) {
          RedactMatches(matches_);
          return true;
        }
        break;
      case Policy::kAtraceMatchBreak:
        atrace_payload_ptr = atrace_find_tried
                                 ? atrace_payload_ptr
                                 : FindAtracePayloadPtr(ptr, ptr + len);
        atrace_find_tried = true;
        if (atrace_payload_ptr &&
            StartsWith(atrace_payload_ptr, ptr + len,
                       rule.atrace_payload_starts_with) &&
            rule.pattern->FullMatch(input)) {
          return false;
        }
        break;
      case Policy::kAtraceMatchRedactGroups:
        atrace_payload_ptr = atrace_find_tried
                                 ? atrace_payload_ptr
                                 : FindAtracePayloadPtr(ptr, ptr + len);
        atrace_find_tried = true;
        if (atrace_payload_ptr &&
            StartsWith(atrace_payload_ptr, ptr + len,
                       rule.atrace_payload_starts_with) &&
            PERFETTO_UNLIKELY(
                rule.pattern->FullMatchWithGroups(input, matches_))) {
          RedactMatches(matches_);
          return true;
        }
        break;
      case Policy::kAtraceRepeatedSearchRedactGroups:
        atrace_payload_ptr = atrace_find_tried
                                 ? atrace_payload_ptr
                                 : FindAtracePayloadPtr(ptr, ptr + len);
        atrace_find_tried = true;
        if (atrace_payload_ptr && StartsWith(atrace_payload_ptr, ptr + len,
                                             rule.atrace_payload_starts_with)) {
          bool has_any_matches = false;
          for (auto iter = rule.pattern->PartialMatchAll(input);
               iter.NextWithGroups(matches_);) {
            RedactMatches(matches_);
            has_any_matches = true;
          }
          if (has_any_matches) {
            return true;
          }
        }
        break;
    }
  }
  return false;
}

}  // namespace protozero
