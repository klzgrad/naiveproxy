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

#ifndef SRC_BASE_REGEX_REGEX_PCRE2_H_
#define SRC_BASE_REGEX_REGEX_PCRE2_H_

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/status_or.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace perfetto {
namespace base {

struct Pcre2CodeDeleter {
  void operator()(pcre2_code* p) const { pcre2_code_free(p); }
};

struct Pcre2MatchDataDeleter {
  void operator()(pcre2_match_data* p) const { pcre2_match_data_free(p); }
};

using ScopedPcre2Code = std::unique_ptr<pcre2_code, Pcre2CodeDeleter>;
using ScopedPcre2MatchData =
    std::unique_ptr<pcre2_match_data, Pcre2MatchDataDeleter>;

// PCRE2 backend for Regex. Used on Android when the use_pcre2 flag is enabled.
// Header-only so the compiler can inline through unique_ptr<RegexImpl>.
class RegexPcre2 {
 public:
  RegexPcre2(RegexPcre2&&) noexcept = default;
  RegexPcre2& operator=(RegexPcre2&&) noexcept = default;
  ~RegexPcre2() = default;

  RegexPcre2(const RegexPcre2&) = delete;
  RegexPcre2& operator=(const RegexPcre2&) = delete;

  static StatusOr<RegexPcre2> Create(std::string_view pattern,
                                     bool case_insensitive) {
    RegexPcre2 result;
    int error_code;
    size_t error_offset;
    uint32_t pcre2_flags = 0;
    if (case_insensitive) {
      pcre2_flags |= PCRE2_CASELESS;
    }
    result.code_.reset(pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
        pcre2_flags, &error_code, &error_offset, nullptr));
    if (!result.code_) {
      PCRE2_UCHAR buffer[256];
      pcre2_get_error_message(error_code, buffer, sizeof(buffer));
      return ErrStatus("PCRE2 compile error at offset %zu: %s", error_offset,
                       reinterpret_cast<char*>(buffer));
    }
    // JIT-compile for faster matching. Non-fatal if JIT is unavailable.
    pcre2_jit_compile(result.code_.get(), PCRE2_JIT_COMPLETE);
    result.match_data_.reset(
        pcre2_match_data_create_from_pattern(result.code_.get(), nullptr));
    return std::move(result);
  }

  std::string GlobalReplace(std::string_view s,
                            std::string_view replacement) const {
    std::string out;
    size_t out_len = s.size() + replacement.size() * 2 + 64;
    auto do_substitute = [&](size_t* len) {
      out.resize(*len);
      return pcre2_substitute(
          code_.get(), reinterpret_cast<PCRE2_SPTR>(s.data()), s.size(),
          /*startoffset=*/0,
          PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH,
          /*match_data=*/nullptr, /*mcontext=*/nullptr,
          reinterpret_cast<PCRE2_SPTR>(replacement.data()), replacement.size(),
          reinterpret_cast<PCRE2_UCHAR*>(out.data()), len);
    };
    int rc = do_substitute(&out_len);
    if (rc == PCRE2_ERROR_NOMEMORY) {
      // PCRE2_SUBSTITUTE_OVERFLOW_LENGTH sets out_len to the required size.
      rc = do_substitute(&out_len);
    }
    if (rc >= 0) {
      out.resize(out_len);
      return out;
    }
    return std::string(s);
  }

  bool FullMatch(std::string_view s) const {
    int rc = pcre2_match(code_.get(), reinterpret_cast<PCRE2_SPTR>(s.data()),
                         s.size(),
                         /*startoffset=*/0, PCRE2_ANCHORED | PCRE2_ENDANCHORED,
                         match_data_.get(), /*mcontext=*/nullptr);
    return rc >= 0;
  }

  bool FullMatchWithGroups(std::string_view s,
                           std::vector<std::string_view>& out) const {
    out.clear();
    int rc = pcre2_match(code_.get(), reinterpret_cast<PCRE2_SPTR>(s.data()),
                         s.size(),
                         /*startoffset=*/0, PCRE2_ANCHORED | PCRE2_ENDANCHORED,
                         match_data_.get(), /*mcontext=*/nullptr);
    if (rc <= 0)
      return false;
    FillGroups(s, out);
    return true;
  }

  bool PartialMatch(std::string_view s) const {
    int rc = pcre2_match(code_.get(), reinterpret_cast<PCRE2_SPTR>(s.data()),
                         s.size(),
                         /*startoffset=*/0, /*options=*/0, match_data_.get(),
                         /*mcontext=*/nullptr);
    return rc >= 0;
  }

  bool PartialMatchWithGroups(std::string_view s,
                              std::vector<std::string_view>& out) const {
    out.clear();
    int rc = pcre2_match(code_.get(), reinterpret_cast<PCRE2_SPTR>(s.data()),
                         s.size(),
                         /*startoffset=*/0, /*options=*/0, match_data_.get(),
                         /*mcontext=*/nullptr);
    if (rc <= 0)
      return false;
    FillGroups(s, out);
    return true;
  }

  bool SearchWithOffset(std::string_view s,
                        size_t start_offset,
                        std::vector<std::string_view>& out) const {
    out.clear();
    if (start_offset > s.size())
      return false;
    int rc =
        pcre2_match(code_.get(), reinterpret_cast<PCRE2_SPTR>(s.data()),
                    s.size(), start_offset, /*options=*/0, match_data_.get(),
                    /*mcontext=*/nullptr);
    if (rc <= 0)
      return false;
    FillGroups(s, out);
    return true;
  }

 private:
  RegexPcre2() = default;

  void FillGroups(std::string_view s,
                  std::vector<std::string_view>& out) const {
    uint32_t count = pcre2_get_ovector_count(match_data_.get());
    size_t* ovector = pcre2_get_ovector_pointer(match_data_.get());
    for (uint32_t i = 0; i < count; ++i) {
      if (ovector[2 * i] == std::numeric_limits<PCRE2_SIZE>::max()) {
        out.emplace_back();
      } else {
        out.emplace_back(s.data() + ovector[2 * i],
                         ovector[2 * i + 1] - ovector[2 * i]);
      }
    }
  }

  ScopedPcre2Code code_;
  // pcre2_match() requires a non-null match_data even when capture groups
  // aren't needed (FullMatch, PartialMatch). A separate minimal match_data
  // with ovecsize=1 could be used for those cases, but the savings are
  // negligible: PCRE2's internal backtracking frame size depends on the
  // pattern's capture groups regardless of ovector size. We reuse a single
  // cached match_data created from the pattern to avoid per-call allocation.
  mutable ScopedPcre2MatchData match_data_;
};

}  // namespace base
}  // namespace perfetto

#endif  // SRC_BASE_REGEX_REGEX_PCRE2_H_
