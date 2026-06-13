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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/perfetto_sql/parser/intrinsic_macro_expansion.h"
#include "src/trace_processor/perfetto_sql/preprocessor/perfetto_sql_preprocessor.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/util/sql_argument.h"

namespace perfetto::trace_processor {

namespace {

// When `false`, macro expansion + statement splitting go through the
// handwritten `PerfettoSqlPreprocessor` (the legacy path that has shipped for
// years). When `true`, both responsibilities are delegated to the vendored
// syntaqlite library via the macro-lookup callback.
//
// Both implementations are compiled in for the duration of the cutover so
// flipping is a one-line change and reverting is symmetric. The same value
// is re-exported as `PerfettoSqlParser::kUsesSyntaqliteMacros` for tests
// that need to gate path-specific assertions.
constexpr bool kUseSyntaqliteMacros = PerfettoSqlParser::kUsesSyntaqliteMacros;

using Macro = PerfettoSqlParser::Macro;
using Statement = PerfettoSqlParser::Statement;

std::string SpanText(SyntaqliteParser* p, SyntaqliteTextSpan span) {
  uint32_t len;
  const char* text = syntaqlite_parser_span_expanded_text(p, &span, &len);
  PERFETTO_CHECK(text != nullptr);
  return {text, len};
}

// Slices |stmt| using a source-layer span whose offset/length are direct byte
// offsets into the input string (layer_id == 0, injected by our grammar marker
// rules).
SqlSource SpanSource(const SqlSource& stmt, SyntaqliteTextSpan span) {
  PERFETTO_DCHECK(span._layer_id == 0);
  return stmt.Substr(span.offset, span.length);
}

base::StatusOr<std::vector<sql_argument::ArgumentDefinition>> BuildArgDefs(
    SyntaqliteParser* p,
    uint32_t list_id) {
  std::vector<sql_argument::ArgumentDefinition> result;
  if (!syntaqlite_node_is_present(list_id))
    return result;

  const auto* list = static_cast<const SyntaqlitePerfettoArgDefList*>(
      syntaqlite_parser_node(p, list_id));
  uint32_t count = syntaqlite_list_count(list);
  for (uint32_t i = 0; i < count; i++) {
    const auto* item = static_cast<const SyntaqlitePerfettoArgDef*>(
        syntaqlite_list_child(p, list, i));

    const auto* name_node = static_cast<const SyntaqliteNode*>(
        syntaqlite_parser_node(p, item->arg_name));
    std::string name = "$" + SpanText(p, name_node->ident_name.source);

    std::string type_str = SpanText(p, item->arg_type);
    // For JOINID(table.col) syntax, strip the hint suffix before type lookup.
    auto paren = type_str.find('(');
    if (paren != std::string::npos)
      type_str = type_str.substr(0, paren);
    auto type = sql_argument::ParseType(base::StringView(type_str));
    if (!type)
      return base::ErrStatus("Unknown argument type: %s", type_str.c_str());

    bool is_variadic = item->is_variadic == SYNTAQLITE_BOOL_TRUE;
    result.emplace_back(std::move(name), *type, is_variadic);
  }
  return result;
}

base::StatusOr<PerfettoSqlParser::CreateFunction::Returns> BuildReturnType(
    SyntaqliteParser* p,
    uint32_t rt_id) {
  const auto* rt = static_cast<const SyntaqlitePerfettoReturnType*>(
      syntaqlite_parser_node(p, rt_id));

  PerfettoSqlParser::CreateFunction::Returns result;
  if (rt->kind == SYNTAQLITE_PERFETTO_RETURN_KIND_TABLE) {
    result.is_table = true;
    auto cols = BuildArgDefs(p, rt->table_columns);
    if (!cols.ok())
      return cols.status();
    result.table_columns = std::move(*cols);
  } else {
    result.is_table = false;
    std::string type_str = SpanText(p, rt->scalar_type);
    auto type = sql_argument::ParseType(base::StringView(type_str));
    if (!type)
      return base::ErrStatus("Unknown return type: %s", type_str.c_str());
    result.scalar_type = *type;
  }
  return result;
}

// ---------------------------------------------------------------------------
// LegacyImpl
// ---------------------------------------------------------------------------
//
// Macro expansion + statement splitting via the handwritten
// PerfettoSqlPreprocessor; per-statement parsing via syntaqlite. Reserved as
// a self-contained struct in the anonymous namespace so a second
// implementation (syntaqlite-driven macro expansion) can be added next to it
// without disturbing the public PerfettoSqlParser surface.

struct LegacyImpl {
  // [[maybe_unused]]: only one of LegacyImpl / SyntaqliteMacroImpl is reached
  // through the `if constexpr (kUseSyntaqliteMacros)` switch in Impl, so the
  // unreached side has no in-TU callers for its constructor or `Next`.
  [[maybe_unused]] LegacyImpl(
      SqlSource source,
      const base::FlatHashMap<std::string, Macro>& macros)
      : preprocessor(std::move(source), macros) {
    synq = syntaqlite_parser_create_with_dialect(nullptr,
                                                 syntaqlite_perfetto_dialect());
    PERFETTO_CHECK(synq != nullptr);
  }

  ~LegacyImpl() { syntaqlite_parser_destroy(synq); }

  LegacyImpl(const LegacyImpl&) = delete;
  LegacyImpl& operator=(const LegacyImpl&) = delete;
  LegacyImpl(LegacyImpl&&) = delete;
  LegacyImpl& operator=(LegacyImpl&&) = delete;

