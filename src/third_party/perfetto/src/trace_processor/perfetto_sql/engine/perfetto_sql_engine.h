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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_ENGINE_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_ENGINE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_shared_storage.h"
#include "src/trace_processor/perfetto_sql/engine/runtime_table_function.h"
#include "src/trace_processor/perfetto_sql/engine/static_table_function_module.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sql_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/perfetto_sql/parser/perfetto_sql_parser.h"
#include "src/trace_processor/perfetto_sql/preprocessor/perfetto_sql_preprocessor.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_window_function.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_engine.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/util/sql_argument.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor {

// Intermediary class which translates high-level concepts and algorithms used
// in trace processor into lower-level concepts and functions can be understood
// by and executed against SQLite.
class PerfettoSqlEngine {
 public:
  struct ExecutionStats {
    uint32_t column_count = 0;
    uint32_t statement_count = 0;
    uint32_t statement_count_with_output = 0;
  };
  struct ExecutionResult {
    SqliteEngine::PreparedStatement stmt;
    ExecutionStats stats;
  };
  struct UnfinalizedStaticTable {
    dataframe::Dataframe* dataframe;
    std::string name;
  };
  struct FinalizedStaticTable {
    DataframeSharedStorage::DataframeHandle handle;
    std::string name;
  };
  PerfettoSqlEngine(StringPool* pool,
                    DataframeSharedStorage* storage,
                    bool enable_extra_checks);

  // Initializes the static tables and functions in the engine.
  base::Status InitializeStaticTablesAndFunctions(
      const std::vector<UnfinalizedStaticTable>& unfinalized_tables,
      std::vector<FinalizedStaticTable> finalized_tables,
      std::vector<std::unique_ptr<StaticTableFunction>> functions);

  // Finalizes all the static tables owned by this engine and makes them
  // sharable in the `DataframeSharedStorage` passed in the constructor.
  void FinalizeAndShareAllStaticTables();

  // Executes all the statements in |sql| and returns a |ExecutionResult|
  // object. The metadata will reference all the statements executed and the
  // |ScopedStmt| be empty.
  //
  // Returns an error if the execution of any statement failed or if there was
  // no valid SQL to run.
  base::StatusOr<ExecutionStats> Execute(SqlSource sql);

  // Executes all the statements in |sql| fully until the final statement and
  // returns a |ExecutionResult| object containing a |ScopedStmt| for the final
  // statement (which has been stepped once) and metadata about all statements
  // executed.
  //
  // Returns an error if the execution of any statement failed or if there was
  // no valid SQL to run.
  base::StatusOr<ExecutionResult> ExecuteUntilLastStatement(SqlSource sql);

  // Prepares a single SQLite statement in |sql| and returns a
  // |PreparedStatement| object.
  //
  // Returns an error if the preparation of the statement failed or if there was
  // no valid SQL to run.
  base::StatusOr<SqliteEngine::PreparedStatement> PrepareSqliteStatement(
      SqlSource sql);

  // Registers a virtual table module with the given name.
  //
  // |name|: name of the module in SQL.
  // |ctx|:  context object for the module. This object *must* outlive the
  //         module so should likely be either static or scoped to the lifetime
  //         of TraceProcessor.
  template <typename Module>
  void RegisterVirtualTableModule(const char* name,
                                  typename Module::Context* ctx) {
    static_assert(std::is_base_of_v<sqlite::Module<Module>, Module>,
                  "Must subclass sqlite::Module");
    // If the context class of the module inherits from
    // ModuleStateManagerBase, we need to add it to the list of virtual module
    // state managers so it receives the OnCommit/OnRollback callbacks.
    if constexpr (std::is_base_of_v<sqlite::ModuleStateManagerBase,
                                    typename Module::Context>) {
      virtual_module_state_managers_.push_back(ctx);
    }
    engine_->RegisterVirtualTableModule(name, &Module::kModule, ctx, nullptr);
  }

