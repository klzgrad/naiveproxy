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
#include "src/trace_processor/sqlite/sql_source.h"

namespace perfetto::trace_processor {
extern "C" {
int sqlite3GetToken(const unsigned char* z, int* tokenType);
int sqliteTokenizeInternalAnalyzeWindowKeyword(const unsigned char* z);
int sqliteTokenizeInternalAnalyzeOverKeyword(const unsigned char* z,
                                             int lastToken);
int sqliteTokenizeInternalAnalyzeFilterKeyword(const unsigned char* z,
                                               int lastToken);
}

SqliteTokenizer::SqliteTokenizer(SqlSource sql) : source_(std::move(sql)) {}

SqliteTokenizer::Token SqliteTokenizer::Next() {
  Token token;
  const char* start = source_.sql().data() + offset_;
  int n = sqlite3GetToken(reinterpret_cast<const unsigned char*>(start),
                          &token.token_type);
  if (token.token_type == TK_WINDOW) {
    token.token_type = sqliteTokenizeInternalAnalyzeWindowKeyword(
        reinterpret_cast<const unsigned char*>(start + n));
  } else if (token.token_type == TK_OVER) {
    token.token_type = sqliteTokenizeInternalAnalyzeOverKeyword(
        reinterpret_cast<const unsigned char*>(start + n),
        last_non_space_token_);
  } else if (token.token_type == TK_FILTER) {
    token.token_type = sqliteTokenizeInternalAnalyzeFilterKeyword(
        reinterpret_cast<const unsigned char*>(start + n),
        last_non_space_token_);
  }
  offset_ += static_cast<uint32_t>(n);
  token.str = std::string_view(start, static_cast<uint32_t>(n));
  if (token.token_type != TK_SPACE && token.token_type != TK_COMMENT) {
    last_non_space_token_ = token.token_type;
  }
  return token;
}

SqliteTokenizer::Token SqliteTokenizer::NextNonWhitespace() {
  Token t;
  for (t = Next(); t.token_type == TK_SPACE || t.token_type == TK_COMMENT;
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

}  // namespace perfetto::trace_processor
