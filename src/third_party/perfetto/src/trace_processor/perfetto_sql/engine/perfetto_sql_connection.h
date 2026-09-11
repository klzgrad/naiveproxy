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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_CONNECTION_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_CONNECTION_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_database.h"
#include "src/trace_processor/perfetto_sql/engine/runtime_table_function.h"
#include "src/trace_processor/perfetto_sql/engine/static_table_function_module.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/perfetto_sql/parser/perfetto_sql_parser.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_window_function.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_connection.h"
#include "src/trace_processor/util/sql_argument.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor {

// Intermediary class which translates high-level concepts and algorithms used
// in trace processor into lower-level concepts and functions can be understood
// by and executed against SQLite.
class PerfettoSqlConnection {
 public:
  struct ExecutionStats {
    uint32_t column_count = 0;
    uint32_t statement_count = 0;
    uint32_t statement_count_with_output = 0;
  };
  struct ExecutionResult {
    SqliteConnection::PreparedStatement stmt;
    ExecutionStats stats;
  };
  struct StaticTable {
    dataframe::Dataframe* dataframe;
    std::string name;
  };

  // Aggregated registration data passed to |Initialize|. Registration order
  // mirrors field order: static_tables → static_table_functions →
  // sqlite_modules → functions → aggregate_functions → window_functions.
  struct Initializer {
    std::vector<StaticTable> static_tables;
    std::vector<std::unique_ptr<StaticTableFunction>> static_table_functions;
    std::vector<SqliteModuleRegistration> sqlite_modules;
    std::vector<FunctionRegistration> functions;
    std::vector<AggregateFunctionRegistration> aggregate_functions;
    std::vector<WindowFunctionRegistration> window_functions;
  };

  // Creates a fresh |PerfettoSqlDatabase| and returns a connection attached
  // to it. The database lives only as long as some connection has it open.
  static std::unique_ptr<PerfettoSqlConnection> CreateConnectionToNewDatabase(
      StringPool* pool,
      bool enable_extra_checks);

  ~PerfettoSqlConnection();

  PerfettoSqlConnection(const PerfettoSqlConnection&) = delete;
  PerfettoSqlConnection& operator=(const PerfettoSqlConnection&) = delete;

  // Returns a new connection attached to the same |PerfettoSqlDatabase| as
  // this one. The new connection has its own underlying |SqliteConnection|
  // and per-connection state (vtab modules, registered functions etc.); only
  // the database-scoped state (packages, macros, committed vtab state) is
  // shared.
  std::unique_ptr<PerfettoSqlConnection> Fork();

  // Performs per-connection setup: registers static tables, static table
  // functions, virtual table modules and SQL functions in one shot.
  void Initialize(Initializer init);

  // Executes all the statements in |sql| and returns a |ExecutionResult|
  // object. The metadata will reference all the statements executed and the
  // |ScopedStmt| be empty.
  //
  // Returns an error if the execution of any statement failed or if there was
  // no valid SQL to run.
  base::StatusOr<ExecutionStats> Execute(SqlSource sql);

  // Executes a single-statement parameterized SQL, binding |binds| to its
  // |?| placeholders in order. Strings in |binds| must outlive the call.
  // Bypasses the PerfettoSQL frontend: the SQL must be plain SQLite.
  //
  // Intended for hot internal loops where the same INSERT/UPDATE shape is
  // run many times with different values. Each call still prepares and
  // finalizes a fresh sqlite3_stmt (lookaside makes that cheap); wrap the
  // loop in a |Transaction| to avoid per-call commits.
  base::Status Execute(SqlSource sql,
                       std::initializer_list<std::string_view> binds);

  // RAII transaction. Issues BEGIN on construction and COMMIT on
  // destruction. Use to batch a sequence of Execute() calls so SQLite does
  // not implicitly commit after each statement.
  class [[nodiscard]] Transaction {
   public:
    explicit Transaction(PerfettoSqlConnection*);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

   private:
    PerfettoSqlConnection* conn_;
  };

