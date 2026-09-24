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

#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"

#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/runtime_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/perfetto_sql/engine/created_function.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"
#include "src/trace_processor/perfetto_sql/engine/runtime_table_function.h"
#include "src/trace_processor/perfetto_sql/engine/sqlite_dataframe_builder.h"
#include "src/trace_processor/perfetto_sql/engine/static_table_function_module.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/perfetto_sql/parser/perfetto_sql_parser.h"
#include "src/trace_processor/sqlite/bindings/sqlite_column.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/scoped_db.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_connection.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/util/sql_argument.h"
#include "src/trace_processor/util/sql_modules.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"  // IWYU pragma: keep

// Implementation details
// ----------------------
//
// The execution of PerfettoSQL statements is the joint responsibility of
// the PerfettoSqlConnection and PerfettoSqlParser classes:
//
// * PerfettoSqlConnection: this class is responsible for the end-to-end
// processing
//   of statements. It calls into PerfettoSqlParser to incrementally receive
//   parsed SQL statements and then executes them. If the statement is a
//   PerfettoSQL-only statement, the execution happens entirely in this class.
//   Otherwise, if the statement is a valid SQLite statement, SQLite is called
//   into to perform the execution.
// * PerfettoSqlParser: this class is responsible for taking a chunk of SQL and
//   incrementally converting them into parsed SQL statements. The underlying
//   tokenization, statement splitting and macro expansion are performed by
//   the vendored syntaqlite parser.
namespace perfetto::trace_processor {
namespace {

void IncrementCountForStmt(const SqliteConnection::PreparedStatement& p_stmt,
                           PerfettoSqlConnection::ExecutionStats* res) {
  res->statement_count++;

  // If the stmt is already done, it clearly didn't have any output.
  if (p_stmt.IsDone())
    return;

  sqlite3_stmt* stmt = p_stmt.sqlite_stmt();
  if (sqlite3_column_count(stmt) == 1) {
    sqlite3_value* value = sqlite3_column_value(stmt, 0);

    // If the "VOID" pointer associated to the return value is not null,
    // that means this is a function which is forced to return a value
    // (because all functions in SQLite have to) but doesn't actually
    // wait to (i.e. it wants to be treated like CREATE TABLE or similar).
    // Because of this, ignore the return value of this function.
    // See |WrapSqlFunction| for where this is set.
    if (sqlite3_value_pointer(value, "VOID") != nullptr) {
      return;
    }

    // If the statement only has a single column and that column is named
    // "suppress_query_output", treat it as a statement without output for
    // accounting purposes. This allows an escape hatch for cases where the
    // user explicitly wants to ignore functions as having output.
    if (strcmp(sqlite3_column_name(stmt, 0), "suppress_query_output") == 0) {
      return;
    }
  }

  // Otherwise, the statement has output and so increment the count.
  res->statement_count_with_output++;
}

base::Status AddTracebackIfNeeded(base::Status status,
                                  const SqlSource& source) {
  if (status.ok()) {
    return status;
  }
  if (status.GetPayload("perfetto.dev/has_traceback") == "true") {
    return status;
  }
  // Since the error is with the statement as a whole, just pass zero so the
  // traceback points to the start of the statement.
  std::string traceback = source.AsTraceback(0);
  status = base::ErrStatus("%s%s", traceback.c_str(), status.c_message());
  status.SetPayload("perfetto.dev/has_traceback", "true");
  return status;
}

// This function is used when the PerfettoSQL has been fully executed by the
// PerfettoSqlConnection and a SqlSoruce is needed for SQLite to execute.
SqlSource RewriteToDummySql(const SqlSource& source) {
  return source.RewriteAllIgnoreExisting(
      SqlSource::FromTraceProcessorImplementation("SELECT 0 WHERE 0"));
}

base::StatusOr<std::vector<sql_argument::ArgumentDefinition>>
ValidateAndGetEffectiveSchema(
    const std::vector<std::string>& column_names,
    const std::vector<sql_argument::ArgumentDefinition>& schema,
    const char* tag) {
  std::vector<std::string> duplicate_columns;
  for (auto it = column_names.begin(); it != column_names.end(); ++it) {
    if (std::count(it + 1, column_names.end(), *it) > 0) {
      duplicate_columns.push_back(*it);
    }
  }
  if (!duplicate_columns.empty()) {
    return base::ErrStatus("%s: multiple columns are named: %s", tag,
                           base::Join(duplicate_columns, ", ").c_str());
  }

  // If the user has not provided a schema, we have nothing further to validate.
  if (schema.empty()) {
    return schema;
  }

  std::vector<std::string> columns_missing_from_query;
  std::vector<std::string> columns_missing_from_schema;

  std::vector<sql_argument::ArgumentDefinition> effective_schema;

  for (const std::string& name : column_names) {
    auto it =
        std::find_if(schema.begin(), schema.end(), [&name](const auto& arg) {
          return arg.name() == base::StringView(name);
        });
    bool present = it != schema.end();
    if (present) {
      effective_schema.push_back(*it);
    } else {
      columns_missing_from_schema.push_back(name);
    }
  }

  for (const auto& arg : schema) {
    bool present = std::find_if(column_names.begin(), column_names.end(),
                                [&arg](const std::string& name) {
                                  return arg.name() == base::StringView(name);
                                }) != column_names.end();
    if (!present) {
      columns_missing_from_query.push_back(arg.name().ToStdString());
    }
  }

  if (!columns_missing_from_query.empty() &&
      !columns_missing_from_schema.empty()) {
    return base::ErrStatus(
        "%s: the following columns are declared in the schema, but do not "
        "exist: "
        "%s; and the following columns exist, but are not declared: %s",
        tag, base::Join(columns_missing_from_query, ", ").c_str(),
        base::Join(columns_missing_from_schema, ", ").c_str());
  }

  if (!columns_missing_from_schema.empty()) {
    return base::ErrStatus(
        "%s: the following columns are missing from the schema: %s", tag,
        base::Join(columns_missing_from_schema, ", ").c_str());
  }

  if (!columns_missing_from_query.empty()) {
    return base::ErrStatus(
        "%s: the following columns are declared in the schema, but do not "
        "exist: %s",
        tag, base::Join(columns_missing_from_query, ", ").c_str());
  }

  return effective_schema;
}

base::StatusOr<std::vector<std::string>> GetColumnNamesFromSelectStatement(
    const SqliteConnection::PreparedStatement& stmt,
    const char* tag) {
  auto columns =
      static_cast<uint32_t>(sqlite3_column_count(stmt.sqlite_stmt()));
  std::vector<std::string> column_names;
  for (uint32_t i = 0; i < columns; ++i) {
    std::string col_name =
        sqlite3_column_name(stmt.sqlite_stmt(), static_cast<int>(i));
    if (col_name.empty()) {
      return base::ErrStatus("%s: column %u: name must not be empty", tag, i);
    }
    if (!std::isalpha(col_name.front()) && col_name.front() != '_') {
      return base::ErrStatus(
          "%s: Column %u: name '%s' has to start with a letter or underscore.",
          tag, i, col_name.c_str());
    }
    if (!sql_argument::IsValidName(base::StringView(col_name))) {
      return base::ErrStatus(
          "%s: Column %u: name '%s' has to contain only alphanumeric "
          "characters and underscores.",
          tag, i, col_name.c_str());
    }
    column_names.push_back(col_name);
  }
  return column_names;
}

constexpr std::array<std::string_view, 8> kTokensAllowedInMacro{
    "ColumnNameList", "_ProjectionFragment", "_TableNameList",  "ColumnName",
    "Expr",           "TableOrSubquery",     "UnparenExprList", "ExprList",
};

bool IsTokenAllowedInMacro(const std::string& str) {
  base::StringView view = base::StringView{str};
  return std::any_of(kTokensAllowedInMacro.begin(), kTokensAllowedInMacro.end(),
                     [&view](const auto& allowed_token) {
                       return view.CaseInsensitiveEq(base::StringView{
                           allowed_token.data(), allowed_token.size()});
                     });
}

std::string GetTokenNamesAllowedInMacro() {
  std::vector<std::string> result;
  result.reserve(kTokensAllowedInMacro.size());
  for (auto token : kTokensAllowedInMacro) {
    result.emplace_back(token);
  }
  return base::Join(result, ", ");
}

base::StatusOr<dataframe::AdhocDataframeBuilder::ColumnType>
ArgumentTypeToDataframeType(sql_argument::Type type, bool bytes_as_int64) {
  switch (type) {
    case sql_argument::Type::kLong:
    case sql_argument::Type::kBool:
      return dataframe::AdhocDataframeBuilder::ColumnType::kInt64;
    case sql_argument::Type::kDouble:
      return dataframe::AdhocDataframeBuilder::ColumnType::kDouble;
    case sql_argument::Type::kString:
      return dataframe::AdhocDataframeBuilder::ColumnType::kString;
    case sql_argument::Type::kBytes:
      return bytes_as_int64
                 ? base::StatusOr<dataframe::AdhocDataframeBuilder::ColumnType>(
                       dataframe::AdhocDataframeBuilder::ColumnType::kInt64)
                 : base::ErrStatus("Bytes type is not supported");
    case sql_argument::Type::kAny:
      return base::ErrStatus("ANY type cannot be used in table columns");
  }
  PERFETTO_FATAL("For GCC");
}

base::StatusOr<std::vector<dataframe::AdhocDataframeBuilder::ColumnType>>
GetTypesFromSelectStatement(
    bool bytes_as_int64,
    const std::vector<sql_argument::ArgumentDefinition>& schema,
    const std::vector<std::string>& column_names,
    const std::string& name,
    const char* tag) {
  // Should have been checked in ValidateAndGetEffectiveSchema.
  PERFETTO_DCHECK(schema.empty() || schema.size() == column_names.size());
  std::vector<dataframe::AdhocDataframeBuilder::ColumnType> types;
  for (const auto& col : schema) {
    auto type_or = ArgumentTypeToDataframeType(col.type(), bytes_as_int64);
    if (!type_or.ok()) {
      return base::ErrStatus("%s(%s): %s", tag, name.c_str(),
                             type_or.status().c_message());
    }
    types.push_back(*type_or);
  }
  return types;
}

}  // namespace

std::unique_ptr<PerfettoSqlConnection>
PerfettoSqlConnection::CreateConnectionToNewDatabase(StringPool* pool,
                                                     bool enable_extra_checks) {
  return std::unique_ptr<PerfettoSqlConnection>(new PerfettoSqlConnection(
      std::make_shared<PerfettoSqlDatabase>(pool), enable_extra_checks));
}

std::unique_ptr<PerfettoSqlConnection> PerfettoSqlConnection::Fork() {
  return std::unique_ptr<PerfettoSqlConnection>(
      new PerfettoSqlConnection(database_, enable_extra_checks_));
}

PerfettoSqlConnection::~PerfettoSqlConnection() {
  // Scalar function contexts can hold prepared statements (e.g.
  // CreatedFunction::State::stmts_) that must be finalized before the
  // underlying sqlite3* is closed. Explicitly unregister every entry now so
  // SQLite invokes each function's FnCtxDestructor while the database is
  // still alive; |connection_| is destroyed below.
  for (auto it = fn_registry_.GetIterator(); it; ++it) {
    base::Status s = connection_->UnregisterFunction(it.key().first.c_str(),
                                                     it.key().second);
    if (PERFETTO_UNLIKELY(!s.ok())) {
      PERFETTO_FATAL("Failed to drop function: '%s'", it.key().first.c_str());
    }
  }
  fn_registry_.Clear();
}

PerfettoSqlConnection::PerfettoSqlConnection(
    std::shared_ptr<PerfettoSqlDatabase> database,
    bool enable_extra_checks)
    : database_(std::move(database)),
      pool_(database_->pool()),
      enable_extra_checks_(enable_extra_checks),
      connection_(new SqliteConnection(database_->sqlite_database())) {
  // Initialize `perfetto_tables` table, which will contain the names of all of
  // the registered tables.
  char* errmsg_raw = nullptr;
  int err = sqlite3_exec(connection_->db(),
                         "CREATE TABLE perfetto_tables(name STRING);", nullptr,
                         nullptr, &errmsg_raw);
  ScopedSqliteString errmsg(errmsg_raw);
  if (err != SQLITE_OK) {
    PERFETTO_FATAL("Failed to initialize perfetto_tables: %s", errmsg_raw);
  }

  // Register callbacks for transaction management.
  connection_->SetCommitCallback(
      [](void* ctx) {
        return static_cast<PerfettoSqlConnection*>(ctx)->OnCommit();
      },
      this);
  connection_->SetRollbackCallback(
      [](void* ctx) { static_cast<PerfettoSqlConnection*>(ctx)->OnRollback(); },
      this);

  {
    auto ctx = std::make_unique<RuntimeTableFunctionModule::Context>(
        database_->committed_runtime_table_functions());
    runtime_table_fn_context_ = ctx.get();
    RegisterVirtualTableModule<RuntimeTableFunctionModule>(
        "runtime_table_function", std::move(ctx));
  }
  {
    auto ctx = std::make_unique<StaticTableFunctionModule::Context>(
        database_->committed_static_table_functions());
    static_table_fn_context_ = ctx.get();
    RegisterVirtualTableModule<StaticTableFunctionModule>(
        "__intrinsic_static_table_function", std::move(ctx));
  }
  {
    auto ctx = std::make_unique<DataframeModule::Context>(
        database_->committed_dataframes());
    dataframe_context_ = ctx.get();
    RegisterVirtualTableModule<DataframeModule>("__intrinsic_dataframe",
                                                std::move(ctx));
  }
}

base::StatusOr<SqliteConnection::PreparedStatement>
PerfettoSqlConnection::PrepareSqliteStatement(SqlSource sql_source) {
  PerfettoSqlParser parser(database_->macros());
  parser.Reset(std::move(sql_source));
  if (!parser.Next()) {
    return base::ErrStatus("No statement found to prepare");
  }
  const auto* sqlite =
      std::get_if<PerfettoSqlParser::SqliteSql>(&parser.statement());
  if (!sqlite) {
    return base::ErrStatus("Statement was not a valid SQLite statement");
  }
  SqliteConnection::PreparedStatement stmt =
      connection_->PrepareStatement(parser.statement_sql());
  if (parser.Next()) {
    return base::ErrStatus("Too many statements found to prepare");
  }
  return std::move(stmt);
}

void PerfettoSqlConnection::Initialize(Initializer init) {
  // Wrap the ~100 static-table CREATEs in one transaction; otherwise SQLite
  // implicitly commits after each statement.
  {
    Transaction txn(this);
    for (const auto& info : init.static_tables) {
      RegisterStaticTable(info.dataframe, info.name);
    }
    for (auto& info : init.static_table_functions) {
      RegisterStaticTableFunction(std::move(info));
    }
  }
  for (const auto& mod : init.sqlite_modules) {
    if (mod.is_state_manager) {
      virtual_module_state_managers_.push_back(
          static_cast<sqlite::ModuleStateManagerBase*>(mod.context));
    }
    connection_->RegisterVirtualTableModule(mod.name, mod.module, mod.context,
                                            mod.destructor);
  }
  for (auto& fn : init.functions) {
    function_count_++;
    base::Status s = RegisterFunctionAndAddToRegistry(
        fn.name.c_str(), fn.argc, fn.step, fn.ctx, fn.ctx_destructor,
        fn.deterministic);
    if (!s.ok()) {
      PERFETTO_FATAL("Failed to register %s: %s", fn.name.c_str(),
                     s.c_message());
    }
  }
  for (auto& fn : init.aggregate_functions) {
    aggregate_function_count_++;
    base::Status s = connection_->RegisterAggregateFunction(
        fn.name.c_str(), fn.argc, fn.step, fn.final_fn, fn.ctx,
        fn.ctx_destructor, fn.deterministic);
    if (!s.ok()) {
      PERFETTO_FATAL("Failed to register aggregate %s: %s", fn.name.c_str(),
                     s.c_message());
    }
  }
  for (auto& fn : init.window_functions) {
    window_function_count_++;
    base::Status s = connection_->RegisterWindowFunction(
        fn.name.c_str(), fn.argc, fn.step, fn.inverse, fn.value, fn.final_fn,
        fn.ctx, fn.ctx_destructor, fn.deterministic);
    if (!s.ok()) {
      PERFETTO_FATAL("Failed to register window %s: %s", fn.name.c_str(),
                     s.c_message());
    }
  }
}

void PerfettoSqlConnection::RegisterStaticTable(dataframe::Dataframe* df,
                                                const std::string& table_name) {
  PERFETTO_CHECK(!dataframe_context_->temporary_create_state);
  dataframe_context_->temporary_create_state =
      std::make_unique<DataframeModule::State>(df);
  base::StackString<1024> sql(
      "CREATE VIRTUAL TABLE %s USING __intrinsic_dataframe;"
      "INSERT INTO perfetto_tables(name) VALUES('%s');",
      table_name.c_str(), table_name.c_str());
  auto s =
      Execute(SqlSource::FromTraceProcessorImplementation(sql.ToStdString()));
  if (!s.ok()) {
    PERFETTO_FATAL("%s", s.status().c_message());
  }
  PERFETTO_CHECK(!dataframe_context_->temporary_create_state);
}

void PerfettoSqlConnection::RegisterStaticTableFunction(
    std::unique_ptr<StaticTableFunction> fn) {
  std::string name = fn->TableName();

  // Make sure we didn't accidentally leak a state from a previous table
  // creation.
  PERFETTO_CHECK(!static_table_fn_context_->temporary_create_state);
  static_table_fn_context_->temporary_create_state =
      std::make_unique<StaticTableFunctionModule::State>(std::move(fn));

  base::StackString<1024> sql(
      "CREATE VIRTUAL TABLE %s USING __intrinsic_static_table_function;",
      name.c_str());
  auto status =
      Execute(SqlSource::FromTraceProcessorImplementation(sql.ToStdString()));
  if (!status.ok()) {
    PERFETTO_FATAL("%s", status.status().c_message());
  }
  PERFETTO_CHECK(!static_table_fn_context_->temporary_create_state);
}

std::unique_ptr<PerfettoSqlParser> PerfettoSqlConnection::AcquireParser() {
  if (cached_parser_) {
    return std::move(cached_parser_);
  }
  return std::make_unique<PerfettoSqlParser>(database_->macros());
}

base::StatusOr<PerfettoSqlConnection::ExecutionStats>
PerfettoSqlConnection::Execute(SqlSource sql) {
  auto res = ExecuteUntilLastStatement(std::move(sql));
  RETURN_IF_ERROR(res.status());
  if (res->stmt.IsDone()) {
    return res->stats;
  }
  while (res->stmt.Step()) {
  }
  RETURN_IF_ERROR(res->stmt.status());
  return res->stats;
}

base::Status PerfettoSqlConnection::Execute(
    SqlSource sql,
    std::initializer_list<std::string_view> binds) {
  // Bypass the PerfettoSQL frontend: this overload is for hot internal loops
  // running plain SQLite, where the parser/frame setup would dominate.
  sqlite3* db = connection_->db();
  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db, sql.sql().c_str(),
                              static_cast<int>(sql.sql().size()), &stmt,
                              /*pzTail=*/nullptr);
  if (rc != SQLITE_OK) {
    return base::ErrStatus("Prepare failed: %s", sqlite3_errmsg(db));
  }
  int idx = 1;
  for (const auto& b : binds) {
    // |b.data()| must outlive the step+finalize below; SQLITE_STATIC (passed
    // as nullptr destructor) tells SQLite not to copy.
    sqlite3_bind_text(stmt, idx++, b.data(), static_cast<int>(b.size()),
                      /*destructor=*/nullptr);
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return base::ErrStatus("Step failed: %s", sqlite3_errmsg(db));
  }
  return base::OkStatus();
}

