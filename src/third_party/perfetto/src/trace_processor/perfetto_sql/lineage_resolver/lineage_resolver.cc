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

#include "src/trace_processor/perfetto_sql/lineage_resolver/lineage_resolver.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/perfetto_sql/parser/intrinsic_macro_expansion.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/syntaqlite_perfetto.h"
#include "src/trace_processor/perfetto_sql/syntaqlite/utils.h"

namespace perfetto::trace_processor::lineage_resolver {
namespace {

using NameSet = std::unordered_set<std::string>;

constexpr std::string_view kPreludePrefix = "prelude.";

bool IsPreludeModule(const std::string& m) {
  return m.size() >= kPreludePrefix.size() &&
         std::string_view(m).substr(0, kPreludePrefix.size()) == kPreludePrefix;
}

// "slices/with_context.sql" → "slices.with_context".
std::string ModuleNameFromRelPath(const std::string& rel) {
  std::string r = rel;
  constexpr std::string_view kSuffix = ".sql";
  if (r.size() >= kSuffix.size() &&
      std::string_view(r).substr(r.size() - kSuffix.size()) == kSuffix) {
    r.resize(r.size() - kSuffix.size());
  }
  for (char& c : r) {
    if (c == '/')
      c = '.';
  }
  return r;
}

// Raw (unresolved) refs collected from a single CREATE PERFETTO * statement's
// AST. Materialised into
// DefinedSymbol::uses/implicit_uses/intrinsics_or_external once the global
// symbol index is built.
struct RawRefs {
  NameSet relation_refs;
  NameSet function_refs;
  NameSet macro_invocations;
};

// A defined symbol as seen during parsing, before resolution.
struct ScratchSymbol {
  std::string name;
  std::string kind;
  RawRefs refs;
};

struct ScratchModule {
  std::string module;
  std::string path;
  std::string tree_root;
  std::vector<ScratchSymbol> symbols;
  std::vector<std::string> includes;
  std::vector<std::string> errors;
};

struct SymbolIndex {
  struct Origin {
    std::string module;
    std::string kind;
  };
  base::FlatHashMap<std::string, Origin> by_name;
};

struct DiscoveredFile {
  std::string module;
  std::string tree_root;
  std::string rel_path;
};

struct MacroDef {
  std::string body;
  std::vector<std::string> param_names;
  std::vector<const char*> param_name_ptrs;
  std::vector<SyntaqliteLength> param_name_lens;
};

class MacroRegistry {
 public:
  void Add(std::string name, MacroDef def) {
    def.param_name_ptrs.clear();
    def.param_name_lens.clear();
    def.param_name_ptrs.reserve(def.param_names.size());
    def.param_name_lens.reserve(def.param_names.size());
    for (const auto& p : def.param_names) {
      def.param_name_ptrs.push_back(p.data());
      def.param_name_lens.push_back(static_cast<SyntaqliteLength>(p.size()));
    }
    defs_.Insert(std::move(name), std::move(def));
  }
  const MacroDef* Lookup(std::string_view name) const {
    return defs_.Find(std::string(name));
  }
  perfetto_sql::IntrinsicMacroExpander& intrinsics() { return intrinsics_; }

