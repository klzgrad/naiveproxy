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

#include "src/trace_processor/perfetto_sql/preprocessor/perfetto_sql_preprocessor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/perfetto_sql/preprocessor/preprocessor_grammar_interface.h"
#include "src/trace_processor/perfetto_sql/tokenizer/sqlite_tokenizer.h"
#include "src/trace_processor/sqlite/sql_source.h"

namespace perfetto::trace_processor {
namespace {

using State = PreprocessorGrammarState;

struct Preprocessor {
 public:
  explicit Preprocessor(State* state)
      : parser_(PreprocessorGrammarParseAlloc(malloc, state)) {}
  ~Preprocessor() { PreprocessorGrammarParseFree(parser_, free); }

  void Parse(int token_type, PreprocessorGrammarToken token) {
    PreprocessorGrammarParse(parser_, token_type, token);
  }

 private:
  void* parser_;
};

struct Stringify {
  bool ignore_table;
};
struct Apply {
  int join_token;
  int prefix_token;
};
using MacroImpl =
    std::variant<PerfettoSqlPreprocessor::Macro*, Stringify, Apply>;

// Synthetic "stackframe" representing the processing of a single piece of SQL.
struct Frame {
  struct Root {};
  struct Rewrite {
    SqliteTokenizer& tokenizer;
    SqlSource::Rewriter& rewriter;
    SqliteTokenizer::Token start;
    SqliteTokenizer::Token end;
  };
  struct Append {
    std::vector<SqlSource>& result;
  };
  using Type = std::variant<Root, Rewrite, Append>;
  struct ActiveMacro {
    std::string name;
    MacroImpl impl;
    std::vector<SqlSource> args;
    uint32_t nested_macro_count;
    std::unordered_set<std::string> seen_variables;
    std::unordered_set<std::string> expanded_variables;
  };
  enum VariableHandling { kLookup, kLookupOrIgnore, kIgnore };

  explicit Frame(Type _type,
                 VariableHandling _var_handling,
                 State* s,
                 const SqlSource& source)
      : type(_type),
        var_handling(_var_handling),
        preprocessor(s),
        tokenizer(source),
        rewriter(source),
        substitutions(&owned_substitutions) {}
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;
  Frame(Frame&&) = delete;
  Frame& operator=(Frame&&) = delete;

  Type type;
  VariableHandling var_handling;
  Preprocessor preprocessor;
  SqliteTokenizer tokenizer;

  bool seen_semicolon = false;
  SqlSource::Rewriter rewriter;
  bool ignore_rewrite = false;

  std::optional<ActiveMacro> active_macro;

