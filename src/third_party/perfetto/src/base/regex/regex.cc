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

#include "perfetto/ext/base/regex.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flags.h"
#include "perfetto/ext/base/status_macros.h"

// Picks the regex backend and exposes it as PERFETTO_REGEX_BACKEND.
// Preference order:
//   1. PERFETTO_REGEX_FORCE_STD -> std::regex
//   2. PCRE2 (when enabled and the runtime flag is on)
//   3. RE2   (when enabled)
//   4. std::regex (fallback)
#if defined(PERFETTO_REGEX_FORCE_STD)
#include "src/base/regex/regex_std.h"
#define PERFETTO_REGEX_BACKEND ::perfetto::base::RegexStd
#elif PERFETTO_BUILDFLAG(PERFETTO_PCRE2) && PERFETTO_FLAGS_USE_PCRE2
#include "src/base/regex/regex_pcre2.h"
#define PERFETTO_REGEX_BACKEND ::perfetto::base::RegexPcre2
#elif PERFETTO_BUILDFLAG(PERFETTO_RE2)
#include "src/base/regex/regex_re2.h"
#define PERFETTO_REGEX_BACKEND ::perfetto::base::RegexRe2
#else
#include "src/base/regex/regex_std.h"
#define PERFETTO_REGEX_BACKEND ::perfetto::base::RegexStd
#endif

namespace perfetto {
namespace base {

// Pimpl for Regex. Holds the selected backend; Regex reaches into `backend`
// directly (Regex is a friend below).
class RegexImpl {
 public:
  using Backend = PERFETTO_REGEX_BACKEND;
  explicit RegexImpl(Backend b) : backend(std::move(b)) {}

 private:
  friend class Regex;
  Backend backend;
};
#undef PERFETTO_REGEX_BACKEND

Regex::Regex(Regex&&) noexcept = default;
Regex& Regex::operator=(Regex&&) noexcept = default;
Regex::~Regex() = default;

StatusOr<Regex> Regex::Create(std::string_view pattern,
                              Regex::CaseSensitivity cs) {
  bool insensitive = (cs == CaseSensitivity::kInsensitive);
  ASSIGN_OR_RETURN(auto backend,
                   RegexImpl::Backend::Create(pattern, insensitive));
  Regex regex;
  regex.pattern_ = std::string(pattern);
  regex.cs_ = cs;
  regex.impl_ = std::make_unique<RegexImpl>(std::move(backend));
  return std::move(regex);
}

Regex Regex::CreateOrCheck(std::string_view pattern,
                           Regex::CaseSensitivity cs) {
  auto re_or = Create(pattern, cs);
  PERFETTO_CHECK(re_or.ok());
  return std::move(*re_or);
}

std::string Regex::GlobalReplace(std::string_view s,
                                 std::string_view replacement) const {
  PERFETTO_DCHECK(impl_);
  return impl_->backend.GlobalReplace(s, replacement);
}

bool Regex::FullMatch(std::string_view s) const {
  PERFETTO_DCHECK(impl_);
  return impl_->backend.FullMatch(s);
}

bool Regex::FullMatchWithGroups(std::string_view s,
                                std::vector<std::string_view>& out) const {
  PERFETTO_DCHECK(impl_);
  return impl_->backend.FullMatchWithGroups(s, out);
}

bool Regex::PartialMatch(std::string_view s) const {
  PERFETTO_DCHECK(impl_);
  return impl_->backend.PartialMatch(s);
}

bool Regex::PartialMatchWithGroups(std::string_view s,
                                   std::vector<std::string_view>& out) const {
  PERFETTO_DCHECK(impl_);
  return impl_->backend.PartialMatchWithGroups(s, out);
}

// --- PartialMatchIterator ---

Regex::PartialMatchIterator::PartialMatchIterator(const Regex* re,
                                                  std::string_view input)
    : re_(re), input_(input) {}

std::optional<std::string_view> Regex::PartialMatchIterator::Next() {
  return NextWithGroups(groups_);
}

std::optional<std::string_view> Regex::PartialMatchIterator::NextWithGroups(
    std::vector<std::string_view>& groups) {
  if (offset_ > input_.size())
    return std::nullopt;
  PERFETTO_DCHECK(re_->impl_);
  if (!re_->impl_->backend.SearchWithOffset(input_, offset_, groups))
    return std::nullopt;
  if (groups.empty())
    return std::nullopt;
  std::string_view match = groups[0];
  size_t match_end =
      static_cast<size_t>(match.data() - input_.data()) + match.size();
  // Advance past the match. If match is empty, advance by one to avoid
  // infinite loops.
  offset_ = match.size() == 0 ? match_end + 1 : match_end;
  return match;
}

Regex::PartialMatchIterator Regex::PartialMatchAll(std::string_view s) const {
  return PartialMatchIterator(this, s);
}

// --- CopyableRegex ---

CopyableRegex::CopyableRegex(Regex regex) : regex_(std::move(regex)) {}

CopyableRegex::CopyableRegex(const CopyableRegex& other)
    : regex_(Regex::CreateOrCheck(other.regex_.pattern(),
                                  other.regex_.case_sensitivity())) {}

CopyableRegex& CopyableRegex::operator=(const CopyableRegex& other) {
  if (this != &other) {
    this->~CopyableRegex();
    new (this) CopyableRegex(other);
  }
  return *this;
}

}  // namespace base
}  // namespace perfetto