PerfettoSqlConnection::Transaction::Transaction(PerfettoSqlConnection* conn)
    : conn_(conn) {
  auto s = conn_->Execute(
      SqlSource::FromTraceProcessorImplementation("BEGIN TRANSACTION"));
  PERFETTO_CHECK(s.ok());
}

PerfettoSqlConnection::Transaction::~Transaction() {
  auto s =
      conn_->Execute(SqlSource::FromTraceProcessorImplementation("COMMIT"));
  PERFETTO_CHECK(s.ok());
}

base::StatusOr<PerfettoSqlConnection::ExecutionResult>
PerfettoSqlConnection::ExecuteUntilLastStatement(SqlSource sql_source) {
  auto result =
      ExecuteStatements(std::move(sql_source), /*end_offset=*/nullptr);
  RETURN_IF_ERROR(result.status());
  PERFETTO_CHECK(result->has_value());
  return std::move(**result);
}

base::StatusOr<std::optional<PerfettoSqlConnection::ExecutionResult>>
PerfettoSqlConnection::ExecuteNextStatement(SqlSource sql_source,
                                            uint32_t* end_offset) {
  PERFETTO_DCHECK(end_offset);
  return ExecuteStatements(std::move(sql_source), end_offset);
}

base::StatusOr<std::optional<PerfettoSqlConnection::ExecutionResult>>
PerfettoSqlConnection::ExecuteStatements(SqlSource sql_source,
                                         uint32_t* end_offset) {
  // Save the current stack size to handle re-entrant Execute() calls.
  // Statement handlers like ExecuteCreateFunction may call Execute()
  // recursively, which would otherwise corrupt our stack state.
  size_t stack_base = execution_stack_.size();

  auto result = ExecuteStatementsImpl(std::move(sql_source), end_offset);

  // Unwind back to our entry point. For include frames on the error path,
  // poison the module so future INCLUDEs of the same key short-circuit, and
  // prepend the include's traceback to the result.
  while (execution_stack_.size() > stack_base) {
    auto& frame = execution_stack_.back();
    if (frame.type == FrameType::kInclude && !result.ok()) {
      PERFETTO_DCHECK(frame.aux);
      frame.aux->include_claim.ReleasePoisoned(result.status().message());
      PERFETTO_DCHECK(frame.aux->traceback_sql);
      std::string traceback = frame.aux->traceback_sql->AsTraceback(0);
      result = base::ErrStatus("%s%s", traceback.c_str(),
                               result.status().c_message());
    }
    execution_stack_.pop_back();
  }
  return result;
}