 private:
  base::FlatHashMap<std::string, MacroDef> defs_;
  perfetto_sql::IntrinsicMacroExpander intrinsics_;
};

int MacroLookupCb(void* user_data,
                  SyntaqliteParser* parser,
                  const char* name,
                  SyntaqliteLength name_len,
                  const SyntaqliteToken* args,
                  uint32_t arg_count) {
  auto* reg = static_cast<MacroRegistry*>(user_data);
  std::string_view nm(name, name_len);

  // 1) Try the real C++-implemented intrinsic macros first — same expander
  // the runtime preprocessor uses, so expansions are byte-exact.
  switch (reg->intrinsics().TryExpand(nm, args, arg_count)) {
    case perfetto_sql::ExpandStatus::kExpanded: {
      std::string_view body = reg->intrinsics().body();
      syntaqlite_macro_expansion_set_result(
          parser, body.data(), static_cast<SyntaqliteLength>(body.size()), 0,
          0);
      return SYNTAQLITE_MACRO_LOOKUP_OK;
    }
    case perfetto_sql::ExpandStatus::kExpansionFailed:
      return SYNTAQLITE_MACRO_LOOKUP_ERROR;
    case perfetto_sql::ExpandStatus::kNotIntrinsic:
      break;
  }

  // 2) Fall through to the user macro registry built from `.sql` sources.
  if (const MacroDef* def = reg->Lookup(nm)) {
    int rc = syntaqlite_macro_expansion_expand_and_set_result(
        parser, def->body.data(),
        static_cast<SyntaqliteLength>(def->body.size()),
        def->param_name_ptrs.empty() ? nullptr : def->param_name_ptrs.data(),
        def->param_name_lens.empty() ? nullptr : def->param_name_lens.data(),
        static_cast<uint32_t>(def->param_names.size()),
        // Some macros embed runtime `$table` placeholders (resolved at execute
        // time by the table-pointer scan machinery, not by syntaqlite) — let
        // them pass through verbatim instead of failing the expansion.
        SYNTAQLITE_EXPAND_PASSTHROUGH_UNKNOWN);
    return rc < 0 ? SYNTAQLITE_MACRO_LOOKUP_ERROR : SYNTAQLITE_MACRO_LOOKUP_OK;
  }
  return SYNTAQLITE_MACRO_LOOKUP_NOT_FOUND;
}

struct ParserDeleter {
  void operator()(SyntaqliteParser* p) const { syntaqlite_parser_destroy(p); }
};
using ScopedParser = std::unique_ptr<SyntaqliteParser, ParserDeleter>;

std::string SpanString(SyntaqliteParser* p, SyntaqliteTextSpan span) {
  // Macro-arg drill-through can hand us spans with surrounding whitespace
  // (authored arg text is verbatim). Trim so identifiers compare cleanly.
  return std::string(base::TrimWhitespace(SyntaqliteSpanText(p, span)));
}

ScopedParser MakeParser(MacroRegistry* registry_for_lookup) {
  ScopedParser p(syntaqlite_parser_create_perfetto(nullptr));
  PERFETTO_CHECK(p != nullptr);
  syntaqlite_parser_set_macro_fallback(p.get(), 1);
  if (registry_for_lookup) {
    syntaqlite_parser_set_macro_lookup(p.get(), &MacroLookupCb,
                                       registry_for_lookup);
  }
  return p;
}

void ExtractMacroParamNames(SyntaqliteParser* p,
                            uint32_t arg_list_id,
                            std::vector<std::string>& out) {
  if (!syntaqlite_node_is_present(arg_list_id))
    return;
  const auto* list = static_cast<const SyntaqlitePerfettoMacroArgList*>(
      syntaqlite_parser_node(p, arg_list_id));
  uint32_t count = syntaqlite_list_count(list);
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id = syntaqlite_list_child_id(list, i);
    if (!syntaqlite_node_is_present(id))
      continue;
    const auto* item = static_cast<const SyntaqlitePerfettoMacroArg*>(
        syntaqlite_parser_node(p, id));
    if (!item)
      continue;
    out.emplace_back(SpanString(p, item->arg_name));
  }
}

// Walk every node in the current statement's arena and harvest references
// into `out`. CTE-local names are filtered out via a scratch set.
void CollectStatementRefs(SyntaqliteParser* p, RawRefs& out) {
  NameSet cte_locals;
  uint32_t n = syntaqlite_parser_node_count(p);
  for (uint32_t id = 1; id < n; ++id) {
    if (!syntaqlite_node_is_present(id))
      continue;
    const auto* node =
        static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(p, id));
    if (node && node->tag == SYNTAQLITE_NODE_CTE_DEFINITION) {
      cte_locals.insert(SpanString(p, node->cte_definition.cte_name));
    }
  }
  for (uint32_t id = 1; id < n; ++id) {
    if (!syntaqlite_node_is_present(id))
      continue;
    const auto* node =
        static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(p, id));
    if (!node)
      continue;
    switch (static_cast<int>(node->tag)) {
      case SYNTAQLITE_NODE_TABLE_REF: {
        std::string name = SpanString(p, node->table_ref.table_name);
        auto bang = name.find('!');
        if (bang != std::string::npos) {
          out.macro_invocations.insert(name.substr(0, bang));
          break;
        }
        if (name.empty() || cte_locals.count(name))
          break;
        out.relation_refs.insert(std::move(name));
        break;
      }
      case SYNTAQLITE_NODE_FUNCTION_CALL:
      case SYNTAQLITE_NODE_AGGREGATE_FUNCTION_CALL:
      case SYNTAQLITE_NODE_ORDERED_SET_FUNCTION_CALL: {
        SyntaqliteTextSpan span =
            node->tag == SYNTAQLITE_NODE_FUNCTION_CALL
                ? node->function_call.func_name
                : (node->tag == SYNTAQLITE_NODE_AGGREGATE_FUNCTION_CALL
                       ? node->aggregate_function_call.func_name
                       : node->ordered_set_function_call.func_name);
        std::string name = SpanString(p, span);
        if (name.empty())
          break;
        auto bang = name.find('!');
        if (bang != std::string::npos) {
          out.macro_invocations.insert(name.substr(0, bang));
          break;
        }
        out.function_refs.insert(std::move(name));
        break;
      }
      default:
        break;
    }
  }
  uint32_t rw_count = syntaqlite_result_macro_count(p);
  for (uint32_t i = 0; i < rw_count; ++i) {
    SyntaqliteMacroRewrite rw = syntaqlite_result_macro_rewrite_at(p, i);
    if (rw.name && rw.name_len > 0)
      out.macro_invocations.emplace(rw.name, rw.name_len);
  }
}

