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

#include "src/trace_processor/perfetto_sql/parser/intrinsic_macro_expansion.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"

namespace perfetto::trace_processor::perfetto_sql {
namespace {

bool IsSkippable(uint32_t type) {
  return type == SYNTAQLITE_TK_SPACE || type == SYNTAQLITE_TK_COMMENT;
}

bool NextNonWs(SyntaqliteTokenizer* tok, SyntaqliteToken* out) {
  while (syntaqlite_tokenizer_next(tok, out)) {
    if (!IsSkippable(out->type))
      return true;
  }
  return false;
}

}  // namespace

IntrinsicMacroExpander::IntrinsicMacroExpander() {
  tok_ = syntaqlite_tokenizer_create_with_dialect(
      nullptr, syntaqlite_perfetto_dialect());
  PERFETTO_CHECK(tok_ != nullptr);
}

IntrinsicMacroExpander::~IntrinsicMacroExpander() {
  syntaqlite_tokenizer_destroy(tok_);
}

// Splits a parenthesised list like `(a, b, (c, d), e)` on top-level commas
// using the syntaqlite tokenizer, so that commas inside string literals
// (`'a,b'`), double-quoted identifiers (`"a,b"`), bracketed identifiers
// (`[a,b]`), backticked identifiers (`` `a,b` ``) and nested parens do not
// split. `()` yields an empty vector. Returns false on malformed input
// (missing outer parens, unbalanced parens, empty elements like `(,)`,
// or trailing junk after the closing paren).
bool IntrinsicMacroExpander::SplitParenList(
    std::string_view raw,
    std::vector<std::string_view>* out) {
  out->clear();
  syntaqlite_tokenizer_reset(tok_, raw.data(),
                             static_cast<uint32_t>(raw.size()));

  SyntaqliteToken t;
  if (!NextNonWs(tok_, &t) || t.type != SYNTAQLITE_TK_LP)
    return false;

  const char* seg_start = nullptr;
  const char* seg_end = nullptr;
  int depth = 0;
  while (NextNonWs(tok_, &t)) {
    if (t.type == SYNTAQLITE_TK_RP && depth == 0) {
      if (seg_start) {
        out->emplace_back(seg_start, static_cast<size_t>(seg_end - seg_start));
      }
      // Reject any trailing tokens after the closing paren.
      return !NextNonWs(tok_, &t);
    }
    if (t.type == SYNTAQLITE_TK_COMMA && depth == 0) {
      if (!seg_start)
        return false;
      out->emplace_back(seg_start, static_cast<size_t>(seg_end - seg_start));
      seg_start = nullptr;
      continue;
    }
    if (t.type == SYNTAQLITE_TK_LP) {
      ++depth;
    } else if (t.type == SYNTAQLITE_TK_RP) {
      --depth;
    }
    if (!seg_start)
      seg_start = t.text;
    seg_end = t.text + t.length;
  }
  return false;
}

// Builds the body of a token_apply invocation into `body_`. Accepts either
// `apply!(macro, (a, b, c))` or `apply!(macro, (a, b), (c, d))`: emits
// `macro!(a)`/`macro!(a, c)` etc joined by `joiner` (and optionally
// prefixed with `joiner` when the result is non-empty).
bool IntrinsicMacroExpander::BuildApply(const SyntaqliteToken* args,
                                        uint32_t arg_count,
                                        std::string_view joiner,
                                        bool prefix) {
  if (arg_count != 2 && arg_count != 3)
    return false;
  if (!SplitParenList({args[1].text, args[1].length}, &xs_))
    return false;
  if (arg_count == 3) {
    if (!SplitParenList({args[2].text, args[2].length}, &ys_))
      return false;
  } else {
    ys_.clear();
  }
  size_t n = arg_count == 3 ? std::min(xs_.size(), ys_.size()) : xs_.size();
  std::string_view macro =
      base::TrimWhitespace(std::string_view(args[0].text, args[0].length));
  body_.clear();
  for (size_t i = 0; i < n; ++i) {
    if (i > 0 || prefix)
      body_.append(joiner);
    body_.append(macro);
    body_.append("!(");
    body_.append(xs_[i]);
    if (arg_count == 3) {
      body_.append(", ");
      body_.append(ys_[i]);
    }
    body_.push_back(')');
  }
  return true;
}

ExpandStatus IntrinsicMacroExpander::TryExpand(std::string_view name,
                                               const SyntaqliteToken* args,
                                               uint32_t arg_count) {
  if (name == "__intrinsic_stringify" ||
      name == "__intrinsic_stringify_ignore_table") {
    if (arg_count != 1)
      return ExpandStatus::kExpansionFailed;
    std::string_view arg =
        base::TrimWhitespace(std::string_view(args[0].text, args[0].length));
    body_.clear();
    body_.reserve(arg.size() + 2);
    body_.push_back('\'');
    body_.append(arg);
    body_.push_back('\'');
    return ExpandStatus::kExpanded;
  }

  struct ApplyVariant {
    std::string_view name;
    std::string_view joiner;
    bool prefix;
  };
  static constexpr std::array<ApplyVariant, 4> kVariants{{
      {"__intrinsic_token_apply", ", ", false},
      {"__intrinsic_token_apply_prefix", ", ", true},
      {"__intrinsic_token_apply_and", " AND ", false},
      {"__intrinsic_token_apply_and_prefix", " AND ", true},
  }};
  for (const auto& v : kVariants) {
    if (name != v.name)
      continue;
    return BuildApply(args, arg_count, v.joiner, v.prefix)
               ? ExpandStatus::kExpanded
               : ExpandStatus::kExpansionFailed;
  }
  return ExpandStatus::kNotIntrinsic;
}

}  // namespace perfetto::trace_processor::perfetto_sql