  // Registers a virtual table module with the given name.
  //
  // |name|: name of the module in SQL.
  // |ctx|:  context object for the module. The lifetime of the context object
  //         is managed by SQLite.
  template <typename Module>
  void RegisterVirtualTableModule(
      const char* name,
      std::unique_ptr<typename Module::Context> ctx) {
    static_assert(std::is_base_of_v<sqlite::Module<Module>, Module>,
                  "Must subclass sqlite::Module");
    // If the context class of the module inherits from
    // ModuleStateManagerBase, we need to add it to the list of virtual module
    // state managers so it receives the OnCommit/OnRollback callbacks.
    if constexpr (std::is_base_of_v<sqlite::ModuleStateManagerBase,
                                    typename Module::Context>) {
      virtual_module_state_managers_.push_back(ctx.get());
    }
    engine_->RegisterVirtualTableModule(
        name, &Module::kModule, ctx.release(),
        [](void* ptr) { delete static_cast<typename Module::Context*>(ptr); });
  }

  // Registers a trace processor C++ function to be runnable from SQL.
  //
  // The format of the function is given by the |SqlFunction|.
  //
  // |name|:          name of the function in SQL.
  // |argc|:          number of arguments for this function. This can be -1 if
  //                  the number of arguments is variable.
  // |ctx|:           context object for the function (see
  //                  LegacySqlFunction::Run);
  //                  this object *must* outlive the function so should likely
  //                  be either static or scoped to the lifetime of
  //                  TraceProcessor.
  // |deterministic|: whether this function has deterministic output given the
  //                  same set of arguments.
  template <typename Function = LegacySqlFunction>
  base::Status RegisterStaticFunction(const char* name,
                                      int argc,
                                      typename Function::Context* ctx,
                                      bool deterministic = true);

  // Registers a trace processor C++ function to be runnable from SQL.
  //
  // This function is the same as the above except allows a unique_ptr to be
  // passed for the context; this allows for SQLite to manage the lifetime of
  // this pointer instead of the essentially static requirement of the context
  // pointer above.
  template <typename Function>
  base::Status RegisterStaticFunction(
      const char* name,
      int argc,
      std::unique_ptr<typename Function::Context> ctx,
      bool deterministic = true);

  // Registers a trace processor C++ function to be runnable from SQL.
  //
  // The format of the function is given by the |sqlite::Function|.
  //
  // |ctx|:           context object for the function; this object *must*
  //                  outlive the function so should likely be either static or
  //                  scoped to the lifetime of TraceProcessor.
  // |deterministic|: whether this function has deterministic output given the
  //                  same set of arguments.
  template <typename Function>
  base::Status RegisterSqliteFunction(typename Function::UserData* ctx,
                                      bool deterministic = true);
  template <typename Function>
  base::Status RegisterSqliteFunction(
      std::unique_ptr<typename Function::UserData> ctx,
      bool deterministic = true);

  // Registers a trace processor C++ aggregate function to be runnable from SQL.
  //
  // The format of the function is given by the |SqliteAggregateFunction|.
  //
  // |ctx|:           context object for the function; this object *must*
  //                  outlive the function so should likely be either static or
  //                  scoped to the lifetime of TraceProcessor.
  // |deterministic|: whether this function has deterministic output given the
  //                  same set of arguments.
  template <typename Function>
  base::Status RegisterSqliteAggregateFunction(typename Function::UserData* ctx,
                                               bool deterministic = true);

  // Registers a trace processor C++ window function to be runnable from SQL.
  //
  // The format of the function is given by the |SqliteWindowFunction|.
  //
  // |name|:          name of the function in SQL.
  // |argc|:          number of arguments for this function. This can be -1 if
  //                  the number of arguments is variable.
  // |ctx|:           context object for the function; this object *must*
  //                  outlive the function so should likely be either static or
  //                  scoped to the lifetime of TraceProcessor.
  // |deterministic|: whether this function has deterministic output given the
  //                  same set of arguments.
  template <typename Function = sqlite::WindowFunction>
  base::Status RegisterSqliteWindowFunction(const char* name,
                                            int argc,
                                            typename Function::Context* ctx,
                                            bool deterministic = true);