  [[maybe_unused]] bool Next(std::optional<SqlSource>& out_statement_sql);

  SyntaqliteParser* synq;
  PerfettoSqlPreprocessor preprocessor;
  base::Status status;
  std::optional<Statement> current_statement;
};

bool LegacyImpl::Next(std::optional<SqlSource>& out_statement_sql) {
  PERFETTO_DCHECK(status.ok());

  current_statement = std::nullopt;
  out_statement_sql = std::nullopt;

  if (!preprocessor.NextStatement()) {
    status = preprocessor.status();
    return false;
  }

  const SqlSource& stmt = preprocessor.statement();
  out_statement_sql = stmt;

  syntaqlite_parser_reset(synq, stmt.sql().data(),
                          static_cast<uint32_t>(stmt.sql().size()));

  int32_t rc = syntaqlite_parser_next(synq);
  if (rc == SYNTAQLITE_PARSE_DONE) {
    current_statement = PerfettoSqlParser::SqliteSql{};
    return true;
  }
  if (rc == SYNTAQLITE_PARSE_ERROR) {
    uint32_t off = syntaqlite_result_error_offset(synq);
    status = base::ErrStatus("%s%s", stmt.AsTraceback(off).c_str(),
                             syntaqlite_result_error_msg(synq));
    return false;
  }

  uint32_t root = syntaqlite_result_root(synq);
  const auto* node =
      static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(synq, root));

