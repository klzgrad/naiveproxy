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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_FLAMEGRAPH_FLAMEGRAPH_FUNCTION_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_FLAMEGRAPH_FLAMEGRAPH_FUNCTION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/flamegraph/flamegraph.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/module_state_manager.h"

namespace perfetto::trace_processor {

struct FlamegraphRegexSpec {
  std::string pattern;
  bool case_insensitive = false;
};

struct FlamegraphFilterSpec {
  enum class Kind {
    kShowStack,
    kHideStack,
    kHideFrame,
  };
  Kind kind;
  FlamegraphRegexSpec regex;
};

struct FlamegraphAggregateSpec {
  flamegraph::Config::Aggregate aggregate;
  std::string input_name;
  std::string output_name;
};

// Query-specific configuration passed from the scalar config builder to the
// virtual table in the same SQLite statement.
struct FlamegraphQuery {
  flamegraph::Config::View view =
      flamegraph::Config::View(flamegraph::Config::TopDown{});
  std::optional<FlamegraphRegexSpec> view_pattern;
  std::vector<FlamegraphFilterSpec> filters;
  std::vector<std::string> grouping_columns;
  std::vector<std::string> value_columns;
  std::vector<FlamegraphAggregateSpec> aggregate_columns;
};

// Marker predicate overloaded by the flamegraph virtual table to select its
// synthetic super-root row.
struct FlamegraphFindFunction
    : public sqlite::Function<FlamegraphFindFunction> {
  static constexpr char kName[] = "__intrinsic_flamegraph_find";
  static constexpr int kArgCount = 2;
  using UserData = void;

  static void Step(sqlite3_context*, int, sqlite3_value**);
};

// Builds the opaque argument consumed by a flamegraph virtual table call.
struct FlamegraphConfigFunction
    : public sqlite::Function<FlamegraphConfigFunction> {
  static constexpr char kName[] = "__intrinsic_flamegraph_config";
  static constexpr int kArgCount = -1;
  using UserData = StringPool;

  static void Step(sqlite3_context*, int argc, sqlite3_value** argv);
};

// Create-only virtual table operator. The module argument is the immutable raw
// source table; calls on the created table provide only a FlamegraphQuery.
struct FlamegraphOperator : sqlite::Module<FlamegraphOperator> {
  struct State {
    StringPool* pool = nullptr;
    core::Tree source;
    std::vector<std::string> property_names;
    std::vector<std::string> candidate_value_names;
    double source_value_sum = 0;
    // Schema column indices, fixed at creation time.
    uint32_t candidate_column_start = 0;
    uint32_t output_column_count = 0;
  };

  struct Context : sqlite::ModuleStateManager<FlamegraphOperator> {
    Context(PerfettoSqlConnection* _connection, StringPool* _pool)
        : sqlite::ModuleStateManager<FlamegraphOperator>(owned_store_),
          connection(_connection),
          pool(_pool) {}

    PerfettoSqlConnection* connection;
    StringPool* pool;

   private:
    sqlite::CommittedStateManager owned_store_;
  };

  struct Vtab : sqlite::Module<FlamegraphOperator>::Vtab {
    sqlite::ModuleStateManager<FlamegraphOperator>::PerVtabState* state;
  };

  struct Cursor : sqlite::Module<FlamegraphOperator>::Cursor {
    std::unique_ptr<core::Tree> result;
    // Schema column index -> result tree column (null where the result has
    // no such column). Resolved once per query.
    std::vector<const core::Tree::Column*> columns;
    flamegraph::Layout layout;
    bool value_is_int = false;
    uint32_t row = 0;
    bool super_root_only = false;
  };

  static constexpr auto kType = kCreateOnly;
  static constexpr bool kSupportsWrites = false;
  static constexpr bool kDoesOverloadFunctions = true;

  static int Create(sqlite3*,
                    void*,
                    int,
                    const char* const*,
                    sqlite3_vtab**,
                    char**);
  static int Destroy(sqlite3_vtab*);
  static int Connect(sqlite3*,
                     void*,
                     int,
                     const char* const*,
                     sqlite3_vtab**,
                     char**);
  static int Disconnect(sqlite3_vtab*);
  static int BestIndex(sqlite3_vtab*, sqlite3_index_info*);
  static int Open(sqlite3_vtab*, sqlite3_vtab_cursor**);
  static int Close(sqlite3_vtab_cursor*);
  static int Filter(sqlite3_vtab_cursor*,
                    int,
                    const char*,
                    int,
                    sqlite3_value**);
  static int Next(sqlite3_vtab_cursor*);
  static int Eof(sqlite3_vtab_cursor*);
  static int Column(sqlite3_vtab_cursor*, sqlite3_context*, int);
  static int Rowid(sqlite3_vtab_cursor*, sqlite_int64*);
  static int FindFunction(sqlite3_vtab*,
                          int,
                          const char*,
                          FindFunctionFn**,
                          void**);

  static int Begin(sqlite3_vtab*) { return SQLITE_OK; }
  static int Sync(sqlite3_vtab*) { return SQLITE_OK; }
  static int Commit(sqlite3_vtab*) { return SQLITE_OK; }
  static int Rollback(sqlite3_vtab*) { return SQLITE_OK; }
  static int Savepoint(sqlite3_vtab* table, int savepoint) {
    Vtab* vtab = GetVtab(table);
    sqlite::ModuleStateManager<FlamegraphOperator>::OnSavepoint(vtab->state,
                                                                savepoint);
    return SQLITE_OK;
  }
  static int Release(sqlite3_vtab* table, int savepoint) {
    Vtab* vtab = GetVtab(table);
    sqlite::ModuleStateManager<FlamegraphOperator>::OnRelease(vtab->state,
                                                              savepoint);
    return SQLITE_OK;
  }
  static int RollbackTo(sqlite3_vtab* table, int savepoint) {
    Vtab* vtab = GetVtab(table);
    sqlite::ModuleStateManager<FlamegraphOperator>::OnRollbackTo(vtab->state,
                                                                 savepoint);
    return SQLITE_OK;
  }

  static constexpr sqlite3_module kModule = CreateModule();
};

}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::flamegraph {

void RegisterPlugin();

}  // namespace perfetto::trace_processor::flamegraph

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_FLAMEGRAPH_FLAMEGRAPH_FUNCTION_H_