base::StatusOr<PerfettoSqlConnection::FrameResult>
PerfettoSqlConnection::ProcessFrame(size_t frame_idx) {
  // Handle wildcard frames specially - they just push include frames
  if (execution_stack_[frame_idx].type == FrameType::kWildcard) {
    auto& frame = execution_stack_[frame_idx];
    PERFETTO_DCHECK(frame.aux);
    auto& wc_aux = *frame.aux;
    while (wc_aux.wildcard_index < wc_aux.wildcard_modules.size()) {
      auto& slot = wc_aux.wildcard_modules[wc_aux.wildcard_index++];
      std::string key = std::move(slot.first);
      std::string sql = std::move(slot.second);

      PERFETTO_TP_TRACE(
          metatrace::Category::QUERY_TIMELINE,
          "Include (expanded from wildcard)",
          [&key](metatrace::Record* r) { r->AddArg("Module", key); });

      PERFETTO_DCHECK(wc_aux.wildcard_traceback_sql);
      if (IsKeyOnIncludeStack(key)) {
        std::string traceback = wc_aux.wildcard_traceback_sql->AsTraceback(0);
        return base::ErrStatus(
            "%sINCLUDE: cycle detected — module '%s' is already mid-import "
            "in this execution.",
            traceback.c_str(), key.c_str());
      }
      auto res = database_->TryClaimInclude(key);
      if (res.already_included) {
        continue;
      }
      if (res.poisoned) {
        std::string traceback = wc_aux.wildcard_traceback_sql->AsTraceback(0);
        return base::ErrStatus(
            "%sINCLUDE: module '%s' poisoned by earlier failure: %s",
            traceback.c_str(), key.c_str(), res.poison_reason.c_str());
      }

      // Copy traceback before PushIncludeFrame, which may invalidate frame ref.
      SqlSource traceback = *wc_aux.wildcard_traceback_sql;
      PushIncludeFrame(key, sql, std::move(traceback), std::move(res.claim));
      return FrameResult::kContinue;
    }
    // No more modules to process
    return FrameResult::kFrameDone;
  }

  PERFETTO_DCHECK(execution_stack_[frame_idx].parser);

  // Loop until the parser is exhausted or a child frame is pushed. Hoisting
  // |current| and |parser| out of the frame keeps the hot loop body free of
  // re-indexing into execution_stack_.
  std::optional<SqliteConnection::PreparedStatement> current =
      std::move(execution_stack_[frame_idx].current_stmt);
  execution_stack_[frame_idx].current_stmt.reset();
  const bool stop_after_statement =
      execution_stack_[frame_idx].stop_after_statement;
  PerfettoSqlParser* const parser = execution_stack_[frame_idx].parser.get();
  // In stop-after-statement mode, an engaged |current| means the frame's
  // single statement already executed (it pushed child frames and we are
  // resuming after they completed): skip straight to frame completion.
  while (!(stop_after_statement && current) && parser->Next()) {
    const auto& stmt = parser->statement();

    // Vanilla SQLite is inlined; PerfettoSQL extensions detour through
    // ResolveExtensionStatement, which executes them and returns a dummy
    // statement to prepare.
    std::optional<SqlSource> source_to_prepare;
    const bool is_dummy =
        !std::holds_alternative<PerfettoSqlParser::SqliteSql>(stmt);
    if (PERFETTO_LIKELY(!is_dummy)) {
      source_to_prepare = parser->TakeStatementSql();
    } else {
      ASSIGN_OR_RETURN(source_to_prepare, ResolveExtensionStatement(frame_idx));
    }

    std::optional<SqliteConnection::PreparedStatement> next_stmt;
    {
      PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE, "QUERY_PREPARE");
      auto stmt_result =
          connection_->PrepareStatement(std::move(*source_to_prepare));
      RETURN_IF_ERROR(stmt_result.status());
      next_stmt = std::move(stmt_result);
    }
    PERFETTO_DCHECK(next_stmt->sqlite_stmt());

    // Finish the previous statement before replacing it.
    if (current && !current->IsDone()) {
      PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE,
                        "STMT_STEP_UNTIL_DONE", [&](metatrace::Record* record) {
                          record->AddArg("Original SQL",
                                         current->original_sql());
                          record->AddArg("Executed SQL", current->sql());
                        });
      while (current->Step()) {
      }
      RETURN_IF_ERROR(current->status());
    }
    current = std::move(*next_stmt);
    execution_stack_[frame_idx].current_stmt_is_dummy = is_dummy;

    {
      PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE, "STMT_FIRST_STEP",
                        [&](metatrace::Record* record) {
                          record->AddArg("Original SQL",
                                         current->original_sql());
                          record->AddArg("Executed SQL", current->sql());
                        });
      current->Step();
      RETURN_IF_ERROR(current->status());
    }

    IncrementCountForStmt(*current,
                          &execution_stack_[frame_idx].accumulated_stats);

    // Yield if a child frame was pushed.
    if (execution_stack_.size() > frame_idx + 1) {
      execution_stack_[frame_idx].current_stmt = std::move(current);
      return FrameResult::kContinue;
    }
  }
  execution_stack_[frame_idx].current_stmt = std::move(current);

  // No more statements in this frame - check parser status
  auto& frame = execution_stack_[frame_idx];
  RETURN_IF_ERROR(frame.parser->status());

  // Handle frame completion based on frame type
  if (frame.type == FrameType::kRoot) {
    // Root frame completion - return result
    if (!frame.current_stmt) {
      if (frame.stop_after_statement) {
        return FrameResult::kNoStatement;
      }
      return base::ErrStatus("No valid SQL to run");
    }
    // Dummy statements of transpiled PerfettoSQL statements have no result
    // set: don't leak the dummy's phantom column.
    frame.accumulated_stats.column_count =
        frame.current_stmt_is_dummy
            ? 0u
            : static_cast<uint32_t>(
                  sqlite3_column_count(frame.current_stmt->sqlite_stmt()));
    return FrameResult::kReturnResult;
  }

  // Include frame completion
  PERFETTO_DCHECK(frame.type == FrameType::kInclude);
  PERFETTO_DCHECK(frame.aux);
  if (frame.accumulated_stats.statement_count_with_output > 0) {
    return base::ErrStatus("INCLUDE: Included module returning values.");
  }
  frame.aux->include_claim.ReleaseSuccess();
  return FrameResult::kFrameDone;
}