  // Registers a function with the prototype |prototype| which returns a value
  // of |return_type| and is implemented by executing the SQL statement |sql|.
  base::Status RegisterRuntimeFunction(bool replace,
                                       const FunctionPrototype& prototype,
                                       sql_argument::Type return_type,
                                       SqlSource sql);

  // Enables memoization for the given SQL function.
  base::Status EnableSqlFunctionMemoization(const std::string& name);

  SqliteEngine* sqlite_engine() { return engine_.get(); }

  // Makes new SQL package available to include.
  void RegisterPackage(const std::string& name,
                       sql_modules::RegisteredPackage package) {
    packages_.Erase(name);
    packages_.Insert(name, std::move(package));
  }

  // Fetches registered SQL package.
  sql_modules::RegisteredPackage* FindPackage(const std::string& name) {
    return packages_.Find(name);
  }

  // Returns the number of objects (tables, views, functions etc) registered
  // with SQLite.
  uint64_t SqliteRegisteredObjectCount() {
    // This query will return all the tables, views, indexes and table functions
    // SQLite knows about.
    constexpr char kAllTablesQuery[] =
        "SELECT COUNT() FROM (SELECT * FROM sqlite_master "
        "UNION ALL SELECT * FROM sqlite_temp_master)";
    auto stmt = ExecuteUntilLastStatement(
        SqlSource::FromTraceProcessorImplementation(kAllTablesQuery));
    if (!stmt.ok()) {
      PERFETTO_FATAL("%s", stmt.status().c_message());
    }
    uint32_t query_count =
        static_cast<uint32_t>(sqlite3_column_int(stmt->stmt.sqlite_stmt(), 0));
    PERFETTO_CHECK(!stmt->stmt.Step());
    PERFETTO_CHECK(stmt->stmt.status().ok());

    // The missing objects from the above query are static functions, runtime
    // functions and macros. Add those in now.
    return query_count + static_function_count_ +
           static_window_function_count_ + static_aggregate_function_count_ +
           runtime_function_count_ + macros_.size();
  }

  // Find dataframe registered with engine with provided name.
  const dataframe::Dataframe* GetDataframeOrNull(const std::string& name) const;

 private:
  using UnfinalizedOrFinalizedStaticTable =
      std::variant<DataframeSharedStorage::DataframeHandle,
                   dataframe::Dataframe*>;
  void RegisterStaticTable(UnfinalizedOrFinalizedStaticTable,
                           const std::string&);
  void RegisterStaticTableFunction(std::unique_ptr<StaticTableFunction> fn);

  base::Status ExecuteCreateFunction(const PerfettoSqlParser::CreateFunction&);

  base::Status ExecuteInclude(const PerfettoSqlParser::Include&,
                              const PerfettoSqlParser& parser);

  // Creates a runtime table and registers it with SQLite.
  base::Status ExecuteCreateTable(
      const PerfettoSqlParser::CreateTable& create_table);

  base::Status ExecuteCreateView(const PerfettoSqlParser::CreateView&);

  base::Status ExecuteCreateMacro(const PerfettoSqlParser::CreateMacro&);

  base::Status ExecuteCreateIndex(const PerfettoSqlParser::CreateIndex&);

  base::Status DropIndexBeforeCreate(const PerfettoSqlParser::CreateIndex&);

  base::Status ExecuteDropIndex(const PerfettoSqlParser::DropIndex&);

  base::Status ExecuteCreateTableUsingRuntimeTable(
      const PerfettoSqlParser::CreateTable& create_table,
      SqliteEngine::PreparedStatement stmt,
      const std::vector<std::string>& column_names,
      const std::vector<sql_argument::ArgumentDefinition>& effective_schema);

  enum class CreateTableType {
    kCreateTable,
    // For now, bytes columns are not supported in CREATE PERFETTO TABLE,
    // but supported in CREATE PERFETTO VIEW, so we skip them when validating
    // views.
    kValidateOnly
  };
  template <typename Function>
  base::Status RegisterFunctionWithSqlite(
      const char* name,
      int argc,
      std::unique_ptr<typename Function::Context> ctx,
      bool deterministic = true);