  // Cast to int to suppress -Wswitch-enum: we intentionally handle only
  // Perfetto-dialect node types; all SQLite statement types fall through to the
  // default case and are returned as SqliteSql{}.
  switch (static_cast<int>(node->tag)) {
    case SYNTAQLITE_NODE_CREATE_PERFETTO_TABLE_STMT: {
      const auto& n = node->create_perfetto_table_stmt;
      std::string name = SpanText(synq, n.table_name);

      if (syntaqlite_node_is_present(n.table_impl)) {
        const auto* impl_node = static_cast<const SyntaqlitePerfettoTableImpl*>(
            syntaqlite_parser_node(synq, n.table_impl));
        std::string impl_name = SpanText(synq, impl_node->name);
        if (!base::CaseInsensitiveEqual(impl_name, "dataframe")) {
          status = base::ErrStatus("Invalid table implementation '%s'",
                                   impl_name.c_str());
          return false;
        }
      }

      auto schema = BuildArgDefs(synq, n.schema);
      if (!schema.ok()) {
        status = schema.status();
        return false;
      }
      current_statement = PerfettoSqlParser::CreateTable{
          n.or_replace == SYNTAQLITE_BOOL_TRUE,
          std::move(name),
          std::move(*schema),
          SpanSource(stmt, n.select_span),
      };
      return true;
    }

    case SYNTAQLITE_NODE_CREATE_PERFETTO_VIEW_STMT: {
      const auto& n = node->create_perfetto_view_stmt;
      std::string name = SpanText(synq, n.view_name);

      auto schema = BuildArgDefs(synq, n.schema);
      if (!schema.ok()) {
        status = schema.status();
        return false;
      }
      SqlSource select_sql = SpanSource(stmt, n.select_span);

      SqlSource header = SqlSource::FromTraceProcessorImplementation(
          "CREATE VIEW " + name + " AS ");
      SqlSource::Rewriter rewriter(stmt);
      rewriter.Rewrite(0, n.select_span.offset, std::move(header));
      SqlSource create_view_sql = std::move(rewriter).Build();

      current_statement = PerfettoSqlParser::CreateView{
          n.or_replace == SYNTAQLITE_BOOL_TRUE,
          std::move(name),
          std::move(*schema),
          std::move(select_sql),
          std::move(create_view_sql),
      };
      return true;
    }

    case SYNTAQLITE_NODE_CREATE_PERFETTO_FUNCTION_STMT: {
      const auto& n = node->create_perfetto_function_stmt;
      std::string name = SpanText(synq, n.function_name);

      auto args = BuildArgDefs(synq, n.args);
      if (!args.ok()) {
        status = args.status();
        return false;
      }
      // Variadic arguments are not allowed in SQL functions.
      for (const auto& arg : *args) {
        if (arg.is_variadic()) {
          status = base::ErrStatus(
              "Variadic arguments are only allowed in delegate functions (use "
              "DELEGATES TO instead of AS)");
          return false;
        }
      }

      auto returns = BuildReturnType(synq, n.return_type);
      if (!returns.ok()) {
        status = returns.status();
        return false;
      }
      current_statement = PerfettoSqlParser::CreateFunction{
          n.or_replace == SYNTAQLITE_BOOL_TRUE,
          FunctionPrototype{std::move(name), std::move(*args)},
          std::move(*returns),
          SpanSource(stmt, n.select_span),
          "",
          std::nullopt,
      };
      return true;
    }

    case SYNTAQLITE_NODE_CREATE_PERFETTO_DELEGATING_FUNCTION_STMT: {
      const auto& n = node->create_perfetto_delegating_function_stmt;
      std::string name = SpanText(synq, n.function_name);

      auto args = BuildArgDefs(synq, n.args);
      if (!args.ok()) {
        status = args.status();
        return false;
      }
      // Variadic argument, if present, must be the last in the list.
      for (uint32_t i = 0; i + 1 < args->size(); ++i) {
        if ((*args)[i].is_variadic()) {
          status =
              base::ErrStatus("Variadic argument must be the last argument");
          return false;
        }
      }
      auto returns = BuildReturnType(synq, n.return_type);
      if (!returns.ok()) {
        status = returns.status();
        return false;
      }
      std::string delegate_to = SpanText(synq, n.delegate_to);
      current_statement = PerfettoSqlParser::CreateFunction{
          n.or_replace == SYNTAQLITE_BOOL_TRUE,
          FunctionPrototype{std::move(name), std::move(*args)},
          std::move(*returns),
          SqlSource::FromTraceProcessorImplementation(""),
          "",
          std::move(delegate_to),
      };
      return true;
    }

    case SYNTAQLITE_NODE_CREATE_PERFETTO_INDEX_STMT: {
      const auto& n = node->create_perfetto_index_stmt;
      std::string index_name = SpanText(synq, n.index_name);
      std::string table_name = SpanText(synq, n.table_name);

      std::vector<std::string> col_names;
      if (syntaqlite_node_is_present(n.columns)) {
        const auto* list =
            static_cast<const SyntaqlitePerfettoIndexedColumnList*>(
                syntaqlite_parser_node(synq, n.columns));
        uint32_t count = syntaqlite_list_count(list);
        for (uint32_t i = 0; i < count; i++) {
          const auto* col = static_cast<const SyntaqlitePerfettoIndexedColumn*>(
              syntaqlite_list_child(synq, list, i));
          col_names.push_back(SpanText(synq, col->column_name));
        }
      }
      current_statement = PerfettoSqlParser::CreateIndex{
          n.or_replace == SYNTAQLITE_BOOL_TRUE,
          std::move(index_name),
          std::move(table_name),
          std::move(col_names),
      };
      return true;
    }

    case SYNTAQLITE_NODE_CREATE_PERFETTO_MACRO_STMT: {
      const auto& n = node->create_perfetto_macro_stmt;

      // Build macro argument list as (name SqlSource, type SqlSource) pairs.
      std::vector<std::pair<SqlSource, SqlSource>> macro_args;
      if (syntaqlite_node_is_present(n.args)) {
        const auto* list = static_cast<const SyntaqlitePerfettoMacroArgList*>(
            syntaqlite_parser_node(synq, n.args));
        uint32_t count = syntaqlite_list_count(list);
        for (uint32_t i = 0; i < count; i++) {
          const auto* arg = static_cast<const SyntaqlitePerfettoMacroArg*>(
              syntaqlite_list_child(synq, list, i));
          std::string arg_name_str = SpanText(synq, arg->arg_name);
          std::string arg_type_str = SpanText(synq, arg->arg_type);
          macro_args.emplace_back(SqlSource::FromTraceProcessorImplementation(
                                      std::move(arg_name_str)),
                                  SqlSource::FromTraceProcessorImplementation(
                                      std::move(arg_type_str)));
        }
      }

      std::string macro_name_str = SpanText(synq, n.macro_name);
      std::string returns_str = SpanText(synq, n.return_type);
      std::string body_str = SpanText(synq, n.body);
      current_statement = PerfettoSqlParser::CreateMacro{
          n.or_replace == SYNTAQLITE_BOOL_TRUE,
          SqlSource::FromTraceProcessorImplementation(
              std::move(macro_name_str)),
          std::move(macro_args),
          SqlSource::FromTraceProcessorImplementation(std::move(returns_str)),
          SqlSource::FromTraceProcessorImplementation(std::move(body_str)),
      };
      return true;
    }

    case SYNTAQLITE_NODE_INCLUDE_PERFETTO_MODULE_STMT: {
      const auto& n = node->include_perfetto_module_stmt;
      current_statement =
          PerfettoSqlParser::Include{SpanText(synq, n.module_name)};
      return true;
    }

    case SYNTAQLITE_NODE_DROP_PERFETTO_INDEX_STMT: {
      const auto& n = node->drop_perfetto_index_stmt;
      current_statement = PerfettoSqlParser::DropIndex{
          SpanText(synq, n.index_name),
          SpanText(synq, n.table_name),
      };
      return true;
    }

    default:
      // Any other SQLite statement passes through as SqliteSql.
      current_statement = PerfettoSqlParser::SqliteSql{};
      return true;
  }
}

// ---------------------------------------------------------------------------
// Macro rewrite tree -> SqlSource (syntaqlite path only)
// ---------------------------------------------------------------------------

// Walks the flat list of macro rewrites produced by syntaqlite and, for a
// given AST node, builds a SqlSource whose `Rewriter` structure mirrors the
// nesting of macro calls. This lets SQLite-side error tracebacks resolve
// through macro expansions back to the authored call site.
//
// The flat list has O(N) entries reported in insertion order (outer macros
// before their nested calls). Rather than re-scanning the list for each
// rewrite we materialize a parent -> children adjacency once in the
// constructor, so every subsequent lookup descends the tree in O(subtree).
class MacroRewriteBuilder {
 public:
  // `stmt_doc_offset` is the byte offset of the current statement within
  // `stmt`.  Syntaqlite v0.5 reports every layer-0 offset (node extents,
  // spans, macro call offsets) statement-relative, so call sites that
  // slice `stmt` add this offset to translate into document coordinates.
  MacroRewriteBuilder(SyntaqliteParser* p,
                      const SqlSource& stmt,
                      uint32_t stmt_doc_offset,
                      const base::FlatHashMap<std::string, Macro>& macros)
      : p_(p), stmt_(stmt), stmt_doc_offset_(stmt_doc_offset), macros_(macros) {
    uint32_t total = syntaqlite_result_macro_count(p_);
    children_.resize(total);
    for (uint32_t i = 0; i < total; i++) {
      auto r = syntaqlite_result_macro_rewrite_at(p_, i);
      if (r.parent_idx == SYNTAQLITE_MACRO_PARENT_SOURCE) {
        source_rooted_.push_back(i);
      } else {
        children_[r.parent_idx].push_back(i);
      }
    }
  }