base::StatusOr<SqlSource> PerfettoSqlConnection::ResolveExtensionStatement(
    size_t frame_idx) {
  // |stmt| and |stmt_sql| are refs into parser-owned heap storage and remain
  // valid across calls that may reallocate |execution_stack_|.
  const auto& stmt = execution_stack_[frame_idx].parser->statement();
  const auto& stmt_sql = execution_stack_[frame_idx].parser->statement_sql();
  if (const auto* cf = std::get_if<PerfettoSqlParser::CreateFunction>(&stmt)) {
    RETURN_IF_ERROR(AddTracebackIfNeeded(ExecuteCreateFunction(*cf), stmt_sql));
  } else if (const auto* cst =
                 std::get_if<PerfettoSqlParser::CreateTable>(&stmt)) {
    RETURN_IF_ERROR(AddTracebackIfNeeded(ExecuteCreateTable(*cst), stmt_sql));
  } else if (const auto* create_view =
                 std::get_if<PerfettoSqlParser::CreateView>(&stmt)) {
    RETURN_IF_ERROR(
        AddTracebackIfNeeded(ExecuteCreateView(*create_view), stmt_sql));
  } else if (const auto* include =
                 std::get_if<PerfettoSqlParser::Include>(&stmt)) {
    // ExecuteInclude may push new frames onto the stack.
    RETURN_IF_ERROR(
        ExecuteInclude(*include, *execution_stack_[frame_idx].parser));
  } else if (const auto* macro =
                 std::get_if<PerfettoSqlParser::CreateMacro>(&stmt)) {
    // Capture macro->sql before ExecuteCreateMacro consumes it.
    auto sql = macro->sql;
    RETURN_IF_ERROR(ExecuteCreateMacro(*macro));
    return RewriteToDummySql(sql);
  } else if (const auto* create_index =
                 std::get_if<PerfettoSqlParser::CreateIndex>(&stmt)) {
    RETURN_IF_ERROR(ExecuteCreateIndex(*create_index));
  } else if (const auto* drop_index =
                 std::get_if<PerfettoSqlParser::DropIndex>(&stmt)) {
    RETURN_IF_ERROR(ExecuteDropIndex(*drop_index));
  } else {
    // SqliteSql is inlined in ProcessFrame's hot path.
    PERFETTO_FATAL("Unexpected statement variant");
  }
  return RewriteToDummySql(stmt_sql);
}

