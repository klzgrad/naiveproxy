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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_LINEAGE_RESOLVER_LINEAGE_RESOLVER_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_LINEAGE_RESOLVER_LINEAGE_RESOLVER_H_

#include <string>
#include <unordered_set>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"

namespace perfetto::trace_processor::lineage_resolver {

// A reference to a symbol defined by another module.
struct SymbolRef {
  std::string name;
  std::string kind;  // "table" | "view" | "function" | "macro"
};

// Cross-module references bucketed by the dotted module name that defines
// them, e.g. { "slices.with_context": [{name:"thread_slice", kind:"view"}] }.
using SymbolRefsByModule =
    base::FlatHashMap<std::string, std::vector<SymbolRef>>;

// One CREATE PERFETTO {TABLE,VIEW,FUNCTION,MACRO} in a module, with its
// own resolved dependencies. Macro invocations are recorded under `uses`
// alongside table/view/function refs; through macro expansion, references
// from inside macro bodies appear here too (attributed to the symbol that
// invoked the macro).
struct DefinedSymbol {
  std::string name;
  std::string kind;  // "table" | "view" | "function" | "macro"

  // Resolved cross-module references, grouped by defining module.
  SymbolRefsByModule uses;
  // Same shape but for modules under prelude.* (auto-loaded by the runtime).
  SymbolRefsByModule implicit_uses;
  // Names referenced but not defined by any module in the configured trees —
  // typically C++ tables/functions, SQLite builtins, etc.
  std::unordered_set<std::string> intrinsics_or_external;
};

// All the lineage info for a single .sql module.
struct ResolvedModule {
  // Dotted module name (e.g. "slices.with_context").
  std::string module;
  // Path relative to the owning tree (e.g. "slices/with_context.sql").
  std::string path;
  // Absolute root of the tree this module came from.
  std::string tree_root;

  // One entry per CREATE PERFETTO {TABLE,VIEW,FUNCTION,MACRO} in the file,
  // in source order.
  std::vector<DefinedSymbol> symbols;

  // INCLUDE PERFETTO MODULE statements as authored.
  std::vector<std::string> declared_includes;
  // Non-prelude modules used by any symbol but not in declared_includes.
  std::vector<std::string> missing_includes;

  // Per-statement parse errors. Empty on success.
  std::vector<std::string> errors;
};

// Resolves cross-module lineage for one or more trees of PerfettoSQL files.
//
// Treats `prelude.*` modules as implicitly available (the runtime engine
// auto-loads them, so callers needn't `INCLUDE` them). Expands `__intrinsic_*`
// macros using the same C++ expander the engine uses at runtime; expands user
// macros from the configured trees recursively, so references inside macro
// bodies appear in the resolved record of whoever invoked the macro.
//
// Usage:
//   Resolver r;
//   r.AddTreeRoot("/path/to/stdlib");
//   r.AddTreeRoot("/path/to/other");
//   for (const ResolvedModule& m : r.Resolve()) { ... }
class Resolver {
 public:
  // Adds an absolute tree root to discover .sql files in. The path within
  // the tree determines the module name (`/` → `.`, `.sql` stripped). All
  // trees share one module namespace; first-added-wins on name collisions.
  void AddTreeRoot(std::string absolute_root);

  // Discovers files, parses, resolves. Returns one record per module sorted
  // by module name.
  std::vector<ResolvedModule> Resolve();

 private:
  std::vector<std::string> tree_roots_;
};

}  // namespace perfetto::trace_processor::lineage_resolver

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_LINEAGE_RESOLVER_LINEAGE_RESOLVER_H_