base::Status DiscoverTree(const std::string& root,
                          std::vector<DiscoveredFile>& out) {
  std::string trimmed = root;
  while (!trimmed.empty() && trimmed.back() == '/')
    trimmed.pop_back();
  std::vector<std::string> all;
  if (auto st = base::ListFilesRecursive(trimmed, all); !st.ok())
    return st;
  for (auto& rel : all) {
    if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, ".sql") != 0)
      continue;
    out.push_back({ModuleNameFromRelPath(rel), trimmed, std::move(rel)});
  }
  return base::OkStatus();
}

// Pass A: walk every file once to collect macro bodies (for the expansion
// registry) and all top-level defined-symbol names (for the global symbol
// index). We do NOT call CollectStatementRefs here — refs are gathered in
// pass B once macros can be expanded.
void PassA(const DiscoveredFile& f, ScratchModule& m, MacroRegistry& registry) {
  m.module = f.module;
  m.path = f.rel_path;
  m.tree_root = f.tree_root;

  std::string sql;
  if (!base::ReadFile(f.tree_root + "/" + f.rel_path, &sql)) {
    m.errors.push_back("failed to read file");
    return;
  }
  ScopedParser owned = MakeParser(/*registry_for_lookup=*/nullptr);
  SyntaqliteParser* p = owned.get();
  syntaqlite_parser_reset(p, sql.data(), static_cast<uint32_t>(sql.size()));

  for (;;) {
    int32_t rc = syntaqlite_parser_next(p);
    if (rc == SYNTAQLITE_PARSE_DONE)
      break;
    if (rc == SYNTAQLITE_PARSE_ERROR) {
      const char* msg = syntaqlite_result_error_msg(p);
      m.errors.push_back(msg ? std::string("passA: ") + msg
                             : std::string("passA: unknown parse error"));
      continue;
    }
    uint32_t root = syntaqlite_result_root(p);
    if (!syntaqlite_node_is_present(root))
      continue;
    const auto* node =
        static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(p, root));
    if (!node)
      continue;
    auto push_symbol = [&](std::string name, const char* kind) {
      ScratchSymbol s;
      s.name = std::move(name);
      s.kind = kind;
      m.symbols.push_back(std::move(s));
    };
    switch (static_cast<int>(node->tag)) {
      case SYNTAQLITE_NODE_INCLUDE_PERFETTO_MODULE_STMT:
        m.includes.push_back(
            SpanString(p, node->include_perfetto_module_stmt.module_name));
        break;
      case SYNTAQLITE_NODE_CREATE_PERFETTO_TABLE_STMT:
        push_symbol(SpanString(p, node->create_perfetto_table_stmt.table_name),
                    "table");
        break;
      case SYNTAQLITE_NODE_CREATE_PERFETTO_VIEW_STMT:
        push_symbol(SpanString(p, node->create_perfetto_view_stmt.view_name),
                    "view");
        break;
      case SYNTAQLITE_NODE_CREATE_PERFETTO_FUNCTION_STMT:
        push_symbol(
            SpanString(p, node->create_perfetto_function_stmt.function_name),
            "function");
        break;
      case SYNTAQLITE_NODE_CREATE_PERFETTO_DELEGATING_FUNCTION_STMT:
        push_symbol(
            SpanString(
                p,
                node->create_perfetto_delegating_function_stmt.function_name),
            "function");
        break;
      case SYNTAQLITE_NODE_CREATE_PERFETTO_MACRO_STMT: {
        const auto& n = node->create_perfetto_macro_stmt;
        std::string name = SpanString(p, n.macro_name);
        push_symbol(name, "macro");
        MacroDef def;
        def.body = SpanString(p, n.body);
        ExtractMacroParamNames(p, n.args, def.param_names);
        registry.Add(std::move(name), std::move(def));
        break;
      }
      default:
        break;
    }
  }
}