  // Executes all the statements in |sql| fully until the final statement and
  // returns a |ExecutionResult| object containing a |ScopedStmt| for the final
  // statement (which has been stepped once) and metadata about all statements
  // executed.
  //
  // Returns an error if the execution of any statement failed or if there was
  // no valid SQL to run.
  base::StatusOr<ExecutionResult> ExecuteUntilLastStatement(SqlSource sql);

  // Executes only the first statement in |sql| and returns a |ExecutionResult|
  // object containing a |ScopedStmt| for that statement (which has been
  // stepped once). Returns std::nullopt if |sql| contains no statements (i.e.
  // only whitespace/comments).
  //
  // |*end_offset| is set to the byte offset into |sql.sql()| just past the
  // executed statement (or to the end of |sql.sql()| when returning
  // std::nullopt). It is not modified if an error is returned.
  base::StatusOr<std::optional<ExecutionResult>> ExecuteNextStatement(
      SqlSource sql,
      uint32_t* end_offset);

  // Prepares a single SQLite statement in |sql| and returns a
  // |PreparedStatement| object.
  //
  // Returns an error if the preparation of the statement failed or if there was
  // no valid SQL to run.
  base::StatusOr<SqliteConnection::PreparedStatement> PrepareSqliteStatement(
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
    connection_->RegisterVirtualTableModule(name, &Module::kModule, ctx,
                                            nullptr);
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
    connection_->RegisterVirtualTableModule(
        name, &Module::kModule, ctx.release(),
        [](void* ptr) { delete static_cast<typename Module::Context*>(ptr); });
  }

  // Registers a trace processor C++ function to be runnable from SQL.
  //
  // Uses the direct SQLite function interface. This is the preferred method
  // for registering new functions.
  //
  // The format of the function is given by the |sqlite::Function|.
  //
  // |ctx|:           context object for the function; this object *must*
  //                  outlive the function so should likely be either static or
  //                  scoped to the lifetime of TraceProcessor.
  // |deterministic|: whether this function has deterministic output given the
  //                  same set of arguments.
  // Arguments for RegisterFunction with custom function names.
  struct RegisterFunctionArgs {
    RegisterFunctionArgs(const char* _name = nullptr,
                         bool _deterministic = true,
                         std::optional<int> _argc = std::nullopt)
        : name(_name), deterministic(_deterministic), argc(_argc) {}
    const char* name = nullptr;  // If nullptr, uses Function::kName
    bool deterministic = true;
    std::optional<int> argc =
        std::nullopt;  // If nullopt, uses Function::kArgCount
    // True for built-in C++ functions registered at initialization time, where
    // |ctx| is an opaque pointer whose layout is known only to the registrant.
    // False for functions managed by PerfettoSQL (currently CreatedFunction),
    // where |ctx| is a Destructible-derived state object that
    // |RegisterLegacyRuntimeFunction| may recover via a typed downcast. Default
    // true so that new registrants never accidentally advertise type-safety
    // they cannot deliver.
    bool is_intrinsic = true;
  };

  template <typename Function>
  base::Status RegisterFunction(typename Function::UserData* ctx,
                                const RegisterFunctionArgs& args = {});
  template <typename Function>
  base::Status RegisterFunction(
      std::unique_ptr<typename Function::UserData> ctx,
      const RegisterFunctionArgs& args = {});

  // Registers a trace processor C++ aggregate function to be runnable from SQL.
  //
  // Uses the direct SQLite aggregate function interface. This is the preferred
  // method for registering new aggregate functions.
  //
  // The format of the function is given by the |SqliteAggregateFunction|.
  //
  // |ctx|:           context object for the function; this object *must*
  //                  outlive the function so should likely be either static or
  //                  scoped to the lifetime of TraceProcessor.
  // |deterministic|: whether this function has deterministic output given the
  //                  same set of arguments.
  template <typename Function>
  base::Status RegisterAggregateFunction(typename Function::UserData* ctx,
                                         bool deterministic = true);

  // Registers a trace processor C++ window function to be runnable from SQL.
  //
  // Uses the direct SQLite window function interface. This is the preferred
  // method for registering new window functions.
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
  base::Status RegisterWindowFunction(const char* name,
                                      int argc,
                                      typename Function::Context* ctx,
                                      bool deterministic = true);