  // Returns a SqlSource for the AST subtree rooted at `node_id`, with any
  // macro rewrites that fall within its range applied. Returns std::nullopt
  // if the node has no recorded extent.
  std::optional<SqlSource> NodeSource(uint32_t node_id) const {
    uint32_t len = 0;
    uint32_t stmt_off = 0;
    if (syntaqlite_parser_node_text(p_, node_id, &len, &stmt_off) == nullptr)
      return std::nullopt;
    SqlSource base = stmt_.Substr(stmt_off + stmt_doc_offset_, len);
    if (syntaqlite_node_is_macro_free(p_, node_id))
      return base;
    return ApplyChildrenInRange(std::move(base), source_rooted_, stmt_off, len);
  }

 private:
  struct RewriteItem {
    uint32_t start;  // in base-SqlSource coordinates
    uint32_t end;    // exclusive, in base-SqlSource coordinates
    SqlSource replacement;
  };

  // Returns `base` with every rewrite in `children` whose call site lies
  // inside [range_offset, range_offset + range_length) applied to it
  // (translated into base-local coordinates).  Shared by every caller that
  // works in `call_offset`-based coordinates: the authored source range,
  // an intrinsic's expansion buffer, and a $param arg's expansion range.
  //
  // Relies on syntaqlite's guarantee that siblings in the rewrite tree are
  // reported in source order, so the filtered items end up pre-sorted by
  // `call_offset` and can be fed straight to the Rewriter.
  SqlSource ApplyChildrenInRange(SqlSource base,
                                 const std::vector<uint32_t>& children,
                                 uint32_t range_offset,
                                 uint32_t range_length) const {
    std::vector<RewriteItem> items;
    for (uint32_t idx : children) {
      auto c = syntaqlite_result_macro_rewrite_at(p_, idx);
      if (c.call_offset < range_offset)
        continue;
      if (c.call_offset + c.call_length > range_offset + range_length)
        continue;
      uint32_t local = c.call_offset - range_offset;
      items.push_back({local, local + c.call_length, BuildForRewrite(idx)});
    }
    return ApplyRewrites(std::move(base), std::move(items));
  }

  // Recursive case: the expansion of a single macro rewrite.
  SqlSource BuildForRewrite(uint32_t idx) const {
    auto r = syntaqlite_result_macro_rewrite_at(p_, idx);
    std::string_view name(r.name, r.name_len);
    if (const Macro* m = macros_.Find(name); m)
      return BuildForUserMacro(idx, *m);
    // Intrinsic macro: no authored body, so work directly in expansion
    // coordinates. The base is the expansion text and children already
    // carry call_offsets into that buffer.
    return ApplyChildrenInRange(
        SqlSource::FromMacroExpansion(std::string(r.expansion, r.expansion_len),
                                      std::string(name)),
        children_[idx], 0, r.expansion_len);
  }

  // A user-defined macro has an authored body (macro->sql) with `$param`
  // placeholders. Two kinds of rewrites apply, both in body coordinates:
  //   1. Nested calls that appear *literally* in the body. Children whose
  //      body_call_length is SYNTAQLITE_MACRO_BODY_CALL_ARG_INTERNAL came
  //      from a substituted arg and will be handled by BuildForArg when
  //      its segment is processed.
  //   2. `$param` substitution segments. A segment whose body range is
  //      contained by a literal nested call is dropped: that call's
  //      expansion already pulled the arg text through, and emitting an
  //      overlapping rewrite would violate Rewriter's invariant.
  SqlSource BuildForUserMacro(uint32_t idx, const Macro& m) const {
    std::vector<RewriteItem> items;
    for (uint32_t cidx : children_[idx]) {
      auto c = syntaqlite_result_macro_rewrite_at(p_, cidx);
      if (c.body_call_length == SYNTAQLITE_MACRO_BODY_CALL_ARG_INTERNAL)
        continue;
      items.push_back({c.body_call_offset,
                       c.body_call_offset + c.body_call_length,
                       BuildForRewrite(cidx)});
    }
    uint32_t seg_count = syntaqlite_macro_rewrite_arg_segment_count(p_, idx);
    for (uint32_t si = 0; si < seg_count; si++) {
      auto seg = syntaqlite_macro_rewrite_arg_segment_at(p_, idx, si);
      bool subsumed = false;
      for (const auto& it : items) {
        if (seg.body_offset >= it.start &&
            seg.body_offset + seg.body_length <= it.end) {
          subsumed = true;
          break;
        }
      }
      if (subsumed)
        continue;
      items.push_back({seg.body_offset, seg.body_offset + seg.body_length,
                       BuildForArg(idx, seg)});
    }
    // This is the only ApplyRewrites caller whose items aren't already in
    // `start` order: we concatenated literal body calls and $param segments,
    // each internally sorted but interleaved relative to each other.
    std::sort(items.begin(), items.end(),
              [](const RewriteItem& a, const RewriteItem& b) {
                return a.start < b.start;
              });
    return ApplyRewrites(m.sql, std::move(items));
  }

