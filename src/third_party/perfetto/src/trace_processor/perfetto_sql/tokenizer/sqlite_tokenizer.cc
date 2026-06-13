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

#include "src/trace_processor/perfetto_sql/tokenizer/sqlite_tokenizer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "perfetto/base/logging.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"
#include "src/trace_processor/sqlite/sql_source.h"

namespace perfetto::trace_processor {

// Verify that the sql_token constants in the header match the generated values.
static_assert(sql_token::kId == SYNTAQLITE_TK_ID);
static_assert(sql_token::kSemi == SYNTAQLITE_TK_SEMI);
static_assert(sql_token::kLp == SYNTAQLITE_TK_LP);
static_assert(sql_token::kRp == SYNTAQLITE_TK_RP);
static_assert(sql_token::kComma == SYNTAQLITE_TK_COMMA);
static_assert(sql_token::kVariable == SYNTAQLITE_TK_VARIABLE);
static_assert(sql_token::kSelect == SYNTAQLITE_TK_SELECT);
static_assert(sql_token::kFrom == SYNTAQLITE_TK_FROM);
static_assert(sql_token::kStar == SYNTAQLITE_TK_STAR);
static_assert(sql_token::kSpace == SYNTAQLITE_TK_SPACE);
static_assert(sql_token::kComment == SYNTAQLITE_TK_COMMENT);
static_assert(sql_token::kIllegal == SYNTAQLITE_TK_ILLEGAL);
static_assert(sql_token::kBang == SYNTAQLITE_TK_BANG);

SqliteTokenizer::SqliteTokenizer(SqlSource sql) : source_(std::move(sql)) {
  tok_ = syntaqlite_tokenizer_create_with_dialect(
      nullptr, syntaqlite_perfetto_dialect());
  PERFETTO_CHECK(tok_ != nullptr);
  syntaqlite_tokenizer_reset(tok_, source_.sql().data(),
                             static_cast<uint32_t>(source_.sql().size()));
}

SqliteTokenizer::~SqliteTokenizer() {
  syntaqlite_tokenizer_destroy(tok_);
}

SqliteTokenizer::Token SqliteTokenizer::Next() {
  SyntaqliteToken st;
  if (!syntaqlite_tokenizer_next(tok_, &st)) {
    // Point past the end of the source string so AsTraceback still works.
    const char* end = source_.sql().data() + source_.sql().size();
    return Token{std::string_view(end, 0), sql_token::kIllegal};
  }
  return Token{std::string_view(st.text, st.length), static_cast<int>(st.type)};
}

SqliteTokenizer::Token SqliteTokenizer::NextNonWhitespace() {
  Token t;
  for (t = Next();
       t.token_type == sql_token::kSpace || t.token_type == sql_token::kComment;
       t = Next()) {
  }
  return t;
}

SqliteTokenizer::Token SqliteTokenizer::NextTerminal() {
  Token tok = Next();
  while (!tok.IsTerminal()) {
    tok = Next();
  }
  return tok;
}

SqlSource SqliteTokenizer::Substr(const Token& start,
                                  const Token& end,
                                  EndToken end_token) const {
  auto offset = static_cast<uint32_t>(start.str.data() - source_.sql().c_str());
  const char* e =
      end.str.data() +
      (end_token == SqliteTokenizer::EndToken::kInclusive ? end.str.size() : 0);
  auto len = static_cast<uint32_t>(e - start.str.data());
  return source_.Substr(offset, len);
}

SqlSource SqliteTokenizer::SubstrToken(const Token& token) const {
  auto offset = static_cast<uint32_t>(token.str.data() - source_.sql().c_str());
  auto len = static_cast<uint32_t>(token.str.size());
  return source_.Substr(offset, len);
}

std::string SqliteTokenizer::AsTraceback(const Token& token) const {
  PERFETTO_CHECK(source_.sql().c_str() <= token.str.data());
  PERFETTO_CHECK(token.str.data() <=
                 source_.sql().c_str() + source_.sql().size());
  auto offset = static_cast<uint32_t>(token.str.data() - source_.sql().c_str());
  return source_.AsTraceback(offset);
}

void SqliteTokenizer::Rewrite(SqlSource::Rewriter& rewriter,
                              const Token& start,
                              const Token& end,
                              SqlSource rewrite,
                              EndToken end_token) const {
  auto s_off = static_cast<uint32_t>(start.str.data() - source_.sql().c_str());
  auto e_off = static_cast<uint32_t>(end.str.data() - source_.sql().c_str());
  uint32_t e_diff = end_token == EndToken::kInclusive
                        ? static_cast<uint32_t>(end.str.size())
                        : 0;
  rewriter.Rewrite(s_off, e_off + e_diff, std::move(rewrite));
}

void SqliteTokenizer::RewriteToken(SqlSource::Rewriter& rewriter,
                                   const Token& token,
                                   SqlSource rewrite) const {
  auto s_off = static_cast<uint32_t>(token.str.data() - source_.sql().c_str());
  auto e_off = static_cast<uint32_t>(token.str.data() + token.str.size() -
                                     source_.sql().c_str());
  rewriter.Rewrite(s_off, e_off, std::move(rewrite));
}

void SqliteTokenizer::Reset(SqlSource source) {
  source_ = std::move(source);
  syntaqlite_tokenizer_reset(tok_, source_.sql().data(),
                             static_cast<uint32_t>(source_.sql().size()));
}

}  // namespace perfetto::trace_processor
