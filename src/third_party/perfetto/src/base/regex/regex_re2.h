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

#ifndef SRC_BASE_REGEX_REGEX_RE2_H_
#define SRC_BASE_REGEX_REGEX_RE2_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/status_or.h"

// Using quotes instead of angle brackets because Bazel passes external repo
// paths via -iquote (not -isystem), which only covers quoted includes.
// See google/re2 commit baeed1c and bazelbuild/bazel#18974.
#include "re2/re2.h"

namespace perfetto {
namespace base {

// RE2 backend for Regex. Used on non-Android builds (standalone, Chromium).
// Header-only so the compiler can inline through unique_ptr<RegexImpl>.
class RegexRe2 {
 public:
  RegexRe2(RegexRe2&&) noexcept = default;
  RegexRe2& operator=(RegexRe2&&) noexcept = default;
  ~RegexRe2() = default;

  RegexRe2(const RegexRe2&) = delete;
  RegexRe2& operator=(const RegexRe2&) = delete;

  static StatusOr<RegexRe2> Create(std::string_view pattern,
                                   bool case_insensitive) {
    RegexRe2 result;
    re2::RE2::Options options;
    options.set_log_errors(false);
    options.set_case_sensitive(!case_insensitive);
    result.re_ = std::make_unique<re2::RE2>(std::string(pattern), options);
    if (!result.re_->ok()) {
      return ErrStatus("RE2 compile error: %s", result.re_->error().c_str());
    }
    result.groups_.resize(
        static_cast<size_t>(result.re_->NumberOfCapturingGroups() + 1));
    return std::move(result);
  }

  std::string GlobalReplace(std::string_view s,
                            std::string_view replacement) const {
    std::string out(s);
    re2::RE2::GlobalReplace(&out, *re_, replacement);
    return out;
  }

  bool FullMatch(std::string_view s) const {
    return re2::RE2::FullMatch(s, *re_);
  }

  bool FullMatchWithGroups(std::string_view s,
                           std::vector<std::string_view>& out) const {
    out.clear();
    int n = static_cast<int>(groups_.size());
    if (!re_->Match(s, 0, s.size(), re2::RE2::ANCHOR_BOTH, groups_.data(), n))
      return false;
    out.assign(groups_.begin(), groups_.begin() + n);
    return true;
  }

  bool PartialMatch(std::string_view s) const {
    return re2::RE2::PartialMatch(s, *re_);
  }

  bool PartialMatchWithGroups(std::string_view s,
                              std::vector<std::string_view>& out) const {
    out.clear();
    int n = static_cast<int>(groups_.size());
    if (!re_->Match(s, 0, s.size(), re2::RE2::UNANCHORED, groups_.data(), n))
      return false;
    out.assign(groups_.begin(), groups_.begin() + n);
    return true;
  }

  bool SearchWithOffset(std::string_view s,
                        size_t start_offset,
                        std::vector<std::string_view>& out) const {
    out.clear();
    if (start_offset > s.size())
      return false;
    int n = static_cast<int>(groups_.size());
    if (!re_->Match(s, start_offset, s.size(), re2::RE2::UNANCHORED,
                    groups_.data(), n))
      return false;
    out.assign(groups_.begin(), groups_.begin() + n);
    return true;
  }

 private:
  RegexRe2() = default;
  std::unique_ptr<re2::RE2> re_;
  // Must be absl::string_view, not std::string_view, to match RE2::Match()
  // signature. These can be distinct types in different builds.
  mutable std::vector<absl::string_view> groups_;
};

}  // namespace base
}  // namespace perfetto

#endif  // SRC_BASE_REGEX_REGEX_RE2_H_
