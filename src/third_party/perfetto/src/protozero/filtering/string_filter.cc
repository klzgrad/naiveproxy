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

#include <cstring>
#include <regex>
#include <string_view>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"

namespace protozero {
namespace {

using Matches = std::match_results<char*>;

static constexpr std::string_view kRedacted = "P60REDACTED";
static constexpr char kRedactedDash = '-';

// Returns a pointer to the first character after the tgid pipe character in
// the atrace string given by [ptr, end). Returns null if no such character
// exists.
//
// Examples:
// E|1024 -> nullptr
// foobarbaz -> nullptr
// B|1024|x -> pointer to x
const char* FindAtracePayloadPtr(const char* ptr, const char* end) {
  // Don't even bother checking any strings which are so short that they could
  // not contain a post-tgid section. This filters out strings like "E|" which
  // emitted by Bionic.
  //
  // Also filter out any other strings starting with "E" as they never contain
  // anything past the tgid: this removes >half of the strings for ~zero cost.
  static constexpr size_t kEarliestSecondPipeIndex = 2;
  const char* search_start = ptr + kEarliestSecondPipeIndex;
  if (search_start >= end || *ptr == 'E') {
    return nullptr;
  }

  // We skipped past the first '|' character by starting at the character at
  // index 2. Just find the next pipe character (i.e. the one after tgid) using
  // memchr.
  const char* pipe = static_cast<const char*>(
      memchr(search_start, '|', size_t(end - search_start)));
  return pipe ? pipe + 1 : nullptr;
}

bool StartsWith(const char* ptr,
                const char* end,
                const std::string& starts_with) {
  // Verify that the atrace string has enough characters to match against all
  // the characters in the "starts with" string. If it does, memcmp to check if
  // all the characters match and return true if they do.
  return ptr + starts_with.size() <= end &&
         memcmp(ptr, starts_with.data(), starts_with.size()) == 0;
}

void RedactMatches(const Matches& matches) {
  // Go through every group in the matches.
  for (size_t i = 1; i < matches.size(); ++i) {
    const auto& match = matches[i];
    PERFETTO_CHECK(match.second >= match.first);

    // Overwrite the match with characters from |kRedacted|. If match is
    // smaller, we will not use all of |kRedacted| but that's fine (i.e. we
    // will overwrite with a truncated |kRedacted|).
    size_t match_len = static_cast<size_t>(match.second - match.first);
    size_t redacted_len = std::min(match_len, kRedacted.size());
    memcpy(match.first, kRedacted.data(), redacted_len);

    // Overwrite any characters after |kRedacted| with |kRedactedDash|.
    memset(match.first + redacted_len, kRedactedDash, match_len - redacted_len);
  }
}

}  // namespace

void StringFilter::AddRule(Policy policy,
                           std::string_view pattern_str,
                           std::string atrace_payload_starts_with) {
  rules_.emplace_back(StringFilter::Rule{
      policy,
      std::regex(pattern_str.begin(), pattern_str.end(),
                 std::regex::ECMAScript | std::regex_constants::optimize),
      std::move(atrace_payload_starts_with)});
}

bool StringFilter::MaybeFilterInternal(char* ptr, size_t len) const {
  std::match_results<char*> matches;
  bool atrace_find_tried = false;
  const char* atrace_payload_ptr = nullptr;
  for (const Rule& rule : rules_) {
    switch (rule.policy) {
      case Policy::kMatchRedactGroups:
      case Policy::kMatchBreak:
        if (std::regex_match(ptr, ptr + len, matches, rule.pattern)) {
          if (rule.policy == Policy::kMatchBreak) {
            return false;
          }
          RedactMatches(matches);
          return true;
        }
        break;
      case Policy::kAtraceMatchRedactGroups:
      case Policy::kAtraceMatchBreak:
        atrace_payload_ptr = atrace_find_tried
                                 ? atrace_payload_ptr
                                 : FindAtracePayloadPtr(ptr, ptr + len);
        atrace_find_tried = true;
        if (atrace_payload_ptr &&
            StartsWith(atrace_payload_ptr, ptr + len,
                       rule.atrace_payload_starts_with) &&
            std::regex_match(ptr, ptr + len, matches, rule.pattern)) {
          if (rule.policy == Policy::kAtraceMatchBreak) {
            return false;
          }
          RedactMatches(matches);
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
          auto beg = std::regex_iterator<char*>(ptr, ptr + len, rule.pattern);
          auto end = std::regex_iterator<char*>();
          bool has_any_matches = beg != end;
          for (auto it = std::move(beg); it != end; ++it) {
            RedactMatches(*it);
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