  // Given a package and a key, include the correct file(s) from the package.
  // The key can contain a wildcard to include all files in the module with the
  // matching prefix.
  base::Status IncludePackageImpl(sql_modules::RegisteredPackage&,
                                  const std::string& key,
                                  const PerfettoSqlParser&);

  // Include a given module.
  base::Status IncludeModuleImpl(sql_modules::RegisteredPackage::ModuleFile&,
                                 const std::string& key,
                                 const PerfettoSqlParser&);

  // Called when a transaction is committed by SQLite; that is, the result of
  // running some SQL is considered "perm".
  //
  // See https://www.sqlite.org/lang_transaction.html for an explanation of
  // transactions in SQLite.
  int OnCommit();

  // Called when a transaction is rolled back by SQLite; that is, the result of
  // of running some SQL should be discarded and the state of the database
  // should be restored to the state it was in before the transaction was
  // started.
  //
  // See https://www.sqlite.org/lang_transaction.html for an explanation of
  // transactions in SQLite.
  void OnRollback();

  StringPool* pool_ = nullptr;

  // Storage for shared Dataframe objects.
  //
  // Note that this class can be shared between multiple PerfettoSqlEngine
  // instances which are operating on different threads.
  DataframeSharedStorage* dataframe_shared_storage_;

  // If true, engine will perform additional consistency checks when e.g.
  // creating tables and views.
  const bool enable_extra_checks_;

  // A stack which keeps track of the modules which are being included. Used to
  // know when dataframes should be shared.
  std::vector<std::string> module_include_stack_;

  uint64_t static_function_count_ = 0;
  uint64_t static_aggregate_function_count_ = 0;
  uint64_t static_window_function_count_ = 0;
  uint64_t runtime_function_count_ = 0;

  // Contains the pointers for all registered virtual table modules where the
  // context class of the module inherits from ModuleStateManagerBase.
  std::vector<sqlite::ModuleStateManagerBase*> virtual_module_state_managers_;

  RuntimeTableFunctionModule::Context* runtime_table_fn_context_ = nullptr;
  StaticTableFunctionModule::Context* static_table_fn_context_ = nullptr;
  DataframeModule::Context* dataframe_context_ = nullptr;
  base::FlatHashMap<std::string, sql_modules::RegisteredPackage> packages_;
  base::FlatHashMap<std::string, PerfettoSqlPreprocessor::Macro> macros_;
  std::unique_ptr<SqliteEngine> engine_;
};

// The rest of this file is just implementation details which we need
// in the header file because it is templated code. We separate it out
// like this to keep the API people actually care about easy to read.

namespace perfetto_sql_internal {

// RAII type to call Function::Cleanup when destroyed.
template <typename Function>
struct ScopedCleanup {
  typename Function::Context* ctx;
  ~ScopedCleanup() { Function::Cleanup(ctx); }
};

template <typename Function>
void WrapSqlFunction(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  using Context = typename Function::Context;
  auto* ud = static_cast<Context*>(sqlite3_user_data(ctx));

  ScopedCleanup<Function> scoped_cleanup{ud};
  SqlValue value{};
  LegacySqlFunction::Destructors destructors{};
  base::Status status =
      Function::Run(ud, static_cast<size_t>(argc), argv, value, destructors);
  if (!status.ok()) {
    sqlite::result::Error(ctx, status.c_message());
    return;
  }

  if (Function::kVoidReturn) {
    if (!value.is_null()) {
      sqlite::result::Error(ctx, "void SQL function returned value");
      return;
    }

    // If the function doesn't want to return anything, set the "VOID"
    // pointer type to a non-null value. Note that because of the weird
    // way |sqlite3_value_pointer| works, we need to set some value even
    // if we don't actually read it - just set it to a pointer to an empty
    // string for this reason.
    static char kVoidValue[] = "";
    sqlite::result::StaticPointer(ctx, kVoidValue, "VOID");
  } else {
    switch (value.type) {
      case SqlValue::Type::kLong:
        sqlite::result::Long(ctx, value.long_value);
        break;
      case SqlValue::Type::kDouble:
        sqlite::result::Double(ctx, value.double_value);
        break;
      case SqlValue::Type::kString: {
        sqlite::result::RawString(ctx, value.string_value, -1,
                                  destructors.string_destructor);
        break;
      }
      case SqlValue::Type::kBytes:
        sqlite::result::RawBytes(ctx, value.bytes_value,
                                 static_cast<int>(value.bytes_count),
                                 destructors.bytes_destructor);
        break;
      case SqlValue::Type::kNull:
        sqlite::result::Null(ctx);
        break;
    }
  }

  status = Function::VerifyPostConditions(ud);
  if (!status.ok()) {
    sqlite::result::Error(ctx, status.c_message());
    return;
  }
}

}  // namespace perfetto_sql_internal