  // Enables memoization for the given SQL function.
  base::Status EnableSqlFunctionMemoization(const std::string& name);

  SqliteConnection* sqlite_connection() { return connection_.get(); }

  // Test-only accessor for the |PerfettoSqlDatabase| backing this connection.
  PerfettoSqlDatabase* database_for_testing() { return database_.get(); }

  // Makes new SQL package available to include. Fails if any module key in
  // the new package has already been included or poisoned on the underlying
  // database; see |PerfettoSqlDatabase::RegisterPackage|.
  base::Status RegisterPackage(const std::string& name,
                               sql_modules::RegisteredPackage package) {
    return database_->RegisterPackage(name, std::move(package));
  }

  // Removes a SQL package.
  void ErasePackage(const std::string& name) { database_->ErasePackage(name); }

  // Fetches registered SQL package.
  sql_modules::RegisteredPackage* FindPackage(const std::string& name) {
    return database_->FindPackage(name);
  }
  const sql_modules::RegisteredPackage* FindPackage(
      const std::string& name) const {
    return database_->FindPackage(name);
  }

  // Returns (package_name, module_key) pairs for every registered module.
  std::vector<std::pair<std::string, std::string>> GetModules() const {
    return database_->GetModules();
  }

  // Finds a package that owns the given module key (i.e., whose name is a
  // prefix of the key).
  sql_modules::RegisteredPackage* FindPackageForModule(const std::string& key) {
    return database_->FindPackageForModule(key);
  }
  const sql_modules::RegisteredPackage* FindPackageForModule(
      const std::string& key) const {
    return database_->FindPackageForModule(key);
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

    // The missing objects from the above query are functions and macros.
    // Add those in now.
    return query_count + function_count_ + window_function_count_ +
           aggregate_function_count_ + database_->macro_count();
  }

  // Find dataframe registered with this connection with provided name.
  const dataframe::Dataframe* GetDataframeOrNull(const std::string& name) const;

  // Registers a function with the prototype |prototype| which returns a value
  // of |return_type| and is implemented by executing the SQL statement |sql|.
  //
  // LEGACY: This function uses SQL-based function definitions. For new code,
  // prefer RegisterFunction() which uses C++ implementations.
  base::Status RegisterLegacyRuntimeFunction(bool replace,
                                             const FunctionPrototype& prototype,
                                             sql_argument::Type return_type,
                                             SqlSource sql);

 private:
  enum class FrameType { kRoot, kInclude, kWildcard };

  // Result of processing a single frame iteration.
  enum class FrameResult {
    kContinue,      // Frame still has work, continue processing it
    kFrameDone,     // Frame completed, should be popped
    kReturnResult,  // Root frame completed with result
    kNoStatement    // Root frame in stop-after-statement mode found no
                    // statement to execute (whitespace/comments only)
  };

  // Auxiliary state for kInclude / kWildcard frames. Held via unique_ptr so
  // kRoot frames don't pay the construction cost. |include_claim| owns the
  // cross-connection slot for |include_key|: ReleaseSuccess on success,
  // ReleasePoisoned on error so subsequent attempts short-circuit.
  struct ExecutionFrameAux {
    std::string include_key;
    std::optional<SqlSource> traceback_sql;
    PerfettoSqlDatabase::IncludeClaim include_claim;

    // kWildcard: (key, sql) pairs to expand one at a time.
    std::vector<std::pair<std::string, std::string>> wildcard_modules;
    size_t wildcard_index = 0;
    std::optional<SqlSource> wildcard_traceback_sql;
  };

  // Execution state for a single SQL source. The SqlSource lives inside
  // |parser| (via Reset()) rather than on the frame. |parser| is null for
  // kWildcard; |aux| is null for kRoot.
  struct ExecutionFrame {
    FrameType type = FrameType::kRoot;
    std::unique_ptr<PerfettoSqlParser> parser;
    ExecutionStats accumulated_stats;
    std::optional<SqliteConnection::PreparedStatement> current_stmt;
    std::unique_ptr<ExecutionFrameAux> aux;
    // kRoot only: stop after the first statement instead of executing all of
    // them (ExecuteNextStatement).
    bool stop_after_statement = false;
    // True if |current_stmt| is the dummy statement of a transpiled
    // PerfettoSQL statement (INCLUDE, CREATE PERFETTO ...) rather than real
    // SQLite SQL. Such statements have no result set, so the reported column
    // count is forced to zero.
    bool current_stmt_is_dummy = false;
  };