base::StatusOr<std::optional<PerfettoSqlConnection::ExecutionResult>>
PerfettoSqlConnection::ExecuteStatementsImpl(SqlSource sql_source,
                                             uint32_t* end_offset) {
  // A SQL string can contain several statements. Some of them might be
  // comment only, e.g. "SELECT 1; /* comment */; SELECT 2;". Some statements
  // can also be PerfettoSQL statements which we need to transpile before
  // execution or execute without delegating to SQLite.
  //
  // The logic here is the following:
  //  - We parse the statement as a PerfettoSQL statement.
  //  - If the statement is something we can execute, execute it instantly and
  //    prepare a dummy SQLite statement so the rest of the code continues to
  //    work correctly.
  //  - If the statement is actually an SQLite statement, we invoke PrepareStmt.
  //  - We step once to make sure side effects take effect (e.g. for CREATE
  //    TABLE statements, tables are created).
  //  - If we encounter a valid statement afterwards, we step internally through
  //    all rows of the previous one. This ensures that any further side
  //    effects take hold *before* we step into the next statement.
  //  - Once no further statements are encountered, we return the prepared
  //    statement for the last valid statement.
  //
  // When |end_offset| is non-null, the root frame instead stops after the
  // first statement (ExecuteNextStatement).
  //
  // When an INCLUDE statement is encountered, the included module's SQL is
  // pushed onto the execution stack and processed before continuing with the
  // current SQL. This uses an explicit stack to avoid deep recursion.

  auto source_size = static_cast<uint32_t>(sql_source.sql().size());
  auto root_parser = AcquireParser();
  root_parser->Reset(std::move(sql_source));
  execution_stack_.emplace_back(ExecutionFrame{FrameType::kRoot,
                                               std::move(root_parser),
                                               /*accumulated_stats=*/{},
                                               /*current_stmt=*/std::nullopt,
                                               /*aux=*/nullptr,
                                               /*stop_after_statement=*/
                                               end_offset != nullptr});

  while (!execution_stack_.empty()) {
    size_t frame_idx = execution_stack_.size() - 1;
    auto result = ProcessFrame(frame_idx);
    RETURN_IF_ERROR(result.status());

    switch (*result) {
      case FrameResult::kContinue:
        continue;
      case FrameResult::kFrameDone:
        execution_stack_.pop_back();
        continue;
      case FrameResult::kReturnResult: {
        auto& frame = execution_stack_.back();
        if (end_offset) {
          *end_offset = frame.parser->statement_end_offset();
        }
        ExecutionResult res{std::move(*frame.current_stmt),
                            frame.accumulated_stats};
        if (!cached_parser_) {
          cached_parser_ = std::move(frame.parser);
        }
        execution_stack_.pop_back();
        return std::optional<ExecutionResult>(std::move(res));
      }
      case FrameResult::kNoStatement: {
        auto& frame = execution_stack_.back();
        PERFETTO_DCHECK(end_offset && frame.stop_after_statement);
        *end_offset = source_size;
        if (!cached_parser_) {
          cached_parser_ = std::move(frame.parser);
        }
        execution_stack_.pop_back();
        return std::optional<ExecutionResult>();
      }
    }
  }
  PERFETTO_FATAL("Unexpected empty execution stack");
}

const dataframe::Dataframe* PerfettoSqlConnection::GetDataframeOrNull(
    const std::string& name) const {
  auto* state = dataframe_context_->GetStateByName(name);
  return state ? state->dataframe : nullptr;
}

base::Status PerfettoSqlConnection::RegisterLegacyRuntimeFunction(
    bool replace,
    const FunctionPrototype& prototype,
    sql_argument::Type return_type,
    SqlSource sql) {
  int created_argc = static_cast<int>(prototype.arguments.size());
  // Refuse to clobber a C++ intrinsic. The reused-ctx fast path below ends in
  // CreatedFunction::Reset(), which destroys and placement-news a
  // CreatedFunction::State over the existing object; if the existing function
  // were an intrinsic (e.g. import, which registers a raw
  // PerfettoSqlConnection*), the destructor call would dereference a
  // non-existent vtable and the placement-new would corrupt the intrinsic's
  // state -- type-confusion bug previously exploitable via
  // CREATE OR REPLACE PERFETTO FUNCTION import(...).
  if (IsIntrinsicFunction(prototype.function_name, created_argc)) {
    return base::ErrStatus(
        "CREATE PERFETTO FUNCTION[prototype=%s]: cannot redefine built-in "
        "function with the same name and argument count",
        prototype.ToString().c_str());
  }
  auto* ctx = static_cast<CreatedFunction::UserData*>(
      GetFunctionContextOrNull(prototype.function_name, created_argc));
  if (ctx) {
    if (CreatedFunction::IsValid(ctx) && !replace) {
      return base::ErrStatus(
          "CREATE PERFETTO FUNCTION[prototype=%s]: function already exists",
          prototype.ToString().c_str());
    }
    CreatedFunction::Reset(ctx, this);
  } else {
    // We register the function with SQLite before we prepare the statement so
    // the statement can reference the function itself, enabling recursive
    // calls.
    std::unique_ptr<CreatedFunction::UserData> created_fn_ctx =
        CreatedFunction::MakeContext(this);
    ctx = created_fn_ctx.get();
    RegisterFunctionArgs args(prototype.function_name.c_str(), true,
                              static_cast<int>(prototype.arguments.size()));
    args.is_intrinsic = false;
    RETURN_IF_ERROR(
        RegisterFunction<CreatedFunction>(std::move(created_fn_ctx), args));
  }
  return CreatedFunction::Prepare(ctx, prototype, return_type, std::move(sql));
}

base::Status PerfettoSqlConnection::ExecuteCreateTable(
    const PerfettoSqlParser::CreateTable& create_table) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE,
                    "CREATE PERFETTO TABLE",
                    [&create_table](metatrace::Record* record) {
                      record->AddArg("table_name", create_table.name);
                    });
  auto stmt_or = connection_->PrepareStatement(create_table.sql);
  RETURN_IF_ERROR(stmt_or.status());
  SqliteConnection::PreparedStatement stmt = std::move(stmt_or);
  ASSIGN_OR_RETURN(auto column_names, GetColumnNamesFromSelectStatement(
                                          stmt, "CREATE PERFETTO TABLE"));
  ASSIGN_OR_RETURN(auto schema, ValidateAndGetEffectiveSchema(
                                    column_names, create_table.schema,
                                    "CREATE PERFETTO TABLE"));
  ASSIGN_OR_RETURN(auto types, GetTypesFromSelectStatement(
                                   false, schema, column_names,
                                   create_table.name, "CREATE PERFETTO TABLE"));
  stmt.Step();
  RETURN_IF_ERROR(stmt.status());
  SqliteDataframeBuilderOptions options;
  options.column_types = std::move(types);
  const std::string error_context =
      "CREATE PERFETTO TABLE(" + create_table.name + ")";
  ASSIGN_OR_RETURN(auto builder, BuildRuntimeDataframeFromSqliteStatement(
                                     pool_, std::move(column_names), &stmt,
                                     error_context, std::move(options)));
  ASSIGN_OR_RETURN(auto dataframe, std::move(builder).Build());

  base::StackString<1024> drop("DROP TABLE IF EXISTS %s;",
                               create_table.name.c_str());
  base::StackString<1024> sql_str(
      R"(
      SAVEPOINT create_table_using_dataframe;
      %s
      CREATE VIRTUAL TABLE %s USING __intrinsic_dataframe;
      RELEASE SAVEPOINT create_table_using_dataframe;
      )",
      create_table.replace ? drop.c_str() : "", create_table.name.c_str());

  // Make sure we didn't accidentally leak a state from a previous function
  // creation.
  PERFETTO_CHECK(!dataframe_context_->temporary_create_state);
  dataframe_context_->temporary_create_state =
      std::make_unique<DataframeModule::State>(
          std::make_unique<dataframe::Dataframe>(std::move(dataframe)));

  auto exec_res = Execute(
      SqlSource::FromTraceProcessorImplementation(sql_str.ToStdString()));
  if (exec_res.ok()) {
    PERFETTO_CHECK(!dataframe_context_->temporary_create_state);
  } else {
    dataframe_context_->temporary_create_state.reset();

    auto rollback_res = Execute(SqlSource::FromTraceProcessorImplementation(
        "ROLLBACK TO create_table_using_dataframe; "
        "RELEASE create_table_using_dataframe;"));
    // Failing a rollback/release is pretty catastrophic as we have no idea
    // what state the database is in anymore.
    // TODO(lalitm): turn this into a fatal error once we understand why
    // this is happening in Google3.
    if (!rollback_res.ok()) {
      PERFETTO_LOG(
          "Failed to rollback after CREATE PERFETTO TABLE(%s): %s. Original "
          "error: %s",
          create_table.name.c_str(), rollback_res.status().c_message(),
          exec_res.status().c_message());
    }
  }
  return exec_res.status();
}

