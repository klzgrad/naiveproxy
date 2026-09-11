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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_PARSER_INTRINSIC_MACRO_EXPANSION_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_PARSER_INTRINSIC_MACRO_EXPANSION_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"

namespace perfetto::trace_processor::perfetto_sql {

enum class ExpandStatus : uint8_t {
  // Name matched an intrinsic and `body()` holds the expansion text.
  kExpanded,
  // Name is not an intrinsic; caller should try the user macro registry.
  kNotIntrinsic,
  // Name matched an intrinsic but expansion failed (e.g. wrong arg count
  // or malformed token list); caller should surface a failure to the
  // syntaqlite parser.
  kExpansionFailed,
};

// Expander for PerfettoSQL-builtin intrinsic macros (`__intrinsic_stringify`,
// `__intrinsic_token_apply{,_prefix,_and,_and_prefix}`). These are
// preprocessor-compat shims that operate on raw token text rather than via
// the syntaqlite $param mechanism.
//
// Holds reusable scratch state (tokenizer, segment vectors, body buffer) so
// that repeated expansions inside a single parse don't allocate.
class IntrinsicMacroExpander {
 public:
  IntrinsicMacroExpander();
  ~IntrinsicMacroExpander();

  IntrinsicMacroExpander(const IntrinsicMacroExpander&) = delete;
  IntrinsicMacroExpander& operator=(const IntrinsicMacroExpander&) = delete;
  IntrinsicMacroExpander(IntrinsicMacroExpander&&) = delete;
  IntrinsicMacroExpander& operator=(IntrinsicMacroExpander&&) = delete;

  // Tries to expand. On `kExpanded`, the expansion text is available via
  // `body()` until the next call to `TryExpand`.
  ExpandStatus TryExpand(std::string_view name,
                         const SyntaqliteToken* args,
                         uint32_t arg_count);

  std::string_view body() const { return body_; }

 private:
  // Splits a parenthesised list at the top level into views over `raw`.
  // Returns false on malformed input. `out` is cleared on entry.
  bool SplitParenList(std::string_view raw, std::vector<std::string_view>* out);

  // Builds a token_apply body into `body_`. See file comment for shape.
  bool BuildApply(const SyntaqliteToken* args,
                  uint32_t arg_count,
                  std::string_view joiner,
                  bool prefix);

  SyntaqliteTokenizer* tok_ = nullptr;
  std::vector<std::string_view> xs_;
  std::vector<std::string_view> ys_;
  std::string body_;
};

}  // namespace perfetto::trace_processor::perfetto_sql

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_PARSER_INTRINSIC_MACRO_EXPANSION_H_
