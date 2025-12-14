/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/util/glob.h"

#include <cstddef>
#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"

namespace perfetto::trace_processor::util {

GlobMatcher::GlobMatcher(base::StringView pattern_str)
    : pattern_(pattern_str.size() + 1) {
  base::StringCopy(pattern_.data(), pattern_str.data(), pattern_.size());

  base::StringView pattern(pattern_.data());

  // Note: see the class header for how this algorithm works.
  uint32_t segment_start = 0;
  uint32_t segment_potential_matched_chars = 0;
  auto create_segment = [this, &segment_start, &segment_potential_matched_chars,
                         &pattern](size_t i) {
    base::StringView segment = pattern.substr(segment_start, i - segment_start);
    PERFETTO_DCHECK(segment_potential_matched_chars <= segment.size());
    if (!segment.empty()) {
      PERFETTO_DCHECK(segment_potential_matched_chars > 0);
      segments_.emplace_back(Segment{segment, segment_potential_matched_chars});
    }
    return segment.empty();
  };

  for (uint32_t i = 0; i < pattern.size(); ++i) {
    char c = pattern.at(i);

    // If we don't have a star, we are only matching a single character (but
    // potentially with a character class which contains >1 character).
    if (c != '*') {
      if (c == '[') {
        base::StringView cclass = ExtractCharacterClass(pattern.substr(i + 1));
        contains_char_class_or_question_ |= !cclass.empty();

        // Skip the current '[' character.
        ++i;

        // Skip the whole character class. This will leave i pointing at the
        // terminating character (i.e. ']'). With the ++i in the loop, this will
        // correctly lead us going past the whole class.
        i += cclass.size();
      }

      contains_char_class_or_question_ |= c == '?';
      ++segment_potential_matched_chars;
      continue;
    }

    // Add the characters collected so far as a segment.
    create_segment(i);
    segment_start = i + 1;
    segment_potential_matched_chars = 0;
  }

  // Ensure we add any remaining characters as a segment.
  bool empty_segment = create_segment(pattern.size());
  leading_star_ = !pattern.empty() && pattern.at(0) == '*';
  trailing_star_ = !pattern.empty() && empty_segment;
}

bool GlobMatcher::Matches(base::StringView in) const {
  // If there are no segments, that means the pattern is either '' or '*'
  // (or '**', '***' etc which is really the same as '*'). This means
  // we are match if either a) there is a leading star (== trailing star) or
  // b) the input string is empty.
  if (segments_.empty()) {
    PERFETTO_DCHECK(leading_star_ == trailing_star_);
    return leading_star_ || in.empty();
  }

  // If there is only one segment and no stars we need an equality match.
  // As we still need to handle '[..]' and '?', we cannot just use string
  // equality. We *can* however use StartsWith and check the matched
  // characters is equal to the length of the input: this is basically the
  // same as checking equality.
  if (segments_.size() == 1 && !leading_star_ && !trailing_star_) {
    return segments_.front().matched_chars == in.size() &&
           StartsWith(in, segments_.front());
  }

  // If there's no leading star, the first segment needs to be handled
  // separately because it *needs* to be anchored to the left of the
  // string rather than appearing at some point later.
  if (!leading_star_ && !StartsWith(in, segments_.front())) {
    return false;
  }

  // Similarly, if there's no trailing star, the last segment needs to be
  // "anchored" to the right of the string.
  if (!trailing_star_ && !EndsWith(in, segments_.back())) {
    return false;
  }

  // For any segment we haven't checked, they needs to appear in the string
  // sequentially with possibly some characters separating them. To handle
  // this, we just need to iteratively find each segment, starting from the
  // previous segment.
  const Segment* segment_start = segments_.begin() + (leading_star_ ? 0 : 1);
  const Segment* segment_end = segments_.end() - (trailing_star_ ? 0 : 1);
  size_t find_idx = leading_star_ ? 0 : segments_.front().matched_chars;
  for (const auto* segment = segment_start; segment < segment_end; ++segment) {
    size_t pos = Find(in, *segment, find_idx);
    if (pos == base::StringView::npos) {
      return false;
    }
    find_idx = pos + segment->matched_chars;
  }

  // Every segment has been found to match so far including the leading and
  // trailing one so the entire string matches!
  return true;
}

bool GlobMatcher::StartsWithSlow(base::StringView in, const Segment& segment) {
  base::StringView pattern = segment.pattern;
  for (uint32_t i = 0, p = 0; p < pattern.size(); ++i, ++p) {
    // We've run out of characters to consume in the input but still have more
    // to consume in the pattern: |in| cannot possibly start with |pattern|.
    if (i >= in.size()) {
      return false;
    }

    char in_c = in.at(i);
    char pattern_c = pattern.at(p);

    // '?' matches any character.
    if (pattern_c == '?') {
      continue;
    }

    // '[' signifies the start of a character class.
    if (pattern_c == '[') {
      base::StringView cclass = ExtractCharacterClass(pattern.substr(p + 1));
      if (!MatchesCharacterClass(in_c, cclass)) {
        return false;
      }

      // Skip the current '[' character.
      p++;

      // Skip the whole character class. This will leave i pointing at the
      // terminating character (i.e. ']'). With the ++i in the loop, this will
      // correctly lead us going past the whole class.
      p += cclass.size();
      continue;
    }

    // Anything else is just an ordinary character.
    if (in_c != pattern_c) {
      return false;
    }
  }
  return true;
}

base::StringView GlobMatcher::ExtractCharacterClass(base::StringView in) {
  if (in.empty())
    return {};

  // We should always skip the first real character: it could be ']' but if
  // so, it is treated as a normal character because empty classes are not
  // valid.
  bool invert = in.at(0) == '^';
  size_t end_idx = in.find(']', invert ? 2 : 1);
  return end_idx == base::StringView::npos ? base::StringView()
                                           : in.substr(0, end_idx);
}

bool GlobMatcher::MatchesCharacterClass(char in, base::StringView char_class) {
  PERFETTO_DCHECK(!char_class.empty());

  const char* start = char_class.data();
  const char* end = start + char_class.size();

  bool invert = *start == '^';
  start += invert;

  PERFETTO_DCHECK(start != end);

  for (const char* ptr = start; ptr != end; ++ptr) {
    char cur = *ptr;

    // If we see a '-' at any point except at the start or end of the string,
    // it represents a matching range (e.g. a-z represents matching any
    // character between a and z).
    if (cur == '-' && ptr != start && ptr != end - 1) {
      // Look at the previous and next characters in the class and check if the
      // character falls in the range.
      char range_start = ptr[-1];
      char range_end = ptr[1];
      if (range_start <= in && in <= range_end) {
        return !invert;
      }
      continue;
    }

    // We match a character in the class.
    if (in == cur) {
      return !invert;
    }
  }

  // If we're here, nothing in the class matched: return whether the class was
  // inverted as this would actually be a match.
  return invert;
}

}  // namespace perfetto::trace_processor::util