base::Status PerfettoSqlConnection::ExecuteCreateView(
    const PerfettoSqlParser::CreateView& create_view) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE, "CREATE PERFETTO VIEW",
                    [&create_view](metatrace::Record* record) {
                      record->AddArg("view_name", create_view.name);
                    });

  // Verify that the underlying SQL statement is valid.
  auto stmt = sqlite_connection()->PrepareStatement(create_view.sql);
  RETURN_IF_ERROR(stmt.status());

  if (create_view.replace) {
    base::StackString<1024> drop_if_exists("DROP VIEW IF EXISTS %s",
                                           create_view.name.c_str());
    RETURN_IF_ERROR(Execute(SqlSource::FromTraceProcessorImplementation(
                                drop_if_exists.ToStdString()))
                        .status());
  }

  // If the schema is specified, verify that the column names match it.
  if (!create_view.schema.empty()) {
    base::StatusOr<std::vector<std::string>> maybe_column_names =
        GetColumnNamesFromSelectStatement(stmt, "CREATE PERFETTO VIEW");
    RETURN_IF_ERROR(maybe_column_names.status());
    const std::vector<std::string>& column_names = *maybe_column_names;

    ASSIGN_OR_RETURN(
        auto effective_schema,
        ValidateAndGetEffectiveSchema(column_names, create_view.schema,
                                      "CREATE PERFETTO VIEW"));
    if (enable_extra_checks_) {
      // If extra checks are enabled, materialize the view to ensure that its
      // values are correct.
      ASSIGN_OR_RETURN(auto types,
                       GetTypesFromSelectStatement(
                           true, effective_schema, column_names,
                           create_view.name, "CREATE PERFETTO VIEW"));
      stmt.Step();
      RETURN_IF_ERROR(stmt.status());
      SqliteDataframeBuilderOptions options;
      options.column_types = std::move(types);
      options.blobs_as_null = true;
      const std::string error_context =
          "CREATE PERFETTO VIEW(" + create_view.name + ")";
      ASSIGN_OR_RETURN(auto builder, BuildRuntimeDataframeFromSqliteStatement(
                                         pool_, std::move(column_names), &stmt,
                                         error_context, std::move(options)));
      base::StatusOr<dataframe::Dataframe> materialized =
          std::move(builder).Build();
      RETURN_IF_ERROR(materialized.status());
    }
  }
  RETURN_IF_ERROR(Execute(create_view.create_view_sql).status());
  return base::OkStatus();
}

base::Status PerfettoSqlConnection::EnableSqlFunctionMemoization(
    const std::string& name) {
  constexpr int kSupportedArgCount = 1;
  // Refuse EXPERIMENTAL_MEMOIZE on intrinsics: their ctx is opaque and casting
  // it to CreatedFunction::UserData* would walk a non-existent vtable inside
  // EnableMemoization.
  if (IsIntrinsicFunction(name, kSupportedArgCount)) {
    return base::ErrStatus(
        "EXPERIMENTAL_MEMOIZE: '%s' is a built-in function and cannot be "
        "memoized",
        name.c_str());
  }
  auto* ctx = static_cast<CreatedFunction::UserData*>(
      GetFunctionContextOrNull(name, kSupportedArgCount));
  if (!ctx) {
    return base::ErrStatus(
        "EXPERIMENTAL_MEMOIZE: Function '%s'(INT) does not exist",
        name.c_str());
  }
  return CreatedFunction::EnableMemoization(ctx);
}

base::Status PerfettoSqlConnection::ExecuteInclude(
    const PerfettoSqlParser::Include& include,
    const PerfettoSqlParser& parser) {
  PERFETTO_TP_TRACE(
      metatrace::Category::QUERY_TIMELINE, "INCLUDE PERFETTO MODULE",
      [&](metatrace::Record* r) { r->AddArg("include", include.key); });

  const std::string& key = include.key;
  if (key == "*") {
    for (auto package = database_->packages().GetIterator(); package;
         ++package) {
      RETURN_IF_ERROR(IncludePackageImpl(package.value(), key, parser));
    }
    return base::OkStatus();
  }

  // Find the package that owns this module by looking for a package whose
  // name is a prefix of the key. Multi-level package names are supported
  // (e.g., package "android.camera" owns module "android.camera.jank").
  auto* package = FindPackageForModule(key);
  if (!package) {
    std::string first_component = sql_modules::GetPackageName(key);
    if (first_component == "common") {
      return base::ErrStatus(
          "INCLUDE: Package `common` has been removed and most of the "
          "functionality has been moved to other packages. Check "
          "`slices.with_context` for replacement for `common.slices` and "
          "`time.conversion` for replacement for `common.timestamps`. The "
          "documentation for Perfetto standard library can be found at "
          "https://perfetto.dev/docs/analysis/stdlib-docs.");
    }
    return base::ErrStatus("INCLUDE: Package '%s' not found", key.c_str());
  }
  return IncludePackageImpl(*package, key, parser);
}

base::Status PerfettoSqlConnection::ExecuteCreateIndex(
    const PerfettoSqlParser::CreateIndex& create_index) {
  PERFETTO_TP_TRACE(
      metatrace::Category::QUERY_TIMELINE, "CREATE PERFETTO INDEX",
      [&create_index](metatrace::Record* record) {
        record->AddArg("index_name", create_index.name);
        record->AddArg("table_name", create_index.table_name);
        record->AddArg("cols", base::Join(create_index.col_names, ", "));
      });
  DataframeModule::State* state =
      dataframe_context_->GetStateByName(create_index.table_name);
  if (!state) {
    return base::ErrStatus("CREATE PERFETTO INDEX: table '%s' does not exist",
                           create_index.table_name.c_str());
  }
  if (!state->owned_dataframe) {
    return base::ErrStatus(
        "CREATE PERFETTO INDEX: indexes on intrinsic table '%s' must be "
        "declared at compile time, not via SQL",
        create_index.table_name.c_str());
  }
  RETURN_IF_ERROR(DropIndexBeforeCreate(create_index));

  const auto& df = *state->dataframe;
  std::vector<uint32_t> col_idxs;
  for (const std::string& col_name : create_index.col_names) {
    auto it =
        std::find(df.column_names().begin(), df.column_names().end(), col_name);
    if (it == df.column_names().end()) {
      return base::ErrStatus(
          "CREATE PERFETTO INDEX: Column '%s' not found in table '%s'",
          col_name.c_str(), create_index.table_name.c_str());
    }
    col_idxs.push_back(
        static_cast<uint32_t>(std::distance(df.column_names().begin(), it)));
  }
  ASSIGN_OR_RETURN(auto index,
                   state->dataframe->BuildIndex(
                       col_idxs.data(), col_idxs.data() + col_idxs.size()));
  state->owned_dataframe = std::make_unique<dataframe::Dataframe>(
      state->dataframe->AddIndex(std::move(index)));
  state->dataframe = state->owned_dataframe.get();
  state->named_indexes.push_back(create_index.name);
  return base::OkStatus();
}

base::Status PerfettoSqlConnection::DropIndexBeforeCreate(
    const PerfettoSqlParser::CreateIndex& create_index) {
  for (const auto& [name, state] : dataframe_context_->GetAllStates()) {
    for (uint32_t i = 0; i < state->named_indexes.size(); ++i) {
      if (state->named_indexes[i] == create_index.name) {
        if (!create_index.replace) {
          return base::ErrStatus(
              "CREATE PERFETTO INDEX: Index '%s' already exists",
              create_index.name.c_str());
        }
        state->owned_dataframe = std::make_unique<dataframe::Dataframe>(
            state->dataframe->RemoveIndexAt(i));
        state->dataframe = state->owned_dataframe.get();
        state->named_indexes.erase(state->named_indexes.begin() +
                                   static_cast<std::ptrdiff_t>(i));
        return base::OkStatus();
      }
    }
  }
  return base::OkStatus();
}