  void RegisterStaticTable(dataframe::Dataframe*, const std::string&);
  void RegisterStaticTableFunction(std::unique_ptr<StaticTableFunction> fn);

  base::Status ExecuteCreateFunction(const PerfettoSqlParser::CreateFunction&);

  base::Status RegisterDelegatingFunction(
      const PerfettoSqlParser::CreateFunction&);

  base::Status RegisterFunctionAndAddToRegistry(
      const char* name,
      int argc,
      SqliteConnection::Fn* func,
      void* ctx,
      SqliteConnection::FnCtxDestructor* ctx_destructor,
      bool deterministic,
      bool is_intrinsic = true);

  // Looks up |ctx| for the function with the given name/argc, or nullptr if no
  // such function is registered with this connection. Membership in
  // |fn_registry_| is also the only safe signal that a SQLite function context
  // belongs to a typed Destructible-derived state (use |IsIntrinsicFunction|
  // to distinguish): SqliteConnection itself does not track this.
  void* GetFunctionContextOrNull(const std::string& name, int argc) const;

  // Returns true if a function with the given name/argc is registered with
  // this connection AND was registered as an intrinsic. Returns false either
  // when the function is not registered or when it was registered as a
  // managed-context PerfettoSQL function (currently only CreatedFunction).
  // Callers MUST consult this before downcasting the result of
  // |GetFunctionContextOrNull| to a typed Destructible-derived object.
  bool IsIntrinsicFunction(const std::string& name, int argc) const;

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
      SqliteConnection::PreparedStatement stmt,
      const std::vector<std::string>& column_names,
      const std::vector<sql_argument::ArgumentDefinition>& effective_schema);

  enum class CreateTableType {
    kCreateTable,
    // For now, bytes columns are not supported in CREATE PERFETTO TABLE,
    // but supported in CREATE PERFETTO VIEW, so we skip them when validating
    // views.
    kValidateOnly
  };

  // Given a package and a key, include the correct file(s) from the package.
  // The key can contain a wildcard to include all files in the module with the
  // matching prefix.
  base::Status IncludePackageImpl(sql_modules::RegisteredPackage&,
                                  const std::string& key,
                                  const PerfettoSqlParser&);

  // Include a given module body. Goes through |TryClaimInclude| on the
  // database; returns OkStatus on already-included, an error on poisoned,
  // or pushes an include frame on the execution stack on a fresh claim.
  base::Status IncludeModuleImpl(const std::string& key,
                                 std::string_view sql,
                                 const PerfettoSqlParser&);

  // Pushes a fresh include frame onto |execution_stack_|. Caller must have
  // already obtained |claim| from the database and verified there's no cycle.
  void PushIncludeFrame(const std::string& key,
                        std::string_view sql,
                        SqlSource traceback_sql,
                        PerfettoSqlDatabase::IncludeClaim claim);

  // Returns true iff |key| is the |include_key| of an active |kInclude|
  // frame on this connection's execution stack — i.e. a re-entry of |key|
  // would form an include cycle.
  bool IsKeyOnIncludeStack(const std::string& key) const;

  // Shared implementation of ExecuteUntilLastStatement (|end_offset| ==
  // nullptr) and ExecuteNextStatement (|end_offset| != nullptr, which stops
  // after the first statement). Unwinds the execution stack on the error
  // path; separated from ExecuteStatementsImpl to handle re-entrant Execute()
  // calls from statement handlers.
  base::StatusOr<std::optional<ExecutionResult>> ExecuteStatements(
      SqlSource,
      uint32_t* end_offset);

  // Implementation of ExecuteStatements.
  base::StatusOr<std::optional<ExecutionResult>> ExecuteStatementsImpl(
      SqlSource,
      uint32_t* end_offset);

  // Processes a single iteration of the frame at the given index.
  // May push new frames onto the stack (for includes/wildcards).
  base::StatusOr<FrameResult> ProcessFrame(size_t frame_idx);

  // Cold-path helper for non-SqliteSql parser statements (CREATE PERFETTO
  // ..., DROP PERFETTO INDEX, INCLUDE PERFETTO MODULE). Runs the side
  // effects and returns a dummy SqlSource that preserves the traceback.
  base::StatusOr<SqlSource> ResolveExtensionStatement(size_t frame_idx);

  // Returns a parser ready for use: |cached_parser_| if available (and
  // Reset()ed by the caller), otherwise a freshly-allocated one. The cache
  // is replenished only by Execute()'s top-level frame.
  std::unique_ptr<PerfettoSqlParser> AcquireParser();

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

  PerfettoSqlConnection(std::shared_ptr<PerfettoSqlDatabase> database,
                        bool enable_extra_checks);

  std::shared_ptr<PerfettoSqlDatabase> database_;
  StringPool* pool_ = nullptr;

  // If true, this connection will perform additional consistency checks when
  // e.g. creating tables and views.
  const bool enable_extra_checks_;

  // Execution stack for iterative (non-recursive) processing of SQL sources.
  // When an INCLUDE statement is encountered, the included module's SQL is
  // pushed onto this stack and executed before continuing with the current
  // SQL. Inline storage for one frame avoids a heap alloc on Execute().
  base::SmallVector<ExecutionFrame, 1> execution_stack_;

  uint64_t function_count_ = 0;
  uint64_t aggregate_function_count_ = 0;
  uint64_t window_function_count_ = 0;

  // Contains the pointers for all registered virtual table modules where the
  // context class of the module inherits from ModuleStateManagerBase.
  std::vector<sqlite::ModuleStateManagerBase*> virtual_module_state_managers_;

  RuntimeTableFunctionModule::Context* runtime_table_fn_context_ = nullptr;
  StaticTableFunctionModule::Context* static_table_fn_context_ = nullptr;
  DataframeModule::Context* dataframe_context_ = nullptr;

  // Registry of intrinsic functions that can be aliased.
  // Maps lowercased name -> (function_ptr, argc, ctx, deterministic). Keys are
  // lowercased to match SQLite's case-insensitive function namespace.
  struct IntrinsicFunctionInfo {
    SqliteConnection::Fn* func;
    int argc;
    void* ctx;
    bool deterministic;
  };
  base::FlatHashMap<std::string, IntrinsicFunctionInfo>
      intrinsic_function_registry_;

  // Tracks every scalar function registered with |connection_| via
  // |RegisterFunctionAndAddToRegistry|, keyed by (name, argc). Used for two
  // things:
  //
  // 1) Discrimination: |is_intrinsic| tells us whether |ctx| is opaque (raw
  //    C++ intrinsic, e.g. import's PerfettoSqlConnection*) or a typed
  //    Destructible-derived state (CreatedFunction). Looking at SQLite alone
  //    can't tell the difference, which is what historically allowed
  //    CREATE OR REPLACE PERFETTO FUNCTION import(...) to type-confuse the
  //    intrinsic's context with a CreatedFunction::State.
  //
  // 2) Teardown: scalar function contexts can hold prepared statements
  //    (CreatedFunction::State::stmts_); those must be finalized before the
  //    underlying sqlite3* is closed. The destructor walks this map and
  //    explicitly unregisters every entry, which triggers SQLite to invoke
  //    each entry's |FnCtxDestructor| in turn.
  struct FunctionEntry {
    void* ctx;
    bool is_intrinsic;
  };
  base::FlatHashMap<std::pair<std::string, int>,
                    FunctionEntry,
                    base::MurmurHash<std::pair<std::string, int>>>
      fn_registry_;

  std::unique_ptr<SqliteConnection> connection_;

  // Reused across Execute() calls via Reset() to avoid the syntaqlite
  // create/destroy round-trip. Re-entrant Execute() and include frames
  // allocate fresh parsers.
  std::unique_ptr<PerfettoSqlParser> cached_parser_;
};