  // The SqlSource for one `$param` substitution: the arg's authored text,
  // with any of the parent's nested calls that happen to fall inside the
  // substituted region applied on top.
  SqlSource BuildForArg(uint32_t parent_idx,
                        const SyntaqliteMacroArgSegment& seg) const {
    return ApplyChildrenInRange(
        AuthoredSourceOf(seg.origin_parent_idx, seg.origin_offset,
                         seg.origin_length),
        children_[parent_idx], seg.expansion_offset, seg.expansion_length);
  }

  // Returns the SqlSource for the byte range [offset, offset+length) inside
  // `parent`'s text (`parent` is either SYNTAQLITE_MACRO_PARENT_SOURCE or
  // a rewrite index).
  //
  // If that range lies inside a `$param` substitution of `parent`, the arg
  // text was pasted in from somewhere else - we recurse into the segment's
  // own origin instead of claiming it was authored inside `parent`.  This
  // keeps error tracebacks pointing at the location the user actually
  // typed the text, rather than a macro expansion buffer it flowed
  // through.
  SqlSource AuthoredSourceOf(uint32_t parent,
                             uint32_t offset,
                             uint32_t length) const {
    if (parent == SYNTAQLITE_MACRO_PARENT_SOURCE)
      return stmt_.Substr(offset + stmt_doc_offset_, length);
    uint32_t seg_count = syntaqlite_macro_rewrite_arg_segment_count(p_, parent);
    for (uint32_t i = 0; i < seg_count; i++) {
      auto s = syntaqlite_macro_rewrite_arg_segment_at(p_, parent, i);
      if (offset >= s.expansion_offset &&
          offset + length <= s.expansion_offset + s.expansion_length) {
        return AuthoredSourceOf(s.origin_parent_idx,
                                s.origin_offset + (offset - s.expansion_offset),
                                length);
      }
    }
    // Literal text in `parent`'s body - not from any $param.
    auto r = syntaqlite_result_macro_rewrite_at(p_, parent);
    return SqlSource::FromMacroExpansion(
        std::string(r.expansion + offset, length),
        std::string(r.name, r.name_len));
  }

  // Applies `items` on top of `base`.  Items must already be sorted by
  // `start` and non-overlapping; the sort lives at the (single) call site
  // that can produce unsorted input.
  static SqlSource ApplyRewrites(SqlSource base,
                                 std::vector<RewriteItem> items) {
    if (items.empty())
      return base;
    PERFETTO_DCHECK(
        std::is_sorted(items.begin(), items.end(),
                       [](const RewriteItem& a, const RewriteItem& b) {
                         return a.start < b.start;
                       }));
    SqlSource::Rewriter rw(std::move(base));
    for (auto& it : items) {
      rw.Rewrite(it.start, it.end, std::move(it.replacement));
    }
    return std::move(rw).Build();
  }