base::Status PerfettoSqlConnection::ExecuteDropIndex(
    const PerfettoSqlParser::DropIndex& index) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE, "DROP PERFETTO INDEX",
                    [&index](metatrace::Record* record) {
                      record->AddArg("index_name", index.name);
                      record->AddArg("table_name", index.table_name);
                    });
  for (const auto& [name, state] : dataframe_context_->GetAllStates()) {
    PERFETTO_CHECK(state->named_indexes.empty() ||
                   state->dataframe->finalized());
    for (uint32_t i = 0; i < state->named_indexes.size(); ++i) {
      if (state->named_indexes[i] == index.name) {
        state->owned_dataframe = std::make_unique<dataframe::Dataframe>(
            state->dataframe->RemoveIndexAt(i));
        state->dataframe = state->owned_dataframe.get();
        state->named_indexes.erase(state->named_indexes.begin() +
                                   static_cast<std::ptrdiff_t>(i));
        return base::OkStatus();
      }
    }
  }
  return base::ErrStatus("DROP PERFETTO INDEX: Index '%s' not found",
                         index.name.c_str());
}

base::Status PerfettoSqlConnection::IncludePackageImpl(
    sql_modules::RegisteredPackage& package,
    const std::string& include_key,
    const PerfettoSqlParser& parser) {
  if (!include_key.empty() && include_key.back() == '*') {
    // If the key ends with a wildcard, collect all matching (key, sql) pairs
    // and push a wildcard frame that will process them one at a time. The
    // expander goes through |TryClaimInclude| per module; already-included
    // modules are skipped silently and poisoned modules surface their
    // recorded reason.
    std::string prefix = include_key.substr(0, include_key.size() - 1);
    std::vector<std::pair<std::string, std::string>> matching_modules;
    for (auto module = package.modules.GetIterator(); module; ++module) {
      if (!base::StartsWith(module.key(), prefix))
        continue;
      matching_modules.emplace_back(module.key(), module.value());
    }

    if (matching_modules.empty()) {
      return base::OkStatus();
    }

    auto aux = std::make_unique<ExecutionFrameAux>();
    aux->wildcard_modules = std::move(matching_modules);
    aux->wildcard_traceback_sql = parser.statement_sql();
    execution_stack_.emplace_back(ExecutionFrame{FrameType::kWildcard,
                                                 /*parser=*/nullptr,
                                                 /*accumulated_stats=*/{},
                                                 /*current_stmt=*/std::nullopt,
                                                 std::move(aux)});
    return base::OkStatus();
  }
  auto* module_sql = package.modules.Find(include_key);
  if (!module_sql) {
    return base::ErrStatus("INCLUDE: unknown module '%s'", include_key.c_str());
  }
  return IncludeModuleImpl(include_key, *module_sql, parser);
}

bool PerfettoSqlConnection::IsKeyOnIncludeStack(const std::string& key) const {
  for (const auto& f : execution_stack_) {
    if (f.type == FrameType::kInclude && f.aux && f.aux->include_key == key) {
      return true;
    }
  }
  return false;
}

base::Status PerfettoSqlConnection::IncludeModuleImpl(
    const std::string& key,
    std::string_view sql,
    const PerfettoSqlParser& parser) {
  if (IsKeyOnIncludeStack(key)) {
    std::string traceback = parser.statement_sql().AsTraceback(0);
    return base::ErrStatus(
        "%sINCLUDE: cycle detected — module '%s' is already mid-import in "
        "this execution.",
        traceback.c_str(), key.c_str());
  }
  auto res = database_->TryClaimInclude(key);
  if (res.already_included) {
    return base::OkStatus();
  }
  if (res.poisoned) {
    std::string traceback = parser.statement_sql().AsTraceback(0);
    return base::ErrStatus(
        "%sINCLUDE: module '%s' poisoned by earlier failure: %s",
        traceback.c_str(), key.c_str(), res.poison_reason.c_str());
  }
  PushIncludeFrame(key, sql, parser.statement_sql(), std::move(res.claim));
  return base::OkStatus();
}

void PerfettoSqlConnection::PushIncludeFrame(
    const std::string& key,
    std::string_view sql,
    SqlSource traceback_sql,
    PerfettoSqlDatabase::IncludeClaim claim) {
  auto aux = std::make_unique<ExecutionFrameAux>();
  aux->include_key = key;
  aux->traceback_sql = std::move(traceback_sql);
  aux->include_claim = std::move(claim);
  auto inc_parser = AcquireParser();
  inc_parser->Reset(SqlSource::FromModuleInclude(std::string(sql), key));
  execution_stack_.emplace_back(
      ExecutionFrame{FrameType::kInclude, std::move(inc_parser),
                     /*accumulated_stats=*/{},
                     /*current_stmt=*/std::nullopt, std::move(aux)});
}

base::Status PerfettoSqlConnection::ExecuteCreateFunction(
    const PerfettoSqlParser::CreateFunction& cf) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE,
                    "CREATE PERFETTO FUNCTION",
                    [&cf](metatrace::Record* record) {
                      record->AddArg("name", cf.prototype.function_name);
                      record->AddArg("prototype", cf.prototype.ToString());
                    });

  // Handle delegating function creation
  if (cf.target_function.has_value()) {
    return RegisterDelegatingFunction(cf);
  }

  if (!cf.returns.is_table) {
    return RegisterLegacyRuntimeFunction(cf.replace, cf.prototype,
                                         cf.returns.scalar_type, cf.sql);
  }

  auto state = std::make_unique<RuntimeTableFunctionModule::State>(
      RuntimeTableFunctionModule::State{
          this,
          cf.sql,
          cf.prototype,
          cf.returns.table_columns,
          std::nullopt,
      });

  // Verify that the provided SQL prepares to a statement correctly.
  auto stmt = sqlite_connection()->PrepareStatement(cf.sql);
  RETURN_IF_ERROR(stmt.status());

  // Verify that every argument name in the function appears in the
  // argument list.
  //
  // We intentionally loop from 1 to |used_param_count| because SQL
  // parameters are 1-indexed *not* 0-indexed.
  int used_param_count = sqlite3_bind_parameter_count(stmt.sqlite_stmt());
  for (int i = 1; i <= used_param_count; ++i) {
    const char* name = sqlite3_bind_parameter_name(stmt.sqlite_stmt(), i);

    if (!name) {
      return base::ErrStatus(
          "%s: \"Nameless\" SQL parameters cannot be used in the SQL "
          "statements of view functions.",
          state->prototype.function_name.c_str());
    }

    if (!base::StringView(name).StartsWith("$")) {
      return base::ErrStatus(
          "%s: invalid parameter name %s used in the SQL definition of "
          "the view function: all parameters must be prefixed with '$' not "
          "':' or '@'.",
          state->prototype.function_name.c_str(), name);
    }

    auto it = std::find_if(state->prototype.arguments.begin(),
                           state->prototype.arguments.end(),
                           [name](const sql_argument::ArgumentDefinition& arg) {
                             return arg.dollar_name() == name;
                           });
    if (it == state->prototype.arguments.end()) {
      return base::ErrStatus(
          "%s: parameter %s does not appear in the list of arguments in the "
          "prototype of the view function.",
          state->prototype.function_name.c_str(), name);
    }
  }

  // Verify that the prepared statement column count matches the return
  // count.
  auto col_count =
      static_cast<uint32_t>(sqlite3_column_count(stmt.sqlite_stmt()));
  if (col_count != state->return_values.size()) {
    return base::ErrStatus(
        "%s: number of return values %u does not match SQL statement column "
        "count %zu.",
        state->prototype.function_name.c_str(), col_count,
        state->return_values.size());
  }

  // Verify that the return names matches the prepared statement column names.
  for (uint32_t i = 0; i < col_count; ++i) {
    const char* name =
        sqlite3_column_name(stmt.sqlite_stmt(), static_cast<int>(i));
    if (name != state->return_values[i].name()) {
      return base::ErrStatus(
          "%s: column %s at index %u does not match return value name %s.",
          state->prototype.function_name.c_str(), name, i,
          state->return_values[i].name().c_str());
    }
  }
  state->temporary_create_stmt = std::move(stmt);

  // TODO(lalitm): this suffers the same non-atomic DROP/CREATE problem as
  // CREATE PERFETTO TABLE implementation above: see the comment there for
  // more info on this.
  if (cf.replace) {
    base::StackString<1024> drop("DROP TABLE IF EXISTS %s",
                                 state->prototype.function_name.c_str());
    auto res = Execute(
        SqlSource::FromTraceProcessorImplementation(drop.ToStdString()));
    RETURN_IF_ERROR(res.status());
  }

  base::StackString<1024> create(
      "CREATE VIRTUAL TABLE %s USING runtime_table_function",
      state->prototype.function_name.c_str());

  // Make sure we didn't accidentally leak a state from a previous function
  // creation.
  PERFETTO_CHECK(!runtime_table_fn_context_->temporary_create_state);

  // Move the state into the context so that it will be picked up in xCreate
  // of RuntimeTableFunctionModule.
  runtime_table_fn_context_->temporary_create_state = std::move(state);
  auto status = Execute(cf.sql.RewriteAllIgnoreExisting(
                            SqlSource::FromTraceProcessorImplementation(
                                create.ToStdString())))
                    .status();

  // If an error happened, it's possible that the state was not picked up.
  // Therefore, always reset the state just in case. OTOH if the creation
  // succeeded, the state should always have been captured.
  if (status.ok()) {
    PERFETTO_CHECK(!runtime_table_fn_context_->temporary_create_state);
  } else {
    runtime_table_fn_context_->temporary_create_state.reset();
  }
  return status;
}