// The rest of this file is just implementation details which we need
// in the header file because it is templated code. We separate it out
// like this to keep the API people actually care about easy to read.

template <typename Function>
base::Status PerfettoSqlConnection::RegisterFunction(
    typename Function::UserData* ctx,
    const RegisterFunctionArgs& args) {
  function_count_++;
  const char* name = args.name ? args.name : Function::kName;
  int argc = args.argc.has_value() ? args.argc.value() : Function::kArgCount;
  return RegisterFunctionAndAddToRegistry(name, argc, Function::Step, ctx,
                                          nullptr, args.deterministic,
                                          args.is_intrinsic);
}

template <typename Function>
base::Status PerfettoSqlConnection::RegisterFunction(
    std::unique_ptr<typename Function::UserData> ctx,
    const RegisterFunctionArgs& args) {
  function_count_++;
  const char* name = args.name ? args.name : Function::kName;
  int argc = args.argc.has_value() ? args.argc.value() : Function::kArgCount;
  return RegisterFunctionAndAddToRegistry(
      name, argc, Function::Step, ctx.release(),
      [](void* ptr) {
        std::unique_ptr<typename Function::UserData>(
            static_cast<typename Function::UserData*>(ptr));
      },
      args.deterministic, args.is_intrinsic);
}

template <typename Function>
base::Status PerfettoSqlConnection::RegisterAggregateFunction(
    typename Function::UserData* ctx,
    bool deterministic) {
  aggregate_function_count_++;
  return connection_->RegisterAggregateFunction(
      Function::kName, Function::kArgCount, Function::Step, Function::Final,
      ctx, nullptr, deterministic);
}

