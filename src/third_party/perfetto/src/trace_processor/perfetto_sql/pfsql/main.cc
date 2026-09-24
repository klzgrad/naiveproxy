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

// pfsql: offline tool for working with PerfettoSQL source files. Does not
// load a trace. Each subcommand defines its own input shape.
//
// Subcommands:
//   lineage   Cross-module dependency graph over `.sql` trees.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "src/trace_processor/perfetto_sql/lineage_resolver/lineage_resolver.h"
#include "src/trace_processor/util/json_value.h"
#include "src/trace_processor/util/simple_json_serializer.h"

namespace perfetto::trace_processor::pfsql {
namespace {

// ---------- lineage subcommand ----------

std::string DirOf(const std::string& path) {
  auto slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string ResolvePath(const std::string& base_dir, const std::string& p) {
  return (!p.empty() && p[0] == '/') ? p : base_dir + "/" + p;
}

base::Status LoadConfig(const std::string& config_path,
                        lineage_resolver::Resolver& resolver) {
  std::string text;
  if (!base::ReadFile(config_path, &text))
    return base::ErrStatus("failed to read config %s", config_path.c_str());
  auto parsed = json::Parse(text);
  if (!parsed.ok())
    return base::ErrStatus("invalid JSON in %s: %s", config_path.c_str(),
                           parsed.status().c_message());
  const json::Dom& doc = *parsed;
  if (!doc.IsObject() || !doc.HasMember("trees") || !doc["trees"].IsArray())
    return base::ErrStatus("config must be { \"trees\": [ ... ] }");
  const std::string base_dir = DirOf(config_path);
  bool empty = true;
  for (const auto& t : doc["trees"]) {
    std::string root;
    if (t.IsString()) {
      root = t.AsString();
    } else if (t.IsObject() && t.HasMember("root") && t["root"].IsString()) {
      root = t["root"].AsString();
    } else {
      return base::ErrStatus(
          "each tree must be a string or { \"root\": \"...\" }");
    }
    resolver.AddTreeRoot(ResolvePath(base_dir, root));
    empty = false;
  }
  return empty ? base::ErrStatus("config has no trees") : base::OkStatus();
}

// Snapshot a SymbolRefsByModule into a sorted vector so JSON output is stable.
using SortedUseEntry =
    std::pair<std::string, const std::vector<lineage_resolver::SymbolRef>*>;
std::vector<SortedUseEntry> SortedUses(
    const lineage_resolver::SymbolRefsByModule& by_mod) {
  std::vector<SortedUseEntry> out;
  for (auto it = by_mod.GetIterator(); it; ++it)
    out.emplace_back(it.key(), &it.value());
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

std::vector<std::string> SortedStrings(
    const std::unordered_set<std::string>& set) {
  std::vector<std::string> out(set.begin(), set.end());
  std::sort(out.begin(), out.end());
  return out;
}

std::string EmitLineageJson(
    const std::vector<lineage_resolver::ResolvedModule>& ms) {
  auto write_strings = [](const auto& items) {
    return [&items](json::JsonArraySerializer& a) {
      for (const auto& s : items)
        a.AppendString(s);
    };
  };
  auto write_use_map = [](const lineage_resolver::SymbolRefsByModule& by_mod) {
    auto sorted = SortedUses(by_mod);
    return [sorted = std::move(sorted)](json::JsonDictSerializer& d) {
      for (const auto& kv : sorted) {
        d.AddArray(kv.first, [&kv](json::JsonArraySerializer& a) {
          for (const auto& u : *kv.second) {
            a.AppendDict([&u](json::JsonDictSerializer& e) {
              e.AddString("name", u.name);
              e.AddString("kind", u.kind);
            });
          }
        });
      }
    };
  };

  std::string out = json::SerializeJson([&](json::JsonValueSerializer&& w) {
    std::move(w).WriteDict([&](json::JsonDictSerializer& root) {
      root.AddArray("modules", [&](json::JsonArraySerializer& arr) {
        for (const auto& m : ms) {
          arr.AppendDict([&](json::JsonDictSerializer& mod) {
            mod.AddString("module", m.module);
            mod.AddString("path", m.path);
            mod.AddArray("declared_includes",
                         write_strings(m.declared_includes));
            mod.AddArray("symbols", [&](json::JsonArraySerializer& syms) {
              for (const auto& s : m.symbols) {
                syms.AppendDict([&](json::JsonDictSerializer& sd) {
                  sd.AddString("name", s.name);
                  sd.AddString("kind", s.kind);
                  sd.AddDict("uses", write_use_map(s.uses));
                  sd.AddDict("implicit_uses", write_use_map(s.implicit_uses));
                  sd.AddArray(
                      "intrinsics_or_external",
                      write_strings(SortedStrings(s.intrinsics_or_external)));
                });
              }
            });
            mod.AddArray("missing_includes", write_strings(m.missing_includes));
            mod.AddArray("errors", write_strings(m.errors));
          });
        }
      });
    });
  });
  out.push_back('\n');
  return out;
}

const char* kLineageHelp = R"(Usage:
  pfsql lineage <tree_path>...
  pfsql lineage --config <file.json>

Cross-module dependency graph over PerfettoSQL `.sql` trees. Input is given
EITHER as positional tree paths (resolved against the CWD) OR as a JSON
config (not both). The JSON shape is:
  { "trees": [ "path/to/stdlib",
               { "root": "path/to/other" } ] }
Paths inside the JSON are resolved against the config's dir.

All trees share one module namespace; the path within each tree determines
the dotted module name (`slices/with_context.sql` -> `slices.with_context`).
First-tree-wins on name collisions.

Anything under `prelude.*` is treated as auto-included (the runtime engine
auto-loads it). Macros are expanded recursively — references inside macro
bodies surface in the resolved record of the invoking symbol.

Output: a single JSON object on stdout with a `modules` array, one entry per
module:
  - module / path
  - declared_includes:  authored INCLUDE PERFETTO MODULE stmts
  - symbols:            one entry per CREATE PERFETTO
                        TABLE/VIEW/FUNCTION/MACRO, in source order. Each
                        carries its OWN:
                          - name / kind
                          - uses:           cross-module refs by defining
                                            module
                          - implicit_uses:  refs into prelude.*
                          - intrinsics_or_external:
                                            bare names not defined anywhere
  - missing_includes:   non-prelude modules used by any symbol but not
                        declared
  - errors:             parse errors or symbol-name collisions
)";

int RunLineage(int argc, char** argv) {
  std::string config_path;
  static const option long_opts[] = {
      {"config", required_argument, nullptr, 'c'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0}};
  optind = 1;  // restart getopt
  for (;;) {
    int c = getopt_long(argc, argv, "c:h", long_opts, nullptr);
    if (c < 0)
      break;
    if (c == 'c') {
      config_path = optarg;
    } else if (c == 'h') {
      fputs(kLineageHelp, stdout);
      return 0;
    } else {
      fputs(kLineageHelp, stderr);
      return 1;
    }
  }
  std::vector<std::string> tree_paths;
  for (int i = optind; i < argc; ++i)
    tree_paths.emplace_back(argv[i]);

  const bool have_config = !config_path.empty();
  const bool have_paths = !tree_paths.empty();
  if (have_config && have_paths) {
    fprintf(stderr,
            "pfsql lineage: pass either positional tree paths or --config, "
            "not both\n");
    return 1;
  }
  if (!have_config && !have_paths) {
    fprintf(stderr,
            "pfsql lineage: expected one or more tree paths, or --config "
            "FILE\n");
    return 1;
  }

  lineage_resolver::Resolver resolver;
  if (have_config) {
    if (auto st = LoadConfig(config_path, resolver); !st.ok()) {
      fprintf(stderr, "%s\n", st.c_message());
      return 1;
    }
  } else {
    for (const auto& p : tree_paths)
      resolver.AddTreeRoot(p);
  }
  std::string s = EmitLineageJson(resolver.Resolve());
  fwrite(s.data(), 1, s.size(), stdout);
  return 0;
}

// ---------- top-level dispatcher ----------

const char* kTopLevelHelp =
    R"(pfsql: offline tool for working with PerfettoSQL.

Usage: pfsql <subcommand> [args]

Subcommands:
  lineage   Cross-module dependency graph over `.sql` trees.

Run 'pfsql <subcommand> --help' for subcommand details.
)";

int Main(int argc, char** argv) {
  if (argc < 2) {
    fputs(kTopLevelHelp, stderr);
    return 1;
  }
  std::string_view sub(argv[1]);
  if (sub == "-h" || sub == "--help" || sub == "help") {
    fputs(kTopLevelHelp, stdout);
    return 0;
  }
  if (sub == "lineage") {
    // Shift argv so getopt sees `pfsql lineage [args]` as `lineage [args]`.
    return RunLineage(argc - 1, argv + 1);
  }
  fprintf(stderr, "pfsql: unknown subcommand '%.*s'\n\n",
          static_cast<int>(sub.size()), sub.data());
  fputs(kTopLevelHelp, stderr);
  return 1;
}

}  // namespace
}  // namespace perfetto::trace_processor::pfsql

int main(int argc, char** argv) {
  return perfetto::trace_processor::pfsql::Main(argc, argv);
}