template <typename Function>
base::Status PerfettoSqlEngine::RegisterStaticFunction(
    const char* name,
    int argc,
    typename Function::Context* ctx,
    bool deterministic) {
  // Metric proto builder functions can be reregistered: don't double count when
  // this happens.
  if (!engine_->GetFunctionContext(name, argc)) {
    static_function_count_++;
  }
  return engine_->RegisterFunction(
      name, argc, perfetto_sql_internal::WrapSqlFunction<Function>, ctx,
      nullptr, deterministic);
}

template <typename Function>
base::Status PerfettoSqlEngine::RegisterSqliteFunction(
    typename Function::UserData* ctx,
    bool deterministic) {
  static_function_count_++;
  return engine_->RegisterFunction(Function::kName, Function::kArgCount,
                                   Function::Step, ctx, nullptr, deterministic);
}

template <typename Function>
base::Status PerfettoSqlEngine::RegisterSqliteFunction(
    std::unique_ptr<typename Function::UserData> ctx,
    bool deterministic) {
  static_function_count_++;
  return engine_->RegisterFunction(
      Function::kName, Function::kArgCount, Function::Step, ctx.release(),
      [](void* ptr) {
        std::unique_ptr<typename Function::UserData>(
            static_cast<typename Function::UserData*>(ptr));
      },
      deterministic);
}

template <typename Function>
base::Status PerfettoSqlEngine::RegisterSqliteAggregateFunction(
    typename Function::UserData* ctx,
    bool deterministic) {
  static_aggregate_function_count_++;
  return engine_->RegisterAggregateFunction(
      Function::kName, Function::kArgCount, Function::Step, Function::Final,
      ctx, nullptr, deterministic);
}

template <typename Function>
base::Status PerfettoSqlEngine::RegisterSqliteWindowFunction(
    const char* name,
    int argc,
    typename Function::Context* ctx,
    bool deterministic) {
  static_window_function_count_++;
  return engine_->RegisterWindowFunction(
      name, argc, Function::Step, Function::Inverse, Function::Value,
      Function::Final, ctx, nullptr, deterministic);
}

template <typename Function>
base::Status PerfettoSqlEngine::RegisterStaticFunction(
    const char* name,
    int argc,
    std::unique_ptr<typename Function::Context> ctx,
    bool deterministic) {
  // Metric proto builder functions can be reregistered: don't double count when
  // this happens.
  if (!engine_->GetFunctionContext(name, argc)) {
    static_function_count_++;
  }
  return RegisterFunctionWithSqlite<Function>(name, argc, std::move(ctx),
                                              deterministic);
}

template <typename Function>
base::Status PerfettoSqlEngine::RegisterFunctionWithSqlite(
    const char* name,
    int argc,
    std::unique_ptr<typename Function::Context> ctx,
    bool deterministic) {
  auto ctx_destructor = [](void* ptr) {
    delete static_cast<typename Function::Context*>(ptr);
  };
  return engine_->RegisterFunction(
      name, argc, perfetto_sql_internal::WrapSqlFunction<Function>,
      ctx.release(), ctx_destructor, deterministic);
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_ENGINE_H_
