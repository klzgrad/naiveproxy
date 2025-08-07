/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_DB_SQLITE_TABLE_H_
#define SRC_TRACE_PROCESSOR_SQLITE_DB_SQLITE_TABLE_H_

#include <sqlite3.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/runtime_table.h"
#include "src/trace_processor/db/table.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/module_state_manager.h"

namespace perfetto::trace_processor {

enum class TableComputation : uint8_t {
  // Table is statically defined.
  kStatic,

  // Table is defined in runtime.
  kRuntime
};

// Implements the SQLite table interface for db tables.
struct DbSqliteModule : public sqlite::Module<DbSqliteModule> {
  struct State {
    State(Table*, Table::Schema);
    explicit State(std::unique_ptr<RuntimeTable>);

    TableComputation computation;
    Table::Schema schema;
    int argument_count = 0;

    // Only valid when computation == TableComputation::kStatic.
    Table* static_table = nullptr;

    // Only valid when computation == TableComputation::kRuntime.
    std::unique_ptr<RuntimeTable> runtime_table;

   private:
    State(TableComputation, Table::Schema);
  };
  struct Context : sqlite::ModuleStateManager<DbSqliteModule> {
    std::unique_ptr<State> temporary_create_state;
  };
  struct Vtab : public sqlite::Module<DbSqliteModule>::Vtab {
    sqlite::ModuleStateManager<DbSqliteModule>::PerVtabState* state;
    int best_index_num = 0;
    std::string table_name;
  };
  struct Cursor : public sqlite::Module<DbSqliteModule>::Cursor {
    enum class Mode : uint8_t {
      kSingleRow,
      kTable,
    };

    const Table* upstream_table = nullptr;

    // Only valid for Mode::kSingleRow.
    std::optional<uint32_t> single_row;

    // Only valid for Mode::kTable.
    std::optional<Table::Iterator> iterator;

    bool eof = true;

    Mode mode = Mode::kSingleRow;

    int last_idx_num = -1;

    Query query;
  };
  struct QueryCost {
    double cost;
    uint32_t rows;
  };

  static constexpr bool kSupportsWrites = false;
  static constexpr bool kDoesOverloadFunctions = false;

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
    DbSqliteModule::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<DbSqliteModule>::OnSavepoint(vtab->state, r);
    return SQLITE_OK;
  }
  static int Release(sqlite3_vtab* t, int r) {
    DbSqliteModule::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<DbSqliteModule>::OnRelease(vtab->state, r);
    return SQLITE_OK;
  }
  static int RollbackTo(sqlite3_vtab* t, int r) {
    DbSqliteModule::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<DbSqliteModule>::OnRollbackTo(vtab->state, r);
    return SQLITE_OK;
  }

  // static for testing.
  static QueryCost EstimateCost(const Table::Schema&,
                                uint32_t row_count,
                                sqlite3_index_info* info,
                                const std::vector<int>&,
                                const std::vector<int>&);

  // This needs to happen at the end as it depends on the functions
  // defined above.
  static constexpr sqlite3_module kModule = CreateModule();
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SQLITE_DB_SQLITE_TABLE_H_