  SyntaqliteParser* p_;
  const SqlSource& stmt_;
  uint32_t stmt_doc_offset_;
  const base::FlatHashMap<std::string, Macro>& macros_;
  // children_[parent_idx] = rewrite indices whose `parent_idx` equals that.
  std::vector<std::vector<uint32_t>> children_;
  // Rewrites whose parent is SYNTAQLITE_MACRO_PARENT_SOURCE.
  std::vector<uint32_t> source_rooted_;
};

SqlSource NodeSource(const MacroRewriteBuilder& rb, uint32_t node_id) {
  auto s = rb.NodeSource(node_id);
  PERFETTO_CHECK(s.has_value());
  return std::move(*s);
}

// ---------------------------------------------------------------------------
// Per-statement dispatch (syntaqlite path)
// ---------------------------------------------------------------------------

// Document-relative byte offset of the statement currently held by `p`,
// or 0 when the parser hasn't started a statement yet (e.g. tokenizer
// error before any statement boundary was found).  Every layer-0 offset
// syntaqlite emits for the current statement is measured from this
// position within the bound source.
uint32_t CurrentStatementDocOffset(SyntaqliteParser* p) {
  uint32_t doc_offset = 0;
  syntaqlite_parser_text(p, &doc_offset, nullptr);
  return doc_offset;
}

base::StatusOr<Statement> ParseCreateTable(
    SyntaqliteParser* p,
    const MacroRewriteBuilder& rb,
    const SyntaqliteCreatePerfettoTableStmt& n) {
  if (syntaqlite_node_is_present(n.table_impl)) {
    const auto* impl_node = static_cast<const SyntaqlitePerfettoTableImpl*>(
        syntaqlite_parser_node(p, n.table_impl));
    std::string impl_name = SpanText(p, impl_node->name);
    if (!base::CaseInsensitiveEqual(impl_name, "dataframe"))
      return base::ErrStatus("Invalid table implementation '%s'",
                             impl_name.c_str());
  }
  ASSIGN_OR_RETURN(auto schema, BuildArgDefs(p, n.schema));
  return Statement(PerfettoSqlParser::CreateTable{
      n.or_replace == SYNTAQLITE_BOOL_TRUE,
      SpanText(p, n.table_name),
      std::move(schema),
      NodeSource(rb, n.select),
  });
}

base::StatusOr<Statement> ParseCreateView(
    SyntaqliteParser* p,
    const MacroRewriteBuilder& rb,
    const SyntaqliteCreatePerfettoViewStmt& n) {
  ASSIGN_OR_RETURN(auto schema, BuildArgDefs(p, n.schema));
  std::string name = SpanText(p, n.view_name);
  SqlSource select_sql = NodeSource(rb, n.select);
  SqlSource create_view_sql = SqlSource::FromTraceProcessorImplementation(
      "CREATE VIEW " + name + " AS " + select_sql.sql());
  return Statement(PerfettoSqlParser::CreateView{
      n.or_replace == SYNTAQLITE_BOOL_TRUE,
      std::move(name),
      std::move(schema),
      std::move(select_sql),
      std::move(create_view_sql),
  });
}

base::StatusOr<Statement> ParseCreateFunction(
    SyntaqliteParser* p,
    const MacroRewriteBuilder& rb,
    const SyntaqliteCreatePerfettoFunctionStmt& n) {
  ASSIGN_OR_RETURN(auto args, BuildArgDefs(p, n.args));
  for (const auto& arg : args) {
    if (arg.is_variadic()) {
      return base::ErrStatus(
          "Variadic arguments are only allowed in delegate functions (use "
          "DELEGATES TO instead of AS)");
    }
  }
  ASSIGN_OR_RETURN(auto returns, BuildReturnType(p, n.return_type));
  return Statement(PerfettoSqlParser::CreateFunction{
      n.or_replace == SYNTAQLITE_BOOL_TRUE,
      FunctionPrototype{SpanText(p, n.function_name), std::move(args)},
      std::move(returns),
      NodeSource(rb, n.select),
      "",
      std::nullopt,
  });
}

base::StatusOr<Statement> ParseCreateDelegatingFunction(
    SyntaqliteParser* p,
    const SyntaqliteCreatePerfettoDelegatingFunctionStmt& n) {
  ASSIGN_OR_RETURN(auto args, BuildArgDefs(p, n.args));
  // Variadic argument, if present, must be the last in the list.
  for (uint32_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i].is_variadic())
      return base::ErrStatus("Variadic argument must be the last argument");
  }
  ASSIGN_OR_RETURN(auto returns, BuildReturnType(p, n.return_type));
  return Statement(PerfettoSqlParser::CreateFunction{
      n.or_replace == SYNTAQLITE_BOOL_TRUE,
      FunctionPrototype{SpanText(p, n.function_name), std::move(args)},
      std::move(returns),
      SqlSource::FromTraceProcessorImplementation(""),
      "",
      SpanText(p, n.delegate_to),
  });
}

Statement ParseCreateIndex(SyntaqliteParser* p,
                           const SyntaqliteCreatePerfettoIndexStmt& n) {
  std::vector<std::string> col_names;
  if (syntaqlite_node_is_present(n.columns)) {
    const auto* list = static_cast<const SyntaqlitePerfettoIndexedColumnList*>(
        syntaqlite_parser_node(p, n.columns));
    uint32_t count = syntaqlite_list_count(list);
    for (uint32_t i = 0; i < count; i++) {
      const auto* col = static_cast<const SyntaqlitePerfettoIndexedColumn*>(
          syntaqlite_list_child(p, list, i));
      col_names.push_back(SpanText(p, col->column_name));
    }
  }
  return PerfettoSqlParser::CreateIndex{
      n.or_replace == SYNTAQLITE_BOOL_TRUE,
      SpanText(p, n.index_name),
      SpanText(p, n.table_name),
      std::move(col_names),
  };
}

Statement ParseCreateMacro(SyntaqliteParser* p,
                           const SqlSource& stmt,
                           uint32_t stmt_doc_offset,
                           const SyntaqliteCreatePerfettoMacroStmt& n) {
  // Macro definitions can't appear inside macro expansions, so every span
  // on this statement lives in the original source - slicing `stmt`
  // directly keeps line/col information intact for error messages.  The
  // spans carry statement-relative offsets, shifted back to document
  // coordinates by `stmt_doc_offset`.
  auto span_src = [&](SyntaqliteTextSpan s) {
    return stmt.Substr(s.offset + stmt_doc_offset, s.length);
  };
  std::vector<std::pair<SqlSource, SqlSource>> args;
  if (syntaqlite_node_is_present(n.args)) {
    const auto* list = static_cast<const SyntaqlitePerfettoMacroArgList*>(
        syntaqlite_parser_node(p, n.args));
    uint32_t count = syntaqlite_list_count(list);
    for (uint32_t i = 0; i < count; i++) {
      const auto* arg = static_cast<const SyntaqlitePerfettoMacroArg*>(
          syntaqlite_list_child(p, list, i));
      PERFETTO_CHECK(syntaqlite_span_is_macro_free(&arg->arg_name));
      PERFETTO_CHECK(syntaqlite_span_is_macro_free(&arg->arg_type));
      args.emplace_back(span_src(arg->arg_name), span_src(arg->arg_type));
    }
  }
  PERFETTO_CHECK(syntaqlite_span_is_macro_free(&n.macro_name));
  PERFETTO_CHECK(syntaqlite_span_is_macro_free(&n.return_type));
  PERFETTO_CHECK(syntaqlite_span_is_macro_free(&n.body));
  return PerfettoSqlParser::CreateMacro{
      n.or_replace == SYNTAQLITE_BOOL_TRUE,
      span_src(n.macro_name),
      std::move(args),
      span_src(n.return_type),
      span_src(n.body),
  };
}

