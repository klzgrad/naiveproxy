/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_STATIC_TABLE_FUNCTION_MODULE_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_STATIC_TABLE_FUNCTION_MODULE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/dataframe/cursor.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/module_state_manager.h"

namespace perfetto::trace_processor {

// Adapter class between SQLite and the Dataframe API. Allows SQLite to query
// and iterate over the results of a dataframe query.
struct StaticTableFunctionModule : sqlite::Module<StaticTableFunctionModule> {
  static constexpr auto kType = kCreateOnly;
  static constexpr bool kSupportsWrites = false;
  static constexpr bool kDoesOverloadFunctions = false;
  static constexpr bool kDoesSupportTransactions = true;

  struct State {
    explicit State(std::unique_ptr<StaticTableFunction> _function)
        : function(std::move(_function)) {}
    std::unique_ptr<StaticTableFunction> function;
  };
  struct Context : sqlite::ModuleStateManager<StaticTableFunctionModule> {
    std::unique_ptr<State> temporary_create_state;
  };
  struct Vtab : sqlite::Module<StaticTableFunctionModule>::Vtab {
    StaticTableFunction* function;
    sqlite::ModuleStateManager<StaticTableFunctionModule>::PerVtabState* state;
    std::string name;
    uint32_t output_count = 0;
    uint32_t arg_count = 0;
    int best_idx_num = 0;
  };
  struct Cursor : sqlite::Module<StaticTableFunctionModule>::Cursor {
    std::unique_ptr<StaticTableFunction::Cursor> cursor;
    dataframe::Cursor<DataframeModule::SqliteValueFetcher> df_cursor;
    std::vector<dataframe::FilterSpec> filters;
    std::vector<SqlValue> values;
  };

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

  static int Begin(sqlite3_vtab*) { return SQLITE_OK; }
  static int Sync(sqlite3_vtab*) { return SQLITE_OK; }
  static int Commit(sqlite3_vtab*) { return SQLITE_OK; }
  static int Rollback(sqlite3_vtab*) { return SQLITE_OK; }
  static int Savepoint(sqlite3_vtab* t, int r) {
    StaticTableFunctionModule::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<StaticTableFunctionModule>::OnSavepoint(
        vtab->state, r);
    return SQLITE_OK;
  }
  static int Release(sqlite3_vtab* t, int r) {
    StaticTableFunctionModule::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<StaticTableFunctionModule>::OnRelease(
        vtab->state, r);
    return SQLITE_OK;
  }
  static int RollbackTo(sqlite3_vtab* t, int r) {
    StaticTableFunctionModule::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<StaticTableFunctionModule>::OnRollbackTo(
        vtab->state, r);
    return SQLITE_OK;
  }

  // This needs to happen at the end as it depends on the functions
  // defined above.
  static constexpr sqlite3_module kModule = CreateModule();
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_STATIC_TABLE_FUNCTION_MODULE_H_
