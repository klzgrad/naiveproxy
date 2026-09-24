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

#ifndef INCLUDE_PERFETTO_EXT_BASE_REGEX_H_
#define INCLUDE_PERFETTO_EXT_BASE_REGEX_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/status_or.h"

namespace perfetto {
namespace base {

// Pimpl for the regex engine. Defined in src/base/regex/regex.cc.
class RegexImpl;

// A regular expression abstraction that hides the underlying engine.
//
// Construct via the two static factories:
//   - Create()        – fallible; returns StatusOr<Regex>.
//   - CreateOrCheck() – infallible; crashes on invalid pattern.
//                       Use for compile-time constant patterns only.
//
// Regex is movable but NOT copyable. Use CopyableRegex when copy semantics
// are needed (e.g. for storage in copyable containers).
//
// Patterns use ECMAScript syntax, supporting Perl-style shorthands (\d, \w,
// \s).
//
// Operations:
//   GlobalReplace()          – global substitution; returns a new string.
//   FullMatch()              – anchored, no groups.
//   FullMatchWithGroups()    – anchored, with capture groups.
//   PartialMatch()           – unanchored, no groups.
//   PartialMatchWithGroups() – unanchored, with capture groups.
//   PartialMatchAll()        – iterator over all non-overlapping matches.
class Regex {
 public:
  // Controls whether matching is case-sensitive or case-insensitive.
  enum class CaseSensitivity : uint8_t {
    kSensitive,
    kInsensitive,
  };

  Regex(Regex&&) noexcept;
  Regex& operator=(Regex&&) noexcept;
  ~Regex();

  // Regex is not copyable. Use CopyableRegex for copy semantics.
  Regex(const Regex&) = delete;
  Regex& operator=(const Regex&) = delete;

  // Compiles |pattern| into a Regex.
  // Returns an error if the pattern syntax is invalid.
  static StatusOr<Regex> Create(
      std::string_view pattern,
      CaseSensitivity cs = CaseSensitivity::kSensitive);

  // Like Create(), but crashes (PERFETTO_CHECK) on an invalid pattern.
  // Use only for patterns that are known at compile time.
  static Regex CreateOrCheck(std::string_view pattern,
                             CaseSensitivity cs = CaseSensitivity::kSensitive);

  // Replaces every occurrence of the regex in |s| with |replacement| and
  // returns the resulting string.  |s| is unchanged.
  std::string GlobalReplace(std::string_view s,
                            std::string_view replacement) const;

  // Returns true if the regex matches the entirety of |s|.
  bool FullMatch(std::string_view s) const;

  // Like FullMatch(), but also fills |out| with captured groups on success.
  //   out[0]    – the full match.
  //   out[1..N] – the parenthesised sub-groups.
  // Returns false (and clears |out|) when there is no match.
  //
  // The string_views in |out| point into |s|; they are invalidated if |s| is
  // modified or destroyed.
  bool FullMatchWithGroups(std::string_view s,
                           std::vector<std::string_view>& out) const;

  // Returns true if the regex matches any substring of |s|.
  bool PartialMatch(std::string_view s) const;

  // Like PartialMatch(), but also fills |out| with captured groups on success.
  //   out[0]    – the full match.
  //   out[1..N] – the parenthesised sub-groups.
  // Returns false (and clears |out|) when there is no match.
  //
  // The string_views in |out| point into |s|; they are invalidated if |s| is
  // modified or destroyed.
  bool PartialMatchWithGroups(std::string_view s,
                              std::vector<std::string_view>& out) const;

  // Iterator over all non-overlapping matches in a string.
  class PartialMatchIterator {
   public:
    // Returns the next match, or std::nullopt when no more matches remain.
    std::optional<std::string_view> Next();

    // Returns the next match with capture groups. Groups are filled into
    // |groups|. Returns std::nullopt when no more matches remain.
    std::optional<std::string_view> NextWithGroups(
        std::vector<std::string_view>& groups);

   private:
    friend class Regex;
    PartialMatchIterator(const Regex* re, std::string_view input);
    const Regex* re_;
    std::string_view input_;
    size_t offset_ = 0;
    std::vector<std::string_view> groups_;  // Cached for Next().
  };

  // Returns an iterator that yields all non-overlapping matches in |s|.
  PartialMatchIterator PartialMatchAll(std::string_view s) const;

  // Accessors for pattern and case sensitivity (used by CopyableRegex).
  const std::string& pattern() const { return pattern_; }
  CaseSensitivity case_sensitivity() const { return cs_; }

 private:
  Regex() = default;
  std::unique_ptr<RegexImpl> impl_;
  std::string pattern_;
  CaseSensitivity cs_ = CaseSensitivity::kSensitive;
};

// A copyable wrapper around Regex. Copying re-compiles the pattern from the
// stored string, so prefer moving when the source is no longer needed.
//
// Use this class when Regex needs to be stored in a copyable container
// (e.g. std::optional in a copyable struct).
class CopyableRegex {
 public:
  explicit CopyableRegex(Regex regex);

  CopyableRegex(const CopyableRegex& other);
  CopyableRegex& operator=(const CopyableRegex& other);
  CopyableRegex(CopyableRegex&&) noexcept = default;
  CopyableRegex& operator=(CopyableRegex&&) noexcept = default;
  ~CopyableRegex() = default;

  std::string GlobalReplace(std::string_view s,
                            std::string_view replacement) const {
    return regex_.GlobalReplace(s, replacement);
  }
  bool FullMatch(std::string_view s) const { return regex_.FullMatch(s); }
  bool FullMatchWithGroups(std::string_view s,
                           std::vector<std::string_view>& out) const {
    return regex_.FullMatchWithGroups(s, out);
  }
  bool PartialMatch(std::string_view s) const { return regex_.PartialMatch(s); }
  bool PartialMatchWithGroups(std::string_view s,
                              std::vector<std::string_view>& out) const {
    return regex_.PartialMatchWithGroups(s, out);
  }
  Regex::PartialMatchIterator PartialMatchAll(std::string_view s) const {
    return regex_.PartialMatchAll(s);
  }

 private:
  Regex regex_;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_REGEX_H_