// Pass B: re-parse with the macro lookup callback so invocations expand and
// references inside macro bodies flow into the AST. For each defining
// statement, attach its arena's refs to the matching ScratchSymbol (matched
// by source-order index).
void PassB(const DiscoveredFile& f, MacroRegistry& registry, ScratchModule& m) {
  std::string sql;
  if (!base::ReadFile(f.tree_root + "/" + f.rel_path, &sql)) {
    m.errors.push_back("failed to read file (passB)");
    return;
  }
  ScopedParser owned = MakeParser(&registry);
  SyntaqliteParser* p = owned.get();
  syntaqlite_parser_reset(p, sql.data(), static_cast<uint32_t>(sql.size()));

  size_t sym_idx = 0;
  for (;;) {
    int32_t rc = syntaqlite_parser_next(p);
    if (rc == SYNTAQLITE_PARSE_DONE)
      break;
    if (rc == SYNTAQLITE_PARSE_ERROR) {
      const char* msg = syntaqlite_result_error_msg(p);
      m.errors.push_back(msg ? std::string("passB: ") + msg
                             : std::string("passB: unknown parse error"));
      continue;
    }
    uint32_t root = syntaqlite_result_root(p);
    if (!syntaqlite_node_is_present(root))
      continue;
    const auto* node =
        static_cast<const SyntaqliteNode*>(syntaqlite_parser_node(p, root));
    if (!node)
      continue;
    switch (static_cast<int>(node->tag)) {
      case SYNTAQLITE_NODE_CREATE_PERFETTO_TABLE_STMT:
      case SYNTAQLITE_NODE_CREATE_PERFETTO_VIEW_STMT:
      case SYNTAQLITE_NODE_CREATE_PERFETTO_FUNCTION_STMT:
        // The bodies of these have refs we care about. Match by source-order
        // index against the symbols we collected in pass A.
        if (sym_idx < m.symbols.size())
          CollectStatementRefs(p, m.symbols[sym_idx].refs);
        ++sym_idx;
        break;
      case SYNTAQLITE_NODE_CREATE_PERFETTO_DELEGATING_FUNCTION_STMT:
      case SYNTAQLITE_NODE_CREATE_PERFETTO_MACRO_STMT:
        // No body refs to collect; still advance the symbol cursor so later
        // entries stay aligned with pass A's order.
        ++sym_idx;
        break;
      default:
        break;
    }
  }
}

DefinedSymbol Materialise(const ScratchSymbol& scratch,
                          const std::string& self_module,
                          const SymbolIndex& idx,
                          NameSet& touched_non_prelude_modules) {
  DefinedSymbol out;
  out.name = scratch.name;
  out.kind = scratch.kind;

  auto resolve = [&](const std::string& name) {
    auto* o = idx.by_name.Find(name);
    if (!o) {
      out.intrinsics_or_external.insert(name);
      return;
    }
    if (o->module == self_module)
      return;  // Self-reference; ignore.
    SymbolRefsByModule& bucket_map =
        IsPreludeModule(o->module) ? out.implicit_uses : out.uses;
    auto* bucket = bucket_map.Find(o->module);
    if (!bucket) {
      bucket_map.Insert(o->module, std::vector<SymbolRef>{});
      bucket = bucket_map.Find(o->module);
    }
    bucket->push_back({name, o->kind});
    if (!IsPreludeModule(o->module))
      touched_non_prelude_modules.insert(o->module);
  };
  for (const auto& n : scratch.refs.relation_refs)
    resolve(n);
  for (const auto& n : scratch.refs.function_refs)
    resolve(n);
  for (const auto& n : scratch.refs.macro_invocations)
    resolve(n);
  return out;
}