  base::FlatHashMap<std::string, SqlSource> owned_substitutions;
  base::FlatHashMap<std::string, SqlSource>* substitutions;
};

struct ErrorToken {
  SqliteTokenizer::Token token;
  std::string message;
};

extern "C" struct PreprocessorGrammarState {
  std::list<Frame> stack;
  const base::FlatHashMap<std::string, PerfettoSqlPreprocessor::Macro>& macros;
  std::optional<ErrorToken> error;
};

extern "C" struct PreprocessorGrammarApplyList {
  std::vector<PreprocessorGrammarTokenBounds> args;
};

SqliteTokenizer::Token GrammarTokenToTokenizerToken(
    const PreprocessorGrammarToken& token) {
  return SqliteTokenizer::Token{std::string_view(token.ptr, token.n),
                                TK_ILLEGAL};
}

base::Status ErrorAtToken(const SqliteTokenizer& tokenizer,
                          const SqliteTokenizer::Token& token,
                          const char* error) {
  std::string traceback = tokenizer.AsTraceback(token);
  return base::ErrStatus("%s%s", traceback.c_str(), error);
}

std::vector<std::string> SqlSourceVectorToString(
    const std::vector<SqlSource>& vec) {
  std::vector<std::string> pieces;
  pieces.reserve(vec.size());
  for (const auto& list : vec) {
    pieces.emplace_back(list.sql());
  }
  return pieces;
}

std::string_view BoundsToStringView(const PreprocessorGrammarTokenBounds& b) {
  return {b.start.ptr, static_cast<size_t>(b.end.ptr + b.end.n - b.start.ptr)};
}

void RewriteIntrinsicMacro(Frame& frame,
                           SqliteTokenizer::Token name,
                           SqliteTokenizer::Token rp) {
  const auto& macro = *frame.active_macro;
  frame.tokenizer.Rewrite(
      frame.rewriter, name, rp,
      SqlSource::FromTraceProcessorImplementation(
          macro.name + "!(" +
          base::Join(SqlSourceVectorToString(macro.args), ", ") + ")"),
      SqliteTokenizer::EndToken::kInclusive);
}

void ExecuteSqlMacro(State* state,
                     Frame& frame,
                     Frame::ActiveMacro& macro,
                     SqliteTokenizer::Token name,
                     SqliteTokenizer::Token rp) {
  auto& sql_macro = std::get<PerfettoSqlPreprocessor::Macro*>(macro.impl);
  if (macro.args.size() != sql_macro->args.size()) {
    state->error = ErrorToken{
        name,
        base::ErrStatus(
            "wrong number of macro arguments, expected %zu actual %zu",
            sql_macro->args.size(), macro.args.size())
            .message(),
    };
    return;
  }
  // TODO(lalitm): switch back to kLookup once we have proper parser support.
  state->stack.emplace_back(
      Frame::Rewrite{frame.tokenizer, frame.rewriter, name, rp},
      Frame::kLookupOrIgnore, state, sql_macro->sql);
  auto& macro_frame = state->stack.back();
  for (uint32_t i = 0; i < sql_macro->args.size(); ++i) {
    macro_frame.owned_substitutions.Insert(sql_macro->args[i],
                                           std::move(macro.args[i]));
  }
}

void ExecuteStringify(State* state,
                      Frame& frame,
                      Frame::ActiveMacro& macro,
                      SqliteTokenizer::Token name,
                      SqliteTokenizer::Token rp) {
  auto& stringify = std::get<Stringify>(macro.impl);
  if (macro.args.size() != 1) {
    state->error = ErrorToken{
        name,
        base::ErrStatus(
            "stringify: must specify exactly 1 argument, actual %zu",
            macro.args.size())
            .message(),
    };
    return;
  }
  bool can_stringify_outer =
      macro.seen_variables.empty() ||
      (stringify.ignore_table && macro.seen_variables.size() == 1 &&
       macro.seen_variables.count("table"));
  if (!can_stringify_outer) {
    RewriteIntrinsicMacro(frame, name, rp);
    return;
  }
  if (!macro.expanded_variables.empty()) {
    state->stack.emplace_back(
        Frame::Rewrite{frame.tokenizer, frame.rewriter, name, rp},
        Frame::kIgnore, state,
        SqlSource::FromTraceProcessorImplementation(macro.name + "!(" +
                                                    macro.args[0].sql() + ")"));
    return;
  }
  auto res = SqlSource::FromTraceProcessorImplementation(
      "'" + macro.args[0].sql() + "'");
  frame.tokenizer.Rewrite(frame.rewriter, name, rp, std::move(res),
                          SqliteTokenizer::EndToken::kInclusive);
}

void ExecuteApply(State* state,
                  Frame& frame,
                  Frame::ActiveMacro& macro,
                  SqliteTokenizer::Token name,
                  SqliteTokenizer::Token rp) {
  auto& apply = std::get<Apply>(macro.impl);
  if (!macro.seen_variables.empty()) {
    RewriteIntrinsicMacro(frame, name, rp);
    return;
  }
  // Gross hack to detect if the argument to the macro is a variable. We cannot
  // use macro.expanded_variables because inside functions, we can have
  // variables which are intentionally never going to be expanded by the
  // preprocessor. That's OK to expand, as long as the entire macro argument is
  // itself not a variable.
  bool is_arg_variable = false;
  for (const auto& arg : macro.args) {
    if (!arg.sql().empty() && arg.sql()[0] == '$') {
      is_arg_variable = true;
      break;
    }
  }
  if (is_arg_variable) {
    state->stack.emplace_back(
        Frame::Rewrite{frame.tokenizer, frame.rewriter, name, rp},
        Frame::kIgnore, state,
        SqlSource::FromTraceProcessorImplementation(
            macro.name + "!(" +
            base::Join(SqlSourceVectorToString(macro.args), ", ") + ")"));
    return;
  }
  state->stack.emplace_back(
      Frame::Rewrite{frame.tokenizer, frame.rewriter, name, rp},
      Frame::VariableHandling::kIgnore, state,
      SqlSource::FromTraceProcessorImplementation(
          base::Join(SqlSourceVectorToString(macro.args), " ")));

  auto& expansion_frame = state->stack.back();
  expansion_frame.preprocessor.Parse(
      PPTK_APPLY, PreprocessorGrammarToken{nullptr, 0, PPTK_APPLY});
  expansion_frame.preprocessor.Parse(
      apply.join_token, PreprocessorGrammarToken{nullptr, 0, apply.join_token});
  expansion_frame.preprocessor.Parse(
      apply.prefix_token,
      PreprocessorGrammarToken{nullptr, 0, apply.prefix_token});
  expansion_frame.ignore_rewrite = true;
}

extern "C" void OnPreprocessorSyntaxError(State* state,
                                          PreprocessorGrammarToken* token) {
  state->error = {GrammarTokenToTokenizerToken(*token),
                  "preprocessor syntax error"};
}

extern "C" void OnPreprocessorApply(PreprocessorGrammarState* state,
                                    PreprocessorGrammarToken* name,
                                    PreprocessorGrammarToken* join,
                                    PreprocessorGrammarToken* prefix,
                                    PreprocessorGrammarApplyList* raw_a,
                                    PreprocessorGrammarApplyList* raw_b) {
  std::unique_ptr<PreprocessorGrammarApplyList> a(raw_a);
  std::unique_ptr<PreprocessorGrammarApplyList> b(raw_b);
  auto& frame = state->stack.back();
  size_t size = std::min(a->args.size(), b ? b->args.size() : a->args.size());
  if (size == 0) {
    auto& rewrite = std::get<Frame::Rewrite>(frame.type);
    rewrite.tokenizer.Rewrite(rewrite.rewriter, rewrite.start, rewrite.end,
                              SqlSource::FromTraceProcessorImplementation(""),
                              SqliteTokenizer::EndToken::kInclusive);
    return;
  }
  std::string macro(name->ptr, name->n);
  std::vector<std::string> args;
  for (uint32_t i = 0; i < size; ++i) {
    std::string arg = macro;
    arg.append("!(").append(BoundsToStringView(a->args[i]));
    if (b) {
      arg.append(",").append(BoundsToStringView(b->args[i]));
    }
    arg.append(")");
    args.emplace_back(std::move(arg));
  }
  std::string joiner = join->major == PPTK_AND ? " AND " : " , ";
  std::string res = prefix->major == PPTK_TRUE ? joiner : "";
  res.append(base::Join(args, joiner));
  state->stack.emplace_back(
      frame.type, Frame::VariableHandling::kLookupOrIgnore, state,
      SqlSource::FromTraceProcessorImplementation(std::move(res)));
}

extern "C" void OnPreprocessorVariable(State* state,
                                       PreprocessorGrammarToken* var) {
  if (var->n == 0 || var->ptr[0] != '$') {
    state->error = {GrammarTokenToTokenizerToken(*var),
                    "variable must start with '$'"};
    return;
  }
  auto& frame = state->stack.back();
  if (frame.active_macro) {
    std::string name(var->ptr + 1, var->n - 1);
    if (frame.substitutions->Find(name)) {
      frame.active_macro->expanded_variables.insert(name);
    } else {
      frame.active_macro->seen_variables.insert(name);
    }
    return;
  }
  switch (frame.var_handling) {
    case Frame::kLookup:
    case Frame::kLookupOrIgnore: {
      auto* it =
          frame.substitutions->Find(std::string(var->ptr + 1, var->n - 1));
      if (!it) {
        if (frame.var_handling == Frame::kLookup) {
          state->error = {GrammarTokenToTokenizerToken(*var),
                          "variable not defined"};
        }
        return;
      }
      frame.tokenizer.RewriteToken(frame.rewriter,
                                   GrammarTokenToTokenizerToken(*var), *it);
      break;
    }
    case Frame::kIgnore:
      break;
  }
}

extern "C" void OnPreprocessorMacroId(State* state,
                                      PreprocessorGrammarToken* name_tok) {
  auto& invocation = state->stack.back();
  if (invocation.active_macro) {
    invocation.active_macro->nested_macro_count++;
    return;
  }
  std::string name(name_tok->ptr, name_tok->n);
  MacroImpl impl;
  if (name == "__intrinsic_stringify") {
    impl = Stringify();
  } else if (name == "__intrinsic_stringify_ignore_table") {
    impl = Stringify{true};
  } else if (name == "__intrinsic_token_apply") {
    impl = Apply{PPTK_COMMA, PPTK_FALSE};
  } else if (name == "__intrinsic_token_apply_prefix") {
    impl = Apply{PPTK_COMMA, PPTK_TRUE};
  } else if (name == "__intrinsic_token_apply_and") {
    impl = Apply{PPTK_AND, PPTK_FALSE};
  } else if (name == "__intrinsic_token_apply_and_prefix") {
    impl = Apply{PPTK_AND, PPTK_TRUE};
  } else {
    auto* sql_macro = state->macros.Find(name);
    if (!sql_macro) {
      state->error = {GrammarTokenToTokenizerToken(*name_tok),
                      "no such macro defined"};
      return;
    }
    impl = sql_macro;
  }
  invocation.active_macro =
      Frame::ActiveMacro{std::move(name), impl, {}, 0, {}, {}};
}

extern "C" void OnPreprocessorMacroArg(State* state,
                                       PreprocessorGrammarTokenBounds* arg) {
  auto& frame = state->stack.back();
  auto& macro = *frame.active_macro;
  if (macro.nested_macro_count > 0) {
    return;
  }
  auto start_token = GrammarTokenToTokenizerToken(arg->start);
  auto end_token = GrammarTokenToTokenizerToken(arg->end);
  state->stack.emplace_back(
      Frame::Append{macro.args}, frame.var_handling, state,
      frame.tokenizer.Substr(start_token, end_token,
                             SqliteTokenizer::EndToken::kInclusive));

  auto& arg_frame = state->stack.back();
  arg_frame.substitutions = frame.substitutions;
}

extern "C" void OnPreprocessorMacroEnd(State* state,
                                       PreprocessorGrammarToken* name,
                                       PreprocessorGrammarToken* rp) {
  auto& frame = state->stack.back();
  auto& macro = *frame.active_macro;
  if (macro.nested_macro_count > 0) {
    --macro.nested_macro_count;
    return;
  }
  switch (macro.impl.index()) {
    case base::variant_index<MacroImpl, PerfettoSqlPreprocessor::Macro*>():
      ExecuteSqlMacro(state, frame, macro, GrammarTokenToTokenizerToken(*name),
                      GrammarTokenToTokenizerToken(*rp));
      break;
    case base::variant_index<MacroImpl, Stringify>():
      ExecuteStringify(state, frame, macro, GrammarTokenToTokenizerToken(*name),
                       GrammarTokenToTokenizerToken(*rp));
      break;
    case base::variant_index<MacroImpl, Apply>():
      ExecuteApply(state, frame, macro, GrammarTokenToTokenizerToken(*name),
                   GrammarTokenToTokenizerToken(*rp));
      break;
    default:
      PERFETTO_FATAL("Unknown variant type");
  }
  frame.active_macro = std::nullopt;
}

extern "C" void OnPreprocessorEnd(State* state) {
  auto& frame = state->stack.back();
  PERFETTO_CHECK(!frame.active_macro);

  if (frame.ignore_rewrite) {
    return;
  }
  switch (frame.type.index()) {
    case base::variant_index<Frame::Type, Frame::Append>(): {
      auto& append = std::get<Frame::Append>(frame.type);
      append.result.push_back(std::move(frame.rewriter).Build());
      break;
    }
    case base::variant_index<Frame::Type, Frame::Rewrite>(): {
      auto& rewrite = std::get<Frame::Rewrite>(frame.type);
      rewrite.tokenizer.Rewrite(rewrite.rewriter, rewrite.start, rewrite.end,
                                std::move(frame.rewriter).Build(),
                                SqliteTokenizer::EndToken::kInclusive);
      break;
    }
    case base::variant_index<Frame::Type, Frame::Root>():
      break;
    default:
      PERFETTO_FATAL("Unknown frame type");
  }
}

}  // namespace

PerfettoSqlPreprocessor::PerfettoSqlPreprocessor(
    SqlSource source,
    const base::FlatHashMap<std::string, Macro>& macros)
    : global_tokenizer_(std::move(source)), macros_(&macros) {}

bool PerfettoSqlPreprocessor::NextStatement() {
  PERFETTO_CHECK(status_.ok());

  // Skip through any number of semi-colons (representing empty statements).
  SqliteTokenizer::Token tok = global_tokenizer_.NextNonWhitespace();
  while (tok.token_type == TK_SEMI) {
    tok = global_tokenizer_.NextNonWhitespace();
  }

  // If we still see a terminal token at this point, we must have hit EOF.
  if (tok.IsTerminal()) {
    PERFETTO_DCHECK(tok.token_type != TK_SEMI);
    return false;
  }

  SqlSource stmt =
      global_tokenizer_.Substr(tok, global_tokenizer_.NextTerminal(),
                               SqliteTokenizer::EndToken::kExclusive);

  State s{{}, *macros_, {}};
  s.stack.emplace_back(Frame::Root(), Frame::kIgnore, &s, std::move(stmt));
  for (;;) {
    auto* frame = &s.stack.back();
    auto& tk = frame->tokenizer;
    SqliteTokenizer::Token t = tk.NextNonWhitespace();
    int token_type;
    if (t.str.empty()) {
      token_type = frame->seen_semicolon ? 0 : PPTK_SEMI;
      frame->seen_semicolon = true;
    } else if (t.token_type == TK_SEMI) {
      token_type = PPTK_SEMI;
      frame->seen_semicolon = true;
    } else if (t.token_type == TK_ILLEGAL) {
      if (t.str.size() == 1 && t.str[0] == '!') {
        token_type = PPTK_EXCLAIM;
      } else {
        status_ = ErrorAtToken(tk, t, "illegal token");
        return false;
      }
    } else if (t.token_type == TK_ID) {
      token_type = PPTK_ID;
    } else if (t.token_type == TK_LP) {
      token_type = PPTK_LP;
    } else if (t.token_type == TK_RP) {
      token_type = PPTK_RP;
    } else if (t.token_type == TK_COMMA) {
      token_type = PPTK_COMMA;
    } else if (t.token_type == TK_VARIABLE) {
      token_type = PPTK_VARIABLE;
    } else {
      token_type = PPTK_OPAQUE;
    }
    frame->preprocessor.Parse(
        token_type,
        PreprocessorGrammarToken{t.str.data(), t.str.size(), token_type});
    if (s.error) {
      status_ = ErrorAtToken(tk, s.error->token, s.error->message.c_str());
      return false;
    }
    if (token_type == 0) {
      if (s.stack.size() == 1) {
        statement_ = std::move(frame->rewriter).Build();
        return true;
      }
      s.stack.pop_back();
      frame = &s.stack.back();
    }
  }
}

extern "C" PreprocessorGrammarApplyList* OnPreprocessorCreateApplyList() {
  return std::make_unique<PreprocessorGrammarApplyList>().release();
}

extern "C" PreprocessorGrammarApplyList* OnPreprocessorAppendApplyList(
    PreprocessorGrammarApplyList* list,
    PreprocessorGrammarTokenBounds* bounds) {
  list->args.push_back(*bounds);
  return list;
}

extern "C" void OnPreprocessorFreeApplyList(
    PreprocessorGrammarState*,
    PreprocessorGrammarApplyList* list) {
  std::unique_ptr<PreprocessorGrammarApplyList> l(list);
}

}  // namespace perfetto::trace_processor
