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

#ifndef SRC_BASE_REGEX_REGEX_STD_H_
#define SRC_BASE_REGEX_REGEX_STD_H_

#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/status_or.h"

namespace perfetto {
namespace base {

// std::regex backend for Regex. Uses ECMAScript syntax for Perl-style
// shorthands (\d, \w, \s).
class RegexStd {
 public:
  RegexStd(RegexStd&&) noexcept = default;
  RegexStd& operator=(RegexStd&&) noexcept = default;
  ~RegexStd() = default;

  RegexStd(const RegexStd&) = delete;
  RegexStd& operator=(const RegexStd&) = delete;

  static StatusOr<RegexStd> Create(std::string_view pattern,
                                   bool case_insensitive) {
    RegexStd result;
    auto flags = std::regex::ECMAScript;
    if (case_insensitive) {
      flags |= std::regex::icase;
    }
    result.re_ = std::regex(std::string(pattern), flags);
    return std::move(result);
  }

  std::string GlobalReplace(std::string_view s,
                            std::string_view replacement) const {
    return std::regex_replace(std::string(s), re_, std::string(replacement));
  }

  bool FullMatch(std::string_view s) const {
    return std::regex_match(s.data(), s.data() + s.size(), re_);
  }

  bool FullMatchWithGroups(std::string_view s,
                           std::vector<std::string_view>& out) const {
    out.clear();
    if (!std::regex_match(s.data(), s.data() + s.size(), match_data_, re_))
      return false;
    FillGroups(s.data(), out);
    return true;
  }

  bool PartialMatch(std::string_view s) const {
    return std::regex_search(s.data(), s.data() + s.size(), re_);
  }

  bool PartialMatchWithGroups(std::string_view s,
                              std::vector<std::string_view>& out) const {
    out.clear();
    if (!std::regex_search(s.data(), s.data() + s.size(), match_data_, re_))
      return false;
    FillGroups(s.data(), out);
    return true;
  }

  bool SearchWithOffset(std::string_view s,
                        size_t start_offset,
                        std::vector<std::string_view>& out) const {
    out.clear();
    const char* begin = s.data() + start_offset;
    const char* end = s.data() + s.size();
    if (begin > end)
      return false;
    if (!std::regex_search(begin, end, match_data_, re_))
      return false;
    FillGroups(begin, out);
    return true;
  }

 private:
  RegexStd() = default;

  void FillGroups(const char* base, std::vector<std::string_view>& out) const {
    for (size_t i = 0; i < match_data_.size(); ++i) {
      if (match_data_[i].matched) {
        out.emplace_back(base + match_data_.position(i),
                         static_cast<size_t>(match_data_.length(i)));
      } else {
        out.emplace_back();
      }
    }
  }

  std::regex re_;
  mutable std::cmatch match_data_;
};

}  // namespace base
}  // namespace perfetto

#endif  // SRC_BASE_REGEX_REGEX_STD_H_