base::StatusOr<Statement> ParseStatement(SyntaqliteParser* p,
                                         const MacroRewriteBuilder& rb,
                                         const SqlSource& stmt,
                                         uint32_t stmt_doc_offset,
                                         const SyntaqliteNode* node) {
  // Cast to int to suppress -Wswitch-enum: we intentionally handle only
  // Perfetto-dialect node types; all SQLite statement types fall through to
  // the default case and are returned as SqliteSql{}.
  switch (static_cast<int>(node->tag)) {
    case SYNTAQLITE_NODE_CREATE_PERFETTO_TABLE_STMT:
      return ParseCreateTable(p, rb, node->create_perfetto_table_stmt);
    case SYNTAQLITE_NODE_CREATE_PERFETTO_VIEW_STMT:
      return ParseCreateView(p, rb, node->create_perfetto_view_stmt);
    case SYNTAQLITE_NODE_CREATE_PERFETTO_FUNCTION_STMT:
      return ParseCreateFunction(p, rb, node->create_perfetto_function_stmt);
    case SYNTAQLITE_NODE_CREATE_PERFETTO_DELEGATING_FUNCTION_STMT:
      return ParseCreateDelegatingFunction(
          p, node->create_perfetto_delegating_function_stmt);
    case SYNTAQLITE_NODE_CREATE_PERFETTO_INDEX_STMT:
      return ParseCreateIndex(p, node->create_perfetto_index_stmt);
    case SYNTAQLITE_NODE_CREATE_PERFETTO_MACRO_STMT:
      return ParseCreateMacro(p, stmt, stmt_doc_offset,
                              node->create_perfetto_macro_stmt);
    case SYNTAQLITE_NODE_INCLUDE_PERFETTO_MODULE_STMT:
      return Statement(PerfettoSqlParser::Include{
          SpanText(p, node->include_perfetto_module_stmt.module_name),
      });
    case SYNTAQLITE_NODE_DROP_PERFETTO_INDEX_STMT:
      return Statement(PerfettoSqlParser::DropIndex{
          SpanText(p, node->drop_perfetto_index_stmt.index_name),
          SpanText(p, node->drop_perfetto_index_stmt.table_name),
      });
    default:
      return Statement(PerfettoSqlParser::SqliteSql{});
  }
}

// ---------------------------------------------------------------------------
// SyntaqliteMacroImpl
// ---------------------------------------------------------------------------
//
// Macro expansion + statement splitting both delegated to syntaqlite.
// Bridges syntaqlite's macro-lookup callback to a small intrinsic expander
// (preprocessor-compat shims) and the engine-owned user macro registry.

struct SyntaqliteMacroImpl {
  // [[maybe_unused]]: when `kUseSyntaqliteMacros` is false, the
  // `if constexpr` branches that call this constructor and `Next` below are
  // discarded, leaving them with no in-TU callers. The class still needs to
  // be defined because `std::optional<SyntaqliteMacroImpl>` is held by Impl.
  [[maybe_unused]] SyntaqliteMacroImpl(
      SqlSource src,
      const base::FlatHashMap<std::string, Macro>& m)
      : source(std::move(src)), macros(m) {
    synq = syntaqlite_parser_create_with_dialect(nullptr,
                                                 syntaqlite_perfetto_dialect());
    PERFETTO_CHECK(synq != nullptr);
    PERFETTO_CHECK(syntaqlite_parser_set_collect_node_extents(synq, 1) == 0);
    syntaqlite_parser_set_macro_lookup(synq, &SyntaqliteMacroImpl::LookupMacro,
                                       this);
    syntaqlite_parser_reset(synq, source.sql().data(),
                            static_cast<uint32_t>(source.sql().size()));
  }

  ~SyntaqliteMacroImpl() { syntaqlite_parser_destroy(synq); }

  SyntaqliteMacroImpl(const SyntaqliteMacroImpl&) = delete;
  SyntaqliteMacroImpl& operator=(const SyntaqliteMacroImpl&) = delete;
  SyntaqliteMacroImpl(SyntaqliteMacroImpl&&) = delete;
  SyntaqliteMacroImpl& operator=(SyntaqliteMacroImpl&&) = delete;

  [[maybe_unused]] bool Next(std::optional<SqlSource>& out_statement_sql);

  // Bridges syntaqlite's macro-lookup callback to our intrinsic expander
  // and user macro registry. Return codes are defined by syntaqlite:
  //   0   success (a result was published via set_result)
  //  -1   macro does not exist
  //  -2   expansion error
  static int LookupMacro(void* user_data,
                         SyntaqliteParser* parser,
                         const char* name,
                         uint32_t name_len,
                         const SyntaqliteToken* args,
                         uint32_t arg_count) {
    auto* self = static_cast<SyntaqliteMacroImpl*>(user_data);
    std::string_view name_sv(name, name_len);

    auto status = self->intrinsic_expander.TryExpand(name_sv, args, arg_count);
    if (status == perfetto_sql::ExpandStatus::kExpanded) {
      std::string_view body = self->intrinsic_expander.body();
      syntaqlite_macro_expansion_set_result(
          parser, body.data(), static_cast<uint32_t>(body.size()), 0, 0);
      return 0;
    }
    if (status == perfetto_sql::ExpandStatus::kExpansionFailed)
      return -2;

    const Macro* macro = self->macros.Find(name_sv);
    if (!macro)
      return -1;
    if (macro->args.size() != arg_count)
      return -2;

    self->param_names.clear();
    self->param_name_lens.clear();
    self->param_names.reserve(macro->args.size());
    self->param_name_lens.reserve(macro->args.size());
    for (const auto& a : macro->args) {
      self->param_names.push_back(a.data());
      self->param_name_lens.push_back(static_cast<uint32_t>(a.size()));
    }
    // PASSTHROUGH_UNKNOWN: leave `$param` references that don't match a
    // macro parameter alone, so they reach SQLite as bind variables (used
    // by CREATE PERFETTO FUNCTION bodies).
    int rc = syntaqlite_macro_expansion_expand_and_set_result(
        parser, macro->sql.sql().data(),
        static_cast<uint32_t>(macro->sql.sql().size()),
        self->param_names.data(), self->param_name_lens.data(),
        static_cast<uint32_t>(macro->args.size()),
        SYNTAQLITE_EXPAND_PASSTHROUGH_UNKNOWN);
    return rc == 0 ? 0 : -2;
  }

