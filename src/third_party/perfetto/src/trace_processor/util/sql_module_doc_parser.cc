/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/util/sql_module_doc_parser.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/utils.h"

namespace perfetto::trace_processor::stdlib_doc {

namespace {

// Strips leading "-- " or "--" from a single comment line. Returns a
// string_view into the original source buffer — no allocation.
std::string_view StripCommentPrefix(std::string_view s) {
  s = base::TrimWhitespace(s);
  if (s.size() >= 2 && s[0] == '-' && s[1] == '-') {
    s.remove_prefix(2);
  }
  return base::TrimWhitespace(s);
}

// Joins all line comments (kind == 0) from |comments| into a single string,
// space-separated. |stmt_ptr| is the base pointer for comment offsets.
std::string JoinLineComments(const char* stmt_ptr,
                             const SyntaqliteComment* comments,
                             uint32_t count) {
  std::string result;
  for (uint32_t i = 0; i < count; i++) {
    if (comments[i].kind != 0) {
      continue;
    }
    std::string_view s =
        StripCommentPrefix({stmt_ptr + comments[i].offset, comments[i].length});
    if (!s.empty()) {
      if (!result.empty()) {
        result += ' ';
      }
      result.append(s);
    }
  }
  return result;
}

// Returns the index of the first comment in the last contiguous block of line
// comments. Two or more newlines between consecutive line comments (i.e. a
// blank line) breaks the block, which is used to skip license headers.
uint32_t LastBlockStart(const char* stmt_ptr,
                        const SyntaqliteComment* comments,
                        uint32_t count) {
  uint32_t start = 0;
  for (uint32_t i = 0; i + 1 < count; i++) {
    if (comments[i].kind != 0 || comments[i + 1].kind != 0) {
      start = i + 1;
      continue;
    }
    uint32_t gap_begin = comments[i].offset + comments[i].length;
    uint32_t gap_end = comments[i + 1].offset;
    int newlines = 0;
    for (uint32_t j = gap_begin; j < gap_end; j++) {
      if (stmt_ptr[j] == '\n' && ++newlines >= 2) {
        start = i + 1;
        break;
      }
    }
  }
  return start;
}

// Checks if a name is internal (starts with _).
bool IsInternal(const std::string& name) {
  return base::StartsWith(name, "_");
}

// Extracts entries from a PerfettoArgDefList node. Entry must be a struct with
// name/type/description fields (Column or Arg).
template <typename Entry>
std::vector<Entry> ExtractArgDefList(SyntaqliteParser* p,
                                     uint32_t list_id,
                                     const char* stmt_ptr) {
  std::vector<Entry> result;
  if (!syntaqlite_node_is_present(list_id)) {
    return result;
  }

  uint32_t tok_count = 0;
  const SyntaqliteParserToken* toks = syntaqlite_result_tokens(p, &tok_count);

  const auto* list = static_cast<const SyntaqlitePerfettoArgDefList*>(
      syntaqlite_parser_node(p, list_id));
  uint32_t count = syntaqlite_list_count(list);
  for (uint32_t i = 0; i < count; i++) {
    uint32_t item_id = syntaqlite_list_child_id(list, i);
    const auto* item = static_cast<const SyntaqlitePerfettoArgDef*>(
        syntaqlite_parser_node(p, item_id));
    if (!item) {
      continue;
    }
    const auto* name_node = static_cast<const SyntaqliteNode*>(
        syntaqlite_parser_node(p, item->arg_name));

    Entry entry;
    entry.name = SyntaqliteSpanText(p, name_node->ident_name.source);
    entry.type = SyntaqliteSpanText(p, item->arg_type);

    // Locate the token for the arg name by its source offset, then fetch
    // the leading comments attached to that token.
    uint32_t name_len = 0, name_off = 0;
    if (syntaqlite_parser_span_text(p, &name_node->ident_name.source, &name_len,
                                    &name_off)) {
      for (uint32_t ti = 0; ti < tok_count; ti++) {
        if (toks[ti]._layer_id == 0 && toks[ti].offset == name_off) {
          uint32_t c_count = 0;
          const auto* cs = syntaqlite_token_leading_comments(p, ti, &c_count);
          entry.description = JoinLineComments(stmt_ptr, cs, c_count);
          break;
        }
      }
    }

    result.push_back(std::move(entry));
  }
  return result;
}

std::vector<Column> ExtractColumns(SyntaqliteParser* p,
                                   uint32_t list_id,
                                   const char* stmt_ptr) {
  return ExtractArgDefList<Column>(p, list_id, stmt_ptr);
}

std::vector<Arg> ExtractArgs(SyntaqliteParser* p,
                             uint32_t list_id,
                             const char* stmt_ptr) {
  return ExtractArgDefList<Arg>(p, list_id, stmt_ptr);
}

// Extracts macro args from a PerfettoMacroArgList node.
std::vector<Arg> ExtractMacroArgs(SyntaqliteParser* p,
                                  uint32_t list_id,
                                  const char* stmt_ptr) {
  std::vector<Arg> result;
  if (!syntaqlite_node_is_present(list_id)) {
    return result;
  }
  const auto* list = static_cast<const SyntaqlitePerfettoMacroArgList*>(
      syntaqlite_parser_node(p, list_id));
  uint32_t count = syntaqlite_list_count(list);
  for (uint32_t i = 0; i < count; i++) {
    uint32_t item_id = syntaqlite_list_child_id(list, i);
    const auto* item = static_cast<const SyntaqlitePerfettoMacroArg*>(
        syntaqlite_parser_node(p, item_id));
    if (!item) {
      continue;
    }

    Arg arg;
    arg.name = SyntaqliteSpanText(p, item->arg_name);
    arg.type = SyntaqliteSpanText(p, item->arg_type);

    uint32_t c_count = 0;
    const auto* cs = syntaqlite_node_leading_comments(p, item_id, &c_count);
    arg.description = JoinLineComments(stmt_ptr, cs, c_count);

    result.push_back(std::move(arg));
  }
  return result;
}

// Returns the return description for a function: the leading line comments on
// the RETURNS keyword. Uses syntaqlite_node_token_range to find the first
// token of the return type node, then checks that token and the one before it
// (covering both cases: node starts at RETURNS vs. after RETURNS).
std::string GetReturnDescription(SyntaqliteParser* p,
                                 uint32_t return_type_node_id,
                                 const char* stmt_ptr) {
  if (!syntaqlite_node_is_present(return_type_node_id)) {
    return {};
  }
  SyntaqliteTokenIdx first_tok = 0, last_tok = 0;
  if (!syntaqlite_node_token_range(p, return_type_node_id, &first_tok,
                                   &last_tok)) {
    return {};
  }
  uint32_t c_count = 0;
  const auto* cs = syntaqlite_token_leading_comments(p, first_tok, &c_count);
  if (c_count == 0 && first_tok > 0) {
    cs = syntaqlite_token_leading_comments(p, first_tok - 1, &c_count);
  }
  return JoinLineComments(stmt_ptr, cs, c_count);
}

struct SyntaqliteParserDeleter {
  void operator()(SyntaqliteParser* p) const { syntaqlite_parser_destroy(p); }
};
using ScopedParser = std::unique_ptr<SyntaqliteParser, SyntaqliteParserDeleter>;

}  // namespace

ParsedModule ParseStdlibModule(const char* sql, uint32_t sql_len) {
  ParsedModule result;

  ScopedParser owned(syntaqlite_parser_create_with_dialect(
      nullptr, syntaqlite_perfetto_dialect()));
  PERFETTO_CHECK(owned != nullptr);
  SyntaqliteParser* p = owned.get();

  syntaqlite_parser_set_collect_tokens(p, 1);
  syntaqlite_parser_set_collect_node_extents(p, 1);
  syntaqlite_parser_set_macro_fallback(p, 1);

  syntaqlite_parser_reset(p, sql, sql_len);

  for (;;) {
    int32_t rc = syntaqlite_parser_next(p);
    if (rc == SYNTAQLITE_PARSE_DONE) {
      break;
    }

    if (rc == SYNTAQLITE_PARSE_ERROR) {
      const char* err_msg = syntaqlite_result_error_msg(p);
      result.errors.push_back(err_msg ? std::string("Parse error: ") + err_msg
                                      : std::string("Unknown parse error"));
      continue;
    }

    uint32_t root = syntaqlite_result_root(p);
    if (!syntaqlite_node_is_present(root)) {
      continue;
    }

    const auto* node =
        static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(p, root));

    // syntaqlite_parser_text with null out-params returns the raw statement
    // start pointer without writing any lengths; offsets in tokens/comments
    // are all relative to this base.
    const char* stmt_ptr = syntaqlite_parser_text(p, nullptr, nullptr);
    PERFETTO_DCHECK(stmt_ptr != nullptr);

    // Statement-level description: last contiguous block of leading line
    // comments on token 0 (the CREATE keyword), skipping license headers
    // separated by a blank line.
    auto get_stmt_desc = [&]() -> std::string {
      uint32_t count = 0;
      const auto* cs = syntaqlite_token_leading_comments(p, 0, &count);
      uint32_t start = LastBlockStart(stmt_ptr, cs, count);
      return JoinLineComments(stmt_ptr, cs + start, count - start);
    };

    switch (static_cast<int>(node->tag)) {
      case SYNTAQLITE_NODE_CREATE_PERFETTO_TABLE_STMT: {
        const auto& n = node->create_perfetto_table_stmt;
        TableOrView tv;
        tv.name = SyntaqliteSpanText(p, n.table_name);
        tv.type = "TABLE";
        tv.exposed = !IsInternal(tv.name);
        tv.description = get_stmt_desc();
        tv.columns = ExtractColumns(p, n.schema, stmt_ptr);
        result.table_views.push_back(std::move(tv));
        break;
      }

      case SYNTAQLITE_NODE_CREATE_PERFETTO_VIEW_STMT: {
        const auto& n = node->create_perfetto_view_stmt;
        TableOrView tv;
        tv.name = SyntaqliteSpanText(p, n.view_name);
        tv.type = "VIEW";
        tv.exposed = !IsInternal(tv.name);
        tv.description = get_stmt_desc();
        tv.columns = ExtractColumns(p, n.schema, stmt_ptr);
        result.table_views.push_back(std::move(tv));
        break;
      }

      case SYNTAQLITE_NODE_CREATE_PERFETTO_FUNCTION_STMT:
      case SYNTAQLITE_NODE_CREATE_PERFETTO_DELEGATING_FUNCTION_STMT: {
        SyntaqliteTextSpan fn_name_span;
        uint32_t args_list_id;
        uint32_t return_type_id;
        if (node->tag == SYNTAQLITE_NODE_CREATE_PERFETTO_FUNCTION_STMT) {
          const auto& n = node->create_perfetto_function_stmt;
          fn_name_span = n.function_name;
          args_list_id = n.args;
          return_type_id = n.return_type;
        } else {
          const auto& n = node->create_perfetto_delegating_function_stmt;
          fn_name_span = n.function_name;
          args_list_id = n.args;
          return_type_id = n.return_type;
        }

        Function fn;
        fn.name = SyntaqliteSpanText(p, fn_name_span);
        fn.exposed = !IsInternal(fn.name);
        fn.description = get_stmt_desc();
        fn.args = ExtractArgs(p, args_list_id, stmt_ptr);

        if (syntaqlite_node_is_present(return_type_id)) {
          const auto* rt = static_cast<const SyntaqlitePerfettoReturnType*>(
              syntaqlite_parser_node(p, return_type_id));
          if (rt->kind == SYNTAQLITE_PERFETTO_RETURN_KIND_TABLE) {
            fn.is_table_function = true;
            fn.return_type = "TABLE";
            fn.columns = ExtractColumns(p, rt->table_columns, stmt_ptr);
          } else {
            fn.return_type = SyntaqliteSpanText(p, rt->scalar_type);
          }
          fn.return_description =
              GetReturnDescription(p, return_type_id, stmt_ptr);
        }

        result.functions.push_back(std::move(fn));
        break;
      }

      case SYNTAQLITE_NODE_CREATE_PERFETTO_MACRO_STMT: {
        const auto& n = node->create_perfetto_macro_stmt;
        Macro macro;
        macro.name = SyntaqliteSpanText(p, n.macro_name);
        macro.exposed = !IsInternal(macro.name);
        macro.description = get_stmt_desc();
        macro.return_type = SyntaqliteSpanText(p, n.return_type);
        macro.args = ExtractMacroArgs(p, n.args, stmt_ptr);

        // Return description: leading comments on the RETURNS keyword.
        // The macro return type is a SyntaqliteTextSpan (no node_id), so we
        // locate its token by scanning the statement's token array and look at
        // the preceding token's leading comments.
        uint32_t ret_len = 0, ret_off = 0;
        if (syntaqlite_parser_span_text(p, &n.return_type, &ret_len,
                                        &ret_off)) {
          uint32_t tok_count = 0;
          const SyntaqliteParserToken* toks =
              syntaqlite_result_tokens(p, &tok_count);
          for (uint32_t ti = 0; ti < tok_count; ti++) {
            if (toks[ti]._layer_id == 0 && toks[ti].offset == ret_off &&
                ti > 0) {
              uint32_t c_count = 0;
              const auto* cs =
                  syntaqlite_token_leading_comments(p, ti - 1, &c_count);
              macro.return_description =
                  JoinLineComments(stmt_ptr, cs, c_count);
              break;
            }
          }
        }

        result.macros.push_back(std::move(macro));
        break;
      }

      default:
        break;
    }
  }

  return result;
}

}  // namespace perfetto::trace_processor::stdlib_doc
