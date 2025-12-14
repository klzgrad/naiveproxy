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

#ifndef SRC_TRACE_PROCESSOR_UTIL_REGEX_H_
#define SRC_TRACE_PROCESSOR_UTIL_REGEX_H_

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <regex.h>
#endif

namespace perfetto::trace_processor::regex {

constexpr bool IsRegexSupported() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return false;
#else
  return true;
#endif
}

// Implements regex parsing and regex search based on C library `regex.h`.
// Doesn't work on Windows.
class Regex {
 public:
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  ~Regex() {
    if (regex_) {
      regfree(&regex_.value());
    }
  }
  Regex(const Regex&) = delete;
  Regex(Regex&& other) {
    regex_ = other.regex_;
    other.regex_ = std::nullopt;
  }
  Regex& operator=(Regex&& other) {
    this->~Regex();
    new (this) Regex(std::move(other));
    return *this;
  }
  Regex& operator=(const Regex&) = delete;
#endif

  // Parse regex pattern. Returns error if regex pattern is invalid.
  static base::StatusOr<Regex> Create(const char* pattern) {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    regex_t regex;
    if (regcomp(&regex, pattern, REG_EXTENDED)) {
      return base::ErrStatus("Regex pattern '%s' is malformed.", pattern);
    }
    return Regex(regex);
#else
    base::ignore_result(pattern);
    PERFETTO_FATAL("Windows regex is not supported.");
#endif
  }

  // Returns true if string matches the regex.
  bool Search(const char* s) const {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    PERFETTO_CHECK(regex_);
    return regexec(&regex_.value(), s, 0, nullptr, 0) == 0;
#else
    base::ignore_result(s);
    PERFETTO_FATAL("Windows regex is not supported.");
#endif
  }

  // Returns a vector of string views representing the matched groups.
  // The first element is the full match. Subsequent elements are parenthesized
  // subexpressions.
  // Returns nullopt if there is no match.
  void Submatch(const char* s, std::vector<std::string_view>& out) {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    PERFETTO_CHECK(regex_);
    const auto& rgx = regex_.value();
    size_t nmatch = rgx.re_nsub + 1;
    pmatch_.resize(nmatch);

    out.clear();
    if (regexec(&rgx, s, nmatch, pmatch_.data(), 0) != 0) {
      return;
    }
    for (size_t i = 0; i < nmatch; ++i) {
      if (pmatch_[i].rm_so == -1) {
        // Optional group that did not match.
        out.emplace_back();
      } else {
        out.emplace_back(
            s + pmatch_[i].rm_so,
            static_cast<size_t>(pmatch_[i].rm_eo - pmatch_[i].rm_so));
      }
    }
#else
    base::ignore_result(out);
    if (s)
      PERFETTO_FATAL("Windows regex is not supported.");
#endif
  }

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
 private:
  explicit Regex(regex_t regex) : regex_(regex) {}

  std::optional<regex_t> regex_;
  std::vector<regmatch_t> pmatch_;
#endif
};
}  // namespace perfetto::trace_processor::regex

#endif  // SRC_TRACE_PROCESSOR_UTIL_REGEX_H_