  SyntaqliteParser* synq;
  SqlSource source;
  const base::FlatHashMap<std::string, Macro>& macros;
  base::Status status;
  std::optional<Statement> current_statement;
  perfetto_sql::IntrinsicMacroExpander intrinsic_expander;
  // Scratch buffers for LookupMacro, reused across user-macro lookups.
  std::vector<const char*> param_names;
  std::vector<uint32_t> param_name_lens;
};

bool SyntaqliteMacroImpl::Next(std::optional<SqlSource>& out_statement_sql) {
  PERFETTO_DCHECK(status.ok());

  current_statement = std::nullopt;
  out_statement_sql = std::nullopt;

  const SqlSource& stmt = source;
  uint32_t root = 0;
  for (;;) {
    int32_t rc = syntaqlite_parser_next(synq);
    if (rc == SYNTAQLITE_PARSE_DONE)
      return false;
    if (rc == SYNTAQLITE_PARSE_ERROR) {
      // error_offset is statement-relative; shift by the statement's
      // position within the source to get a document offset.
      uint64_t off = uint64_t{syntaqlite_result_error_offset(synq)} +
                     CurrentStatementDocOffset(synq);
      auto clamped =
          static_cast<uint32_t>(std::min<uint64_t>(off, stmt.sql().size()));
      status = base::ErrStatus("%s%s", stmt.AsTraceback(clamped).c_str(),
                               syntaqlite_result_error_msg(synq));
      return false;
    }
    root = syntaqlite_result_root(synq);
    // PARSE_OK with no root node means bare comments/whitespace between
    // statements; skip to the next statement.
    if (syntaqlite_node_is_present(root))
      break;
  }

  // Every layer-0 offset the parser emits (node extents, spans, macro
  // rewrite call offsets) is relative to this position within the bound
  // source; callers that slice `stmt` add it back on.
  uint32_t stmt_doc_offset = CurrentStatementDocOffset(synq);

  MacroRewriteBuilder rb(synq, stmt, stmt_doc_offset, macros);
  auto root_src = rb.NodeSource(root);
  out_statement_sql = root_src.has_value() ? *std::move(root_src) : stmt;

  const auto* node =
      static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(synq, root));
  auto result = ParseStatement(synq, rb, stmt, stmt_doc_offset, node);
  if (!result.ok()) {
    status = result.status();
    return false;
  }
  current_statement = std::move(*result);
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// PerfettoSqlParser::Impl
// ---------------------------------------------------------------------------
//
// Thin wrapper that holds at most one backend implementation, chosen at
// compile time by `kUseSyntaqliteMacros`. Both halves are compiled in for
// the duration of the cutover; flipping the constant is a one-line change.

struct PerfettoSqlParser::Impl {
  Impl(SqlSource source, const base::FlatHashMap<std::string, Macro>& macros) {
    if constexpr (kUseSyntaqliteMacros) {
      syntaqlite_macros.emplace(std::move(source), macros);
    } else {
      legacy.emplace(std::move(source), macros);
    }
  }

  std::optional<LegacyImpl> legacy;
  std::optional<SyntaqliteMacroImpl> syntaqlite_macros;
};

PerfettoSqlParser::PerfettoSqlParser(
    SqlSource source,
    const base::FlatHashMap<std::string, Macro>& macros)
    : impl_(std::make_unique<Impl>(std::move(source), macros)) {}

PerfettoSqlParser::~PerfettoSqlParser() = default;

bool PerfettoSqlParser::Next() {
  if constexpr (kUseSyntaqliteMacros) {
    return impl_->syntaqlite_macros->Next(statement_sql_);
  } else {
    return impl_->legacy->Next(statement_sql_);
  }
}

const PerfettoSqlParser::Statement& PerfettoSqlParser::statement() const {
  if constexpr (kUseSyntaqliteMacros) {
    PERFETTO_DCHECK(impl_->syntaqlite_macros->current_statement.has_value());
    return *impl_->syntaqlite_macros->current_statement;
  } else {
    PERFETTO_DCHECK(impl_->legacy->current_statement.has_value());
    return *impl_->legacy->current_statement;
  }
}

const base::Status& PerfettoSqlParser::status() const {
  if constexpr (kUseSyntaqliteMacros) {
    return impl_->syntaqlite_macros->status;
  } else {
    return impl_->legacy->status;
  }
}

}  // namespace perfetto::trace_processor