// Two modules defining the same name is a bug — the runtime engine would
// reject it. Surface it loudly by appending an error to every involved
// module's errors list. Resolution itself uses first-defined-wins (which
// is deterministic given the sorted file order), but any collision entry
// here means the source needs fixing.
void FlagSymbolCollisions(std::vector<ScratchModule>& modules) {
  // name → indices into |modules| that define it.
  base::FlatHashMap<std::string, std::vector<size_t>> definers_by_name;
  for (size_t i = 0; i < modules.size(); ++i) {
    for (const auto& s : modules[i].symbols) {
      auto* v = definers_by_name.Find(s.name);
      if (!v) {
        definers_by_name.Insert(s.name, std::vector<size_t>{});
        v = definers_by_name.Find(s.name);
      }
      v->push_back(i);
    }
  }
  for (auto it = definers_by_name.GetIterator(); it; ++it) {
    const auto& defining_indices = it.value();
    if (defining_indices.size() < 2)
      continue;
    for (size_t i : defining_indices) {
      std::string msg =
          "symbol collision: '" + it.key() + "' is also defined in";
      bool first = true;
      for (size_t j : defining_indices) {
        if (j == i)
          continue;
        msg += first ? " " : ", ";
        msg += modules[j].module;
        first = false;
      }
      modules[i].errors.push_back(std::move(msg));
    }
  }
}

ResolvedModule MaterialiseModule(const ScratchModule& m,
                                 const SymbolIndex& idx) {
  ResolvedModule out;
  out.module = m.module;
  out.path = m.path;
  out.tree_root = m.tree_root;
  out.declared_includes = m.includes;
  out.errors = m.errors;

  NameSet touched_non_prelude_modules;
  out.symbols.reserve(m.symbols.size());
  for (const auto& s : m.symbols) {
    out.symbols.push_back(
        Materialise(s, m.module, idx, touched_non_prelude_modules));
  }

  NameSet declared(m.includes.begin(), m.includes.end());
  for (const auto& mod : touched_non_prelude_modules) {
    if (!declared.count(mod))
      out.missing_includes.push_back(mod);
  }
  std::sort(out.missing_includes.begin(), out.missing_includes.end());
  return out;
}

}  // namespace

void Resolver::AddTreeRoot(std::string absolute_root) {
  tree_roots_.push_back(std::move(absolute_root));
}

std::vector<ResolvedModule> Resolver::Resolve() {
  std::vector<DiscoveredFile> files;
  for (const auto& root : tree_roots_) {
    auto st = DiscoverTree(root, files);
    PERFETTO_DCHECK(st.ok());
    (void)st;
  }
  std::sort(files.begin(), files.end(),
            [](const DiscoveredFile& a, const DiscoveredFile& b) {
              return a.module < b.module;
            });
  {
    std::vector<DiscoveredFile> deduped;
    NameSet seen;
    for (auto& f : files) {
      if (seen.insert(f.module).second)
        deduped.push_back(std::move(f));
    }
    files = std::move(deduped);
  }

  MacroRegistry registry;
  std::vector<ScratchModule> scratch(files.size());
  for (size_t i = 0; i < files.size(); ++i)
    PassA(files[i], scratch[i], registry);

  // Global symbol index — first-defined wins (deterministic given the sorted
  // file order). Collisions are flagged separately as errors on every
  // involved module.
  SymbolIndex idx;
  for (const auto& m : scratch) {
    for (const auto& s : m.symbols) {
      if (!idx.by_name.Find(s.name))
        idx.by_name.Insert(s.name, SymbolIndex::Origin{m.module, s.kind});
    }
  }
  FlagSymbolCollisions(scratch);

  for (size_t i = 0; i < files.size(); ++i)
    PassB(files[i], registry, scratch[i]);

  std::vector<ResolvedModule> out;
  out.reserve(scratch.size());
  for (const auto& m : scratch)
    out.push_back(MaterialiseModule(m, idx));
  return out;
}

}  // namespace perfetto::trace_processor::lineage_resolver
