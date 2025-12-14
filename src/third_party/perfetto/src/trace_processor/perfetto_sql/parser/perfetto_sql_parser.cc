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

#include "src/trace_processor/perfetto_sql/parser/perfetto_sql_parser.h"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/perfetto_sql/grammar/perfettosql_grammar_interface.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/perfetto_sql/preprocessor/perfetto_sql_preprocessor.h"
#include "src/trace_processor/perfetto_sql/tokenizer/sqlite_tokenizer.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/util/sql_argument.h"

namespace perfetto::trace_processor {

namespace {

using Token = SqliteTokenizer::Token;
using Statement = PerfettoSqlParser::Statement;

PerfettoSqlToken TokenToPerfettoSqlToken(const Token& token) {
  return PerfettoSqlToken{token.str.data(), token.str.size()};
}

Token PerfettoSqlTokenToToken(const PerfettoSqlToken& token) {
  return Token{std::string_view(token.ptr, token.n), 0};
}

}  // namespace

// Grammar interface implementation
extern "C" {

struct PerfettoSqlParserState {
  explicit PerfettoSqlParserState(
      SqlSource source,
      const base::FlatHashMap<std::string, PerfettoSqlPreprocessor::Macro>&
          macros)
      : tokenizer{SqlSource::FromTraceProcessorImplementation("")},
        preprocessor(std::move(source), macros) {}

  void ErrorAtToken(const char* msg, const PerfettoSqlToken& token) {
    status = base::ErrStatus(
        "%s%s", tokenizer.AsTraceback(PerfettoSqlTokenToToken(token)).c_str(),
        msg);
  }

  // Current statement being built
  std::optional<PerfettoSqlParser::Statement> current_statement;

  // Tokenizer for the current statement
  SqliteTokenizer tokenizer;

  // Preprocessor for handling SQL statements
  PerfettoSqlPreprocessor preprocessor;

  // Error handling
  base::Status status;
};

struct PerfettoSqlArgumentList {
  std::vector<sql_argument::ArgumentDefinition> inner;
};

struct PerfettoSqlIndexedColumnList {
  std::vector<std::string> cols;
};

struct PerfettoSqlMacroArgumentList {
  std::vector<std::pair<SqlSource, SqlSource>> args;
};

struct PerfettoSqlFnReturnType {
  bool is_table;
  sql_argument::Type scalar_type;
  std::vector<sql_argument::ArgumentDefinition> table_columns;
};

struct PerfettoSqlTableSchema {
  std::vector<sql_argument::ArgumentDefinition> columns;
  std::string description;
};

PerfettoSqlArgumentList* OnPerfettoSqlCreateOrAppendArgument(
    PerfettoSqlParserState* state,
    PerfettoSqlArgumentList* list,
    PerfettoSqlToken* name,
    PerfettoSqlToken* type) {
  std::unique_ptr<PerfettoSqlArgumentList> owned_list(list);
  if (!owned_list) {
    owned_list = std::make_unique<PerfettoSqlArgumentList>();
  }
  auto parsed = sql_argument::ParseType(base::StringView(type->ptr, type->n));
  if (!parsed) {
    state->ErrorAtToken("Failed to parse type", *type);
    return nullptr;
  }
  owned_list->inner.emplace_back("$" + std::string(name->ptr, name->n),
                                 *parsed);
  return owned_list.release();
}

void OnPerfettoSqlFreeArgumentList(PerfettoSqlParserState*,
                                   PerfettoSqlArgumentList* args) {
  std::unique_ptr<PerfettoSqlArgumentList> args_deleter(args);
}

PerfettoSqlIndexedColumnList* OnPerfettoSqlCreateOrAppendIndexedColumn(
    PerfettoSqlIndexedColumnList* list,
    PerfettoSqlToken* col) {
  std::unique_ptr<PerfettoSqlIndexedColumnList> owned_list(list);
  if (!owned_list) {
    owned_list = std::make_unique<PerfettoSqlIndexedColumnList>();
  }
  owned_list->cols.emplace_back(col->ptr, col->n);
  return owned_list.release();
}

void OnPerfettoSqlFreeIndexedColumnList(PerfettoSqlParserState*,
                                        PerfettoSqlIndexedColumnList* cols) {
  std::unique_ptr<PerfettoSqlIndexedColumnList> cols_deleter(cols);
}

PerfettoSqlMacroArgumentList* OnPerfettoSqlCreateOrAppendMacroArgument(
    PerfettoSqlParserState* state,
    PerfettoSqlMacroArgumentList* list,
    PerfettoSqlToken* name,
    PerfettoSqlToken* type) {
  std::unique_ptr<PerfettoSqlMacroArgumentList> owned_list(list);
  if (!owned_list) {
    owned_list = std::make_unique<PerfettoSqlMacroArgumentList>();
  }
  owned_list->args.emplace_back(
      state->tokenizer.SubstrToken(PerfettoSqlTokenToToken(*name)),
      state->tokenizer.SubstrToken(PerfettoSqlTokenToToken(*type)));
  return owned_list.release();
}

void OnPerfettoSqlFreeMacroArgumentList(PerfettoSqlParserState*,
                                        PerfettoSqlMacroArgumentList* list) {
  std::unique_ptr<PerfettoSqlMacroArgumentList> list_deleter(list);
}

void OnPerfettoSqlSyntaxError(PerfettoSqlParserState* state,
                              PerfettoSqlToken* token) {
  if (token->n == 0) {
    state->ErrorAtToken("incomplete input", *token);
  } else {
    state->ErrorAtToken("syntax error", *token);
  }
}

PerfettoSqlFnReturnType* OnPerfettoSqlCreateScalarReturnType(
    PerfettoSqlToken* type) {
  auto res = std::make_unique<PerfettoSqlFnReturnType>();
  res->is_table = false;
  auto parsed = sql_argument::ParseType(base::StringView(type->ptr, type->n));
  if (!parsed) {
    return nullptr;
  }
  res->scalar_type = *parsed;
  return res.release();
}

PerfettoSqlFnReturnType* OnPerfettoSqlCreateTableReturnType(
    PerfettoSqlArgumentList* args) {
  std::unique_ptr<PerfettoSqlArgumentList> args_deleter(args);
  auto res = std::make_unique<PerfettoSqlFnReturnType>();
  res->is_table = true;
  res->table_columns = std::move(args->inner);
  return res.release();
}

void OnPerfettoSqlFnFreeReturnType(PerfettoSqlParserState*,
                                   PerfettoSqlFnReturnType* type) {
  std::unique_ptr<PerfettoSqlFnReturnType> type_deleter(type);
}

void OnPerfettoSqlCreateFunction(PerfettoSqlParserState* state,
                                 int replace,
                                 PerfettoSqlToken* name,
                                 PerfettoSqlArgumentList* args,
                                 PerfettoSqlFnReturnType* returns,
                                 PerfettoSqlToken* body_start,
                                 PerfettoSqlToken* body_end) {
  std::unique_ptr<PerfettoSqlArgumentList> args_deleter(args);
  std::unique_ptr<PerfettoSqlFnReturnType> returns_deleter(returns);

  // Convert the return type
  PerfettoSqlParser::CreateFunction::Returns returns_res;
  returns_res.is_table = returns->is_table;
  if (returns->is_table) {
    returns_res.table_columns = std::move(returns->table_columns);
  } else {
    returns_res.scalar_type = returns->scalar_type;
  }

  // Create a new CreateFunction statement
  state->current_statement = PerfettoSqlParser::CreateFunction{
      replace != 0,
      FunctionPrototype{
          std::string(name->ptr, name->n),
          args ? std::move(args->inner)
               : std::vector<sql_argument::ArgumentDefinition>{},
      },
      std::move(returns_res),
      state->tokenizer.Substr(PerfettoSqlTokenToToken(*body_start),
                              PerfettoSqlTokenToToken(*body_end),
                              SqliteTokenizer::EndToken::kInclusive),
      "",
      std::nullopt,  // No target function for SQL functions
  };
}

void OnPerfettoSqlCreateDelegatingFunction(PerfettoSqlParserState* state,
                                           int replace,
                                           PerfettoSqlToken* name,
                                           PerfettoSqlArgumentList* args,
                                           PerfettoSqlFnReturnType* returns,
                                           PerfettoSqlToken* target_function,
                                           PerfettoSqlToken* /*stmt_end*/) {
  std::unique_ptr<PerfettoSqlArgumentList> args_deleter(args);
  std::unique_ptr<PerfettoSqlFnReturnType> returns_deleter(returns);

  // Validate the target function name is not empty
  if (target_function->n == 0) {
    state->ErrorAtToken("Target function name cannot be empty",
                        *target_function);
    return;
  }

  // Convert the return type
  PerfettoSqlParser::CreateFunction::Returns returns_res;
  returns_res.is_table = returns->is_table;
  if (returns->is_table) {
    returns_res.table_columns = std::move(returns->table_columns);
  } else {
    returns_res.scalar_type = returns->scalar_type;
  }

  // Create a new CreateFunction statement with intrinsic name
  state->current_statement = PerfettoSqlParser::CreateFunction{
      replace != 0,
      FunctionPrototype{
          std::string(name->ptr, name->n),
          args ? std::move(args->inner)
               : std::vector<sql_argument::ArgumentDefinition>{},
      },
      std::move(returns_res),
      SqlSource::FromTraceProcessorImplementation(
          ""),  // Empty SQL source for delegating functions
      "",       // Empty description for now
      std::string(target_function->ptr,
                  target_function->n),  // Set target function name
  };
}

void OnPerfettoSqlCreateTable(PerfettoSqlParserState* state,
                              int replace,
                              PerfettoSqlToken* name,
                              PerfettoSqlToken* table_impl,
                              PerfettoSqlArgumentList* args,
                              PerfettoSqlToken* body_start,
                              PerfettoSqlToken* body_end) {
  std::unique_ptr<PerfettoSqlArgumentList> args_deleter(args);
  if (table_impl->n == 0 ||
      base::CaseInsensitiveEqual(std::string(table_impl->ptr, table_impl->n),
                                 "dataframe")) {
    // Do nothing.
  } else {
    state->ErrorAtToken("Invalid table implementation", *table_impl);
    return;
  }
  state->current_statement = PerfettoSqlParser::CreateTable{
      replace != 0,
      std::string(name->ptr, name->n),
      args ? std::move(args->inner)
           : std::vector<sql_argument::ArgumentDefinition>{},
      state->tokenizer.Substr(PerfettoSqlTokenToToken(*body_start),
                              PerfettoSqlTokenToToken(*body_end)),
  };
}

void OnPerfettoSqlCreateView(PerfettoSqlParserState* state,
                             int replace,
                             PerfettoSqlToken* create_token,
                             PerfettoSqlToken* name,
                             PerfettoSqlArgumentList* args,
                             PerfettoSqlToken* body_start,
                             PerfettoSqlToken* body_end) {
  std::unique_ptr<PerfettoSqlArgumentList> args_deleter(args);

  SqlSource header = SqlSource::FromTraceProcessorImplementation(
      "CREATE VIEW " + std::string(name->ptr, name->n) + " AS ");
  SqlSource::Rewriter rewriter(state->preprocessor.statement());
  state->tokenizer.Rewrite(rewriter, PerfettoSqlTokenToToken(*create_token),
                           PerfettoSqlTokenToToken(*body_start), header);

  state->current_statement = PerfettoSqlParser::CreateView{
      replace != 0,
      std::string(name->ptr, name->n),
      args ? std::move(args->inner)
           : std::vector<sql_argument::ArgumentDefinition>(),
      state->tokenizer.Substr(PerfettoSqlTokenToToken(*body_start),
                              PerfettoSqlTokenToToken(*body_end)),
      std::move(rewriter).Build(),
  };
}

void OnPerfettoSqlCreateIndex(PerfettoSqlParserState* state,
                              int replace,
                              PerfettoSqlToken* create_token,
                              PerfettoSqlToken* name,
                              PerfettoSqlToken* table_name,
                              PerfettoSqlIndexedColumnList* cols) {
  std::unique_ptr<PerfettoSqlIndexedColumnList> cols_deleter(cols);

  SqlSource header = SqlSource::FromTraceProcessorImplementation(
      "CREATE INDEX " + std::string(name->ptr, name->n));
  SqlSource::Rewriter rewriter(state->preprocessor.statement());
  state->tokenizer.Rewrite(rewriter, PerfettoSqlTokenToToken(*create_token),
                           PerfettoSqlTokenToToken(*create_token), header,
                           SqliteTokenizer::EndToken::kExclusive);

  state->current_statement = PerfettoSqlParser::CreateIndex{
      replace != 0,
      std::string(name->ptr, name->n),
      std::string(table_name->ptr, table_name->n),
      std::move(cols->cols),
  };
}

void OnPerfettoSqlDropIndex(PerfettoSqlParserState* state,
                            PerfettoSqlToken* name,
                            PerfettoSqlToken* table_name) {
  state->current_statement = PerfettoSqlParser::DropIndex{
      std::string(name->ptr, name->n),
      std::string(table_name->ptr, table_name->n),
  };
}

void OnPerfettoSqlCreateMacro(PerfettoSqlParserState* state,
                              int replace,
                              PerfettoSqlToken* name,
                              PerfettoSqlMacroArgumentList* args,
                              PerfettoSqlToken* returns,
                              PerfettoSqlToken* body_start,
                              PerfettoSqlToken* body_end) {
  std::unique_ptr<PerfettoSqlMacroArgumentList> args_deleter(args);

  state->current_statement = PerfettoSqlParser::CreateMacro{
      replace != 0,
      state->tokenizer.SubstrToken(PerfettoSqlTokenToToken(*name)),
      args ? std::move(args->args)
           : std::vector<std::pair<SqlSource, SqlSource>>{},

      state->tokenizer.SubstrToken(PerfettoSqlTokenToToken(*returns)),
      state->tokenizer.Substr(PerfettoSqlTokenToToken(*body_start),
                              PerfettoSqlTokenToToken(*body_end)),
  };
}

void OnPerfettoSqlInclude(PerfettoSqlParserState* state,
                          PerfettoSqlToken* module_name) {
  state->current_statement =
      PerfettoSqlParser::Include{std::string(module_name->ptr, module_name->n)};
}

}  // extern "C"

PerfettoSqlParser::PerfettoSqlParser(
    SqlSource source,
    const base::FlatHashMap<std::string, PerfettoSqlPreprocessor::Macro>&
        macros)
    : parser_state_(std::make_unique<PerfettoSqlParserState>(std::move(source),
                                                             macros)) {}

PerfettoSqlParser::~PerfettoSqlParser() = default;

bool PerfettoSqlParser::Next() {
  PERFETTO_DCHECK(parser_state_->status.ok());

  parser_state_->current_statement = std::nullopt;
  statement_sql_ = std::nullopt;

  if (!parser_state_->preprocessor.NextStatement()) {
    parser_state_->status = parser_state_->preprocessor.status();
    return false;
  }
  parser_state_->tokenizer.Reset(parser_state_->preprocessor.statement());

  auto* parser = PerfettoSqlParseAlloc(malloc, parser_state_.get());
  auto guard = base::OnScopeExit([&]() { PerfettoSqlParseFree(parser, free); });

  enum { kEof, kSemicolon, kNone } eof = kNone;
  for (Token token = parser_state_->tokenizer.Next();;
       token = parser_state_->tokenizer.Next()) {
    if (!parser_state_->status.ok()) {
      return false;
    }
    if (token.IsTerminal()) {
      if (eof == kNone) {
        PerfettoSqlParse(parser, TK_SEMI, TokenToPerfettoSqlToken(token));
        eof = kSemicolon;
        continue;
      }
      if (eof == kSemicolon) {
        PerfettoSqlParse(parser, 0, TokenToPerfettoSqlToken(token));
        eof = kEof;
        continue;
      }
      if (!parser_state_->current_statement) {
        parser_state_->current_statement = SqliteSql{};
      }
      statement_sql_ = parser_state_->preprocessor.statement();
      return true;
    }
    if (token.token_type == TK_SPACE || token.token_type == TK_COMMENT) {
      continue;
    }
    PerfettoSqlParse(parser, token.token_type, TokenToPerfettoSqlToken(token));
  }
}

const Statement& PerfettoSqlParser::statement() const {
  PERFETTO_DCHECK(parser_state_->current_statement.has_value());
  return *parser_state_->current_statement;
}

const base::Status& PerfettoSqlParser::status() const {
  return parser_state_->status;
}

}  // namespace perfetto::trace_processor