base::Status PerfettoSqlConnection::RegisterDelegatingFunction(
    const PerfettoSqlParser::CreateFunction& cf) {
  PERFETTO_DCHECK(cf.target_function.has_value());

  const std::string& target_function_name = *cf.target_function;
  const std::string& new_name = cf.prototype.function_name;

  // Look up the target function in our registry
  IntrinsicFunctionInfo* info_ptr =
      intrinsic_function_registry_.Find(base::ToLower(target_function_name));
  if (info_ptr == nullptr) {
    return base::ErrStatus(
        "Target function '%s' not found in registry. "
        "Make sure it has been registered as an available function for "
        "delegation.",
        target_function_name.c_str());
  }

  const IntrinsicFunctionInfo& info = *info_ptr;

  // Determine the argument count. If the last argument is variadic, use -1
  // (SQLite's indicator for variadic functions). Otherwise use the argument
  // count from the prototype.
  bool has_variadic = !cf.prototype.arguments.empty() &&
                      cf.prototype.arguments.back().is_variadic();
  int argc =
      has_variadic ? -1 : static_cast<int>(cf.prototype.arguments.size());

  // The prototype's argc must match the target function's argc.
  PERFETTO_CHECK(argc == info.argc);

  // Check if function already exists and handle replace logic
  auto* existing_ctx = GetFunctionContextOrNull(new_name, argc);
  if (existing_ctx) {
    if (!cf.replace) {
      return base::ErrStatus(
          "CREATE PERFETTO FUNCTION[prototype=%s]: function already exists. "
          "Use CREATE OR REPLACE to overwrite.",
          cf.prototype.ToString().c_str());
    }
    // SQLite will overwrite the existing function when we register with the
    // same name - no explicit deletion needed
  }

  // Register the function with SQLite using the new alias name
  RETURN_IF_ERROR(RegisterFunctionAndAddToRegistry(
      new_name.c_str(), info.argc, info.func, info.ctx,
      nullptr,  // no destructor needed for aliased functions
      info.deterministic));

  return base::OkStatus();
}

base::Status PerfettoSqlConnection::RegisterFunctionAndAddToRegistry(
    const char* name,
    int argc,
    SqliteConnection::Fn* func,
    void* ctx,
    SqliteConnection::FnCtxDestructor* ctx_destructor,
    bool deterministic,
    bool is_intrinsic) {
  // Register with SQLite
  RETURN_IF_ERROR(connection_->RegisterFunction(name, argc, func, ctx,
                                                ctx_destructor, deterministic));

  // Track ownership / kind. This is the only place that records whether a
  // function's context is opaque (intrinsic) or a typed Destructible-derived
  // state; the security-sensitive paths in |RegisterLegacyRuntimeFunction| and
  // |EnableSqlFunctionMemoization| consult |IsIntrinsicFunction| before
  // downcasting. Keys are lowercased to match SQLite's case-insensitive
  // function namespace; otherwise CREATE OR REPLACE PERFETTO FUNCTION
  // IMPORT(...) (mixed-case) could bypass the intrinsic check.
  FunctionEntry entry{ctx, is_intrinsic};
  *fn_registry_.Insert(std::make_pair(base::ToLower(name), argc), entry).first =
      entry;

  // Also add to intrinsic registry for potential aliasing
  IntrinsicFunctionInfo info;
  info.func = func;
  info.argc = argc;
  info.ctx = ctx;
  info.deterministic = deterministic;
  intrinsic_function_registry_[base::ToLower(name)] = info;

  return base::OkStatus();
}

void* PerfettoSqlConnection::GetFunctionContextOrNull(const std::string& name,
                                                      int argc) const {
  const auto* entry =
      fn_registry_.Find(std::make_pair(base::ToLower(name), argc));
  return entry ? entry->ctx : nullptr;
}

bool PerfettoSqlConnection::IsIntrinsicFunction(const std::string& name,
                                                int argc) const {
  const auto* entry =
      fn_registry_.Find(std::make_pair(base::ToLower(name), argc));
  return entry && entry->is_intrinsic;
}

base::Status PerfettoSqlConnection::ExecuteCreateMacro(
    const PerfettoSqlParser::CreateMacro& create_macro) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE,
                    "CREATE PERFETTO MACRO",
                    [&create_macro](metatrace::Record* record) {
                      record->AddArg("name", create_macro.name.sql());
                    });

  // Check that the argument types is one of the allowed types.
  for (const auto& [name, type] : create_macro.args) {
    if (!IsTokenAllowedInMacro(type.sql())) {
      // TODO(lalitm): add a link to create macro documentation.
      return base::ErrStatus(
          "%sMacro '%s' argument '%s' is unknown type '%s'. Allowed types: "
          "%s",
          type.AsTraceback(0).c_str(), create_macro.name.sql().c_str(),
          name.sql().c_str(), type.sql().c_str(),
          GetTokenNamesAllowedInMacro().c_str());
    }
  }
  if (!IsTokenAllowedInMacro(create_macro.returns.sql())) {
    // TODO(lalitm): add a link to create macro documentation.
    return base::ErrStatus(
        "%sMacro %s return type %s is unknown. Allowed types: %s",
        create_macro.returns.AsTraceback(0).c_str(),
        create_macro.name.sql().c_str(), create_macro.returns.sql().c_str(),
        GetTokenNamesAllowedInMacro().c_str());
  }

  std::vector<std::string> args;
  args.reserve(create_macro.args.size());
  for (const auto& arg : create_macro.args) {
    args.push_back(arg.first.sql());
  }
  PerfettoSqlParser::Macro macro{
      create_macro.replace,
      create_macro.name.sql(),
      std::move(args),
      create_macro.sql,
  };
  if (auto* it = database_->macros().Find(create_macro.name.sql()); it) {
    if (!create_macro.replace) {
      // TODO(lalitm): add a link to create macro documentation.
      return base::ErrStatus("%sMacro already exists",
                             create_macro.name.AsTraceback(0).c_str());
    }
    *it = std::move(macro);
    return base::OkStatus();
  }
  std::string name = macro.name;
  auto it_and_inserted =
      database_->macros().Insert(std::move(name), std::move(macro));
  PERFETTO_CHECK(it_and_inserted.second);
  return base::OkStatus();
}

int PerfettoSqlConnection::OnCommit() {
  for (auto* ctx : virtual_module_state_managers_) {
    ctx->OnCommit();
  }
  return 0;
}

void PerfettoSqlConnection::OnRollback() {
  for (auto* ctx : virtual_module_state_managers_) {
    ctx->OnRollback();
  }
}

}  // namespace perfetto::trace_processor