template <typename Function>
base::Status PerfettoSqlConnection::RegisterWindowFunction(
    const char* name,
    int argc,
    typename Function::Context* ctx,
    bool deterministic) {
  window_function_count_++;
  return connection_->RegisterWindowFunction(
      name, argc, Function::Step, Function::Inverse, Function::Value,
      Function::Final, ctx, nullptr, deterministic);
}

// Builds a scalar function registration entry with a non-owning context.
template <typename F>
FunctionRegistration MakeFunctionRegistration(
    typename F::UserData* ctx,
    PerfettoSqlConnection::RegisterFunctionArgs args = {}) {
  FunctionRegistration r;
  r.name = args.name ? args.name : F::kName;
  r.argc = args.argc.has_value() ? *args.argc : F::kArgCount;
  r.step = F::Step;
  r.ctx = ctx;
  r.deterministic = args.deterministic;
  return r;
}

// Builds a scalar function registration entry with an owning context.
template <typename F>
FunctionRegistration MakeFunctionRegistration(
    std::unique_ptr<typename F::UserData> ctx,
    PerfettoSqlConnection::RegisterFunctionArgs args = {}) {
  FunctionRegistration r;
  r.name = args.name ? args.name : F::kName;
  r.argc = args.argc.has_value() ? *args.argc : F::kArgCount;
  r.step = F::Step;
  r.ctx = ctx.release();
  r.ctx_destructor = [](void* p) {
    delete static_cast<typename F::UserData*>(p);
  };
  r.deterministic = args.deterministic;
  return r;
}

// Builds an aggregate function registration entry.
template <typename F>
AggregateFunctionRegistration MakeAggregateRegistration(
    typename F::UserData* ctx,
    bool deterministic = true) {
  AggregateFunctionRegistration r;
  r.name = F::kName;
  r.argc = F::kArgCount;
  r.step = F::Step;
  r.final_fn = F::Final;
  r.ctx = ctx;
  r.deterministic = deterministic;
  return r;
}

// Builds a window function registration entry.
template <typename F>
WindowFunctionRegistration MakeWindowRegistration(const char* name,
                                                  int argc,
                                                  typename F::Context* ctx,
                                                  bool deterministic = true) {
  WindowFunctionRegistration r;
  r.name = name;
  r.argc = argc;
  r.step = F::Step;
  r.inverse = F::Inverse;
  r.value = F::Value;
  r.final_fn = F::Final;
  r.ctx = ctx;
  r.deterministic = deterministic;
  return r;
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_PERFETTO_SQL_CONNECTION_H_
