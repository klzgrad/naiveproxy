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

#include "src/trace_processor/perfetto_sql/engine/created_function.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <stack>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/scoped_db.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_engine.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/util/sql_argument.h"

namespace perfetto::trace_processor {

namespace {

void ReturnSqlValue(sqlite3_context* ctx, const SqlValue& value) {
  switch (value.type) {
    case SqlValue::Type::kNull:
      sqlite::utils::ReturnNullFromFunction(ctx);
      break;
    case SqlValue::Type::kLong:
      sqlite::result::Long(ctx, value.long_value);
      break;
    case SqlValue::Type::kDouble:
      sqlite::result::Double(ctx, value.double_value);
      break;
    case SqlValue::Type::kString:
      sqlite::result::RawString(ctx, value.string_value, -1,
                                sqlite::result::kSqliteTransient);
      break;
    case SqlValue::Type::kBytes:
      sqlite::result::RawBytes(ctx, value.bytes_value,
                               static_cast<int>(value.bytes_count),
                               sqlite::result::kSqliteTransient);
      break;
  }
}

base::Status CheckNoMoreRows(sqlite3_stmt* stmt,
                             sqlite3* db,
                             const FunctionPrototype& prototype) {
  int ret = sqlite3_step(stmt);
  RETURN_IF_ERROR(SqliteRetToStatus(db, prototype.function_name, ret));
  if (ret == SQLITE_ROW) {
    auto expanded_sql = ScopedSqliteString(sqlite3_expanded_sql(stmt));
    return base::ErrStatus(
        "%s: multiple values were returned when executing function body. "
        "Executed SQL was %s",
        prototype.function_name.c_str(), expanded_sql.get());
  }
  PERFETTO_DCHECK(ret == SQLITE_DONE);
  return base::OkStatus();
}

// Note: if the returned type is string / bytes, it will be invalidated by the
// next call to SQLite, so the caller must take care to either copy or use the
// value before calling SQLite again.
base::StatusOr<SqlValue> EvaluateScalarStatement(
    sqlite3_stmt* stmt,
    sqlite3* db,
    const FunctionPrototype& prototype) {
  int ret = sqlite3_step(stmt);
  RETURN_IF_ERROR(SqliteRetToStatus(db, prototype.function_name, ret));
  if (ret == SQLITE_DONE) {
    // No return value means we just return don't set |out|.
    return SqlValue();
  }

  PERFETTO_DCHECK(ret == SQLITE_ROW);
  size_t col_count = static_cast<size_t>(sqlite3_column_count(stmt));
  if (col_count != 1) {
    return base::ErrStatus(
        "%s: SQL definition should only return one column: returned %zu "
        "columns",
        prototype.function_name.c_str(), col_count);
  }

  SqlValue result =
      sqlite::utils::SqliteValueToSqlValue(sqlite3_column_value(stmt, 0));

  // If we return a bytes type but have a null pointer, SQLite will convert this
  // to an SQL null. However, for proto build functions, we actively want to
  // distinguish between nulls and 0 byte strings. Therefore, change the value
  // to an empty string.
  if (result.type == SqlValue::kBytes && result.bytes_value == nullptr) {
    PERFETTO_DCHECK(result.bytes_count == 0);
    result.bytes_value = "";
  }

  return result;
}

base::Status BindArguments(sqlite3_stmt* stmt,
                           const FunctionPrototype& prototype,
                           size_t argc,
                           sqlite3_value** argv) {
  // Bind all the arguments to the appropriate places in the function.
  for (size_t i = 0; i < argc; ++i) {
    RETURN_IF_ERROR(MaybeBindArgument(stmt, prototype.function_name,
                                      prototype.arguments[i], argv[i]));
  }
  return base::OkStatus();
}

struct StoredSqlValue {
  // unique_ptr to ensure that the pointers to these values are long-lived.
  using OwnedString = std::unique_ptr<std::string>;
  using OwnedBytes = std::unique_ptr<std::vector<uint8_t>>;
  // variant is a pain to use, but it's the simplest way to ensure that
  // the destructors run correctly for non-trivial members of the
  // union.
  using Data =
      std::variant<int64_t, double, OwnedString, OwnedBytes, std::nullptr_t>;

  explicit StoredSqlValue(SqlValue value) {
    switch (value.type) {
      case SqlValue::Type::kNull:
        data = nullptr;
        break;
      case SqlValue::Type::kLong:
        data = value.long_value;
        break;
      case SqlValue::Type::kDouble:
        data = value.double_value;
        break;
      case SqlValue::Type::kString:
        data = std::make_unique<std::string>(value.string_value);
        break;
      case SqlValue::Type::kBytes:
        const auto* ptr = static_cast<const uint8_t*>(value.bytes_value);
        data = std::make_unique<std::vector<uint8_t>>(ptr,
                                                      ptr + value.bytes_count);
        break;
    }
  }

  SqlValue AsSqlValue() {
    switch (data.index()) {
      case base::variant_index<Data, std::nullptr_t>():
        return {};
      case base::variant_index<Data, int64_t>():
        return SqlValue::Long(base::unchecked_get<int64_t>(data));
      case base::variant_index<Data, double>():
        return SqlValue::Double(base::unchecked_get<double>(data));
      case base::variant_index<Data, OwnedString>(): {
        const auto& str_ptr = base::unchecked_get<OwnedString>(data);
        return SqlValue::String(str_ptr->c_str());
      }
      case base::variant_index<Data, OwnedBytes>(): {
        const auto& bytes_ptr = base::unchecked_get<OwnedBytes>(data);
        return SqlValue::Bytes(bytes_ptr->data(), bytes_ptr->size());
      }
    }
    // GCC doesn't realize that the switch is exhaustive.
    PERFETTO_CHECK(false);
    return SqlValue();
  }

  Data data = nullptr;
};

class Memoizer {
 public:
  // Supported arguments. For now, only functions with a single int argument are
  // supported.
  using MemoizedArgs = int64_t;

  // Enables memoization.
  // Only functions with a single int argument returning ints are supported.
  base::Status EnableMemoization(const FunctionPrototype& prototype) {
    if (prototype.arguments.size() != 1 ||
        TypeToSqlValueType(prototype.arguments[0].type()) !=
            SqlValue::Type::kLong) {
      return base::ErrStatus(
          "EXPERIMENTAL_MEMOIZE: Function %s should take one int argument",
          prototype.function_name.c_str());
    }
    enabled_ = true;
    return base::OkStatus();
  }

  // Returns the memoized value for the current invocation if it exists.
  std::optional<SqlValue> GetMemoizedValue(MemoizedArgs args) {
    if (!enabled_) {
      return std::nullopt;
    }
    StoredSqlValue* value = memoized_values_.Find(args);
    if (!value) {
      return std::nullopt;
    }
    return value->AsSqlValue();
  }

  bool HasMemoizedValue(MemoizedArgs args) {
    return GetMemoizedValue(args).has_value();
  }

  // Saves the return value of the current invocation for memoization.
  void Memoize(MemoizedArgs args, SqlValue value) {
    if (!enabled_) {
      return;
    }
    memoized_values_.Insert(args, StoredSqlValue(value));
  }

  // Checks that the function has a single int argument and returns it.
  static std::optional<MemoizedArgs> AsMemoizedArgs(size_t argc,
                                                    sqlite3_value** argv) {
    if (argc != 1) {
      return std::nullopt;
    }
    SqlValue arg = sqlite::utils::SqliteValueToSqlValue(argv[0]);
    if (arg.type != SqlValue::Type::kLong) {
      return std::nullopt;
    }
    return arg.AsLong();
  }

  bool enabled() const { return enabled_; }

 private:
  bool enabled_ = false;
  base::FlatHashMap<MemoizedArgs, StoredSqlValue> memoized_values_;
};

// A helper to unroll recursive calls: to minimise the amount of stack space
// used, memoized recursive calls are evaluated using an on-heap queue.
//
// We compute the function in two passes:
// - In the first pass, we evaluate the statement to discover which recursive
//   calls it makes, returning null from recursive calls and ignoring the
//   result.
// - In the second pass, we evaluate the statement again, but this time we
//   memoize the result of each recursive call.
//
// We maintain a queue for scheduled "first pass" calls and a stack for the
// scheduled "second pass" calls, evaluating available first pass calls, then
// second pass calls. When we evaluate a first pass call, the further calls to
// CreatedFunction::Run will just add it to the "first pass" queue. The second
// pass, however, will evaluate the function normally, typically just using the
// memoized result for the dependent calls. However, if the recursive calls
// depend on the return value of the function, we will proceed with normal
// recursion.
//
// To make it more concrete, consider an following example.
// We have a function computing factorial (f) and we want to compute f(3).
//
// SELECT create_function('f(x INT)', 'INT',
// 'SELECT IIF($x = 0, 1, $x * f($x - 1))');
// SELECT experimental_memoize('f');
// SELECT f(3);
//
// - We start with a call to f(3). It executes the statement as normal, which
//   recursively calls f(2).
// - When f(2) is called, we detect that it is a recursive call and we start
//   unrolling it, entering RecursiveCallUnroller::Run.
// - We schedule first pass for 2 and the state of the unroller
//   is first_pass: [2], second_pass: [].
// - Then we compute the first pass for f(2). It calls f(1), which is ignored
//   due to OnFunctionCall returning kIgnoreDueToFirstPass and 1 is added to the
//   first pass queue. 2 is taked out of the first pass queue and moved to the
//   second pass stack. State: first_pass: [1], second_pass: [2].
// - Then we compute the first pass for 1. The similar thing happens: f(0) is
//   called and ignored, 0 is added to first_pass, 1 is added to second_pass.
//   State: first_pass: [0], second_pass: [2, 1].
// - Then we compute the first pass for 0. It doesn't make further calls, so
//   0 is moved to the second pass stack.
//   State: first_pass: [], second_pass: [2, 1, 0].
// - Then we compute the second pass for 0. It just returns 1.
//   State: first_pass: [], second_pass: [2, 1], results: {0: 1}.
// - Then we compute the second pass for 1. It calls f(0), which is memoized.
//   State: first_pass: [], second_pass: [2], results: {0: 1, 1: 1}.
// - Then we compute the second pass for 1. It calls f(1), which is memoized.
//   State: first_pass: [], second_pass: [], results: {0: 1, 1: 1, 2: 2}.
// - As both first_pass and second_pass are empty, we return from
//   RecursiveCallUnroller::Run.
// - Control is returned to CreatedFunction::Run for f(2), which returns
//   memoized value.
// - Then control is returned to CreatedFunction::Run for f(3), which completes
//   the computation.
class RecursiveCallUnroller {
 public:
  RecursiveCallUnroller(PerfettoSqlEngine* engine,
                        sqlite3_stmt* stmt,
                        const FunctionPrototype& prototype,
                        Memoizer& memoizer)
      : engine_(engine),
        stmt_(stmt),
        prototype_(prototype),
        memoizer_(memoizer) {}

  // Whether we should just return null due to us being in the "first pass".
  enum class FunctionCallState : uint8_t {
    kIgnoreDueToFirstPass,
    kEvaluate,
  };

  base::StatusOr<FunctionCallState> OnFunctionCall(
      Memoizer::MemoizedArgs args) {
    // If we are in the second pass, we just continue the function execution,
    // including checking if a memoized value is available and returning it.
    //
    // We generally expect a memoized value to be available, but there are
    // cases when it might not be the case, e.g. when which recursive calls are
    // made depends on the return value of the function, e.g. for the following
    // function, the first pass will not detect f(y) calls, so they will
    // be computed recursively.
    // f(x): SELECT max(f(y)) FROM y WHERE y < f($x - 1);
    if (state_ == State::kComputingSecondPass) {
      return FunctionCallState::kEvaluate;
    }
    if (!memoizer_.HasMemoizedValue(args)) {
      ArgState* state = visited_.Find(args);
      if (state) {
        // Detect recursive loops, e.g. f(1) calling f(2) calling f(1).
        if (*state == ArgState::kEvaluating) {
          return base::ErrStatus("Infinite recursion detected");
        }
      } else {
        visited_.Insert(args, ArgState::kScheduled);
        first_pass_.push(args);
      }
    }
    return FunctionCallState::kIgnoreDueToFirstPass;
  }

  base::Status Run(Memoizer::MemoizedArgs initial_args) {
    PERFETTO_TP_TRACE(metatrace::Category::FUNCTION_CALL,
                      "UNROLL_RECURSIVE_FUNCTION_CALL",
                      [&](metatrace::Record* r) {
                        r->AddArg("Function", prototype_.function_name);
                        r->AddArg("Arg 0", std::to_string(initial_args));
                      });

    first_pass_.push(initial_args);
    visited_.Insert(initial_args, ArgState::kScheduled);

    while (!first_pass_.empty() || !second_pass_.empty()) {
      // If we have scheduled first pass calls, we evaluate them first.
      if (!first_pass_.empty()) {
        state_ = State::kComputingFirstPass;
        Memoizer::MemoizedArgs args = first_pass_.front();

        PERFETTO_TP_TRACE(metatrace::Category::FUNCTION_CALL,
                          "SQL_FUNCTION_CALL", [&](metatrace::Record* r) {
                            r->AddArg("Function", prototype_.function_name);
                            r->AddArg("Type", "UnrollRecursiveCall_FirstPass");
                            r->AddArg("Arg 0", std::to_string(args));
                          });

        first_pass_.pop();
        second_pass_.push(args);
        Evaluate(args).status();
        continue;
      }

      state_ = State::kComputingSecondPass;
      Memoizer::MemoizedArgs args = second_pass_.top();

      PERFETTO_TP_TRACE(metatrace::Category::FUNCTION_CALL, "SQL_FUNCTION_CALL",
                        [&](metatrace::Record* r) {
                          r->AddArg("Function", prototype_.function_name);
                          r->AddArg("Type", "UnrollRecursiveCall_SecondPass");
                          r->AddArg("Arg 0", std::to_string(args));
                        });

      visited_.Insert(args, ArgState::kEvaluating);
      second_pass_.pop();
      base::StatusOr<std::optional<int64_t>> result = Evaluate(args);
      RETURN_IF_ERROR(result.status());
      std::optional<int64_t> maybe_int_result = result.value();
      if (!maybe_int_result.has_value()) {
        continue;
      }
      visited_.Insert(args, ArgState::kEvaluated);
      memoizer_.Memoize(args, SqlValue::Long(*maybe_int_result));
    }
    return base::OkStatus();
  }

 private:
  // This function returns:
  // - base::ErrStatus if the evaluation of the function failed.
  // - std::nullopt if the function returned a non-integer value.
  // - the result of the function otherwise.
  base::StatusOr<std::optional<int64_t>> Evaluate(Memoizer::MemoizedArgs args) {
    RETURN_IF_ERROR(MaybeBindIntArgument(stmt_, prototype_.function_name,
                                         prototype_.arguments[0], args));
    base::StatusOr<SqlValue> result = EvaluateScalarStatement(
        stmt_, engine_->sqlite_engine()->db(), prototype_);
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    RETURN_IF_ERROR(result.status());
    if (result->type != SqlValue::Type::kLong) {
      return std::optional<int64_t>(std::nullopt);
    }
    return std::optional<int64_t>(result->long_value);
  }

  PerfettoSqlEngine* engine_;
  sqlite3_stmt* stmt_;
  const FunctionPrototype& prototype_;
  Memoizer& memoizer_;

  // Current state of the evaluation.
  enum class State : uint8_t {
    kComputingFirstPass,
    kComputingSecondPass,
  };
  State state_ = State::kComputingFirstPass;

  // A state of evaluation of a given argument.
  enum class ArgState : uint8_t {
    kScheduled,
    kEvaluating,
    kEvaluated,
  };

  // See the class-level comment for the explanation of the two passes.
  std::queue<Memoizer::MemoizedArgs> first_pass_;
  base::FlatHashMap<Memoizer::MemoizedArgs, ArgState> visited_;
  std::stack<Memoizer::MemoizedArgs> second_pass_;
};

}  // namespace

// This class is used to store the state of a CREATE_FUNCTION call.
// It is used to store the state of the function across multiple invocations
// of the function (e.g. when the function is called recursively).
class State : public CreatedFunction::UserData {
 public:
  explicit State(PerfettoSqlEngine* engine) : engine_(engine) {}
  ~State() override;

  // Prepare a statement and push it into the stack of allocated statements
  // for this function.
  base::Status PrepareStatement() {
    SqliteEngine::PreparedStatement stmt =
        engine_->sqlite_engine()->PrepareStatement(*sql_);
    RETURN_IF_ERROR(stmt.status());
    is_valid_ = true;
    stmts_.push_back(std::move(stmt));
    return base::OkStatus();
  }

  // Sets the state of the function. Should be called only when the function
  // is invalid (i.e. when it is first created or when the previous statement
  // failed to prepare).
  void Reset(FunctionPrototype prototype,
             sql_argument::Type return_type,
             SqlSource sql) {
    // Re-registration of valid functions is not allowed.
    PERFETTO_DCHECK(!is_valid_);
    PERFETTO_DCHECK(stmts_.empty());

    prototype_ = std::move(prototype);
    return_type_ = return_type;
    sql_ = std::move(sql);
  }

  // This function is called each time the function is called.
  // It ensures that we have a statement for the current recursion level,
  // allocating a new one if needed.
  base::Status PushStackEntry() {
    ++current_recursion_level_;
    if (current_recursion_level_ > stmts_.size()) {
      return PrepareStatement();
    }
    return base::OkStatus();
  }

  // Returns the statement that is used for the current invocation.
  sqlite3_stmt* CurrentStatement() {
    return stmts_[current_recursion_level_ - 1].sqlite_stmt();
  }

  // This function is called each time the function returns and resets the
  // statement that this invocation used.
  void PopStackEntry() {
    if (current_recursion_level_ > stmts_.size()) {
      // This is possible if we didn't prepare the statement and returned
      // an error.
      return;
    }
    sqlite3_reset(CurrentStatement());
    sqlite3_clear_bindings(CurrentStatement());
    --current_recursion_level_;
  }

  base::StatusOr<RecursiveCallUnroller::FunctionCallState> OnFunctionCall(
      Memoizer::MemoizedArgs args) {
    if (!recursive_call_unroller_) {
      return RecursiveCallUnroller::FunctionCallState::kEvaluate;
    }
    return recursive_call_unroller_->OnFunctionCall(args);
  }

  // Called before checking the function for memoization.
  base::Status UnrollRecursiveCallIfNeeded(Memoizer::MemoizedArgs args) {
    if (!memoizer_.enabled() || !is_in_recursive_call() ||
        recursive_call_unroller_) {
      return base::OkStatus();
    }
    // If we are in a recursive call, we need to check if we have already
    // computed the result for the current arguments.
    if (memoizer_.HasMemoizedValue(args)) {
      return base::OkStatus();
    }

    // If we are in a beginning of a function call:
    // - is a recursive,
    // - can be memoized,
    // - hasn't been memoized already, and
    // - hasn't start unrolling yet;
    // start the unrolling and run the unrolling loop.
    recursive_call_unroller_ = std::make_unique<RecursiveCallUnroller>(
        engine_, CurrentStatement(), prototype_, memoizer_);
    auto status = recursive_call_unroller_->Run(args);
    recursive_call_unroller_.reset();
    return status;
  }

  // Schedule a statement to be validated that it is indeed doesn't have any
  // more rows.
  void ScheduleEmptyStatementValidation(sqlite3_stmt* stmt) {
    empty_stmts_to_validate_.push_back(stmt);
  }

  base::Status ValidateEmptyStatements() {
    while (!empty_stmts_to_validate_.empty()) {
      sqlite3_stmt* stmt = empty_stmts_to_validate_.back();
      empty_stmts_to_validate_.pop_back();
      RETURN_IF_ERROR(
          CheckNoMoreRows(stmt, engine_->sqlite_engine()->db(), prototype_));
    }
    return base::OkStatus();
  }

  bool is_in_recursive_call() const { return current_recursion_level_ > 1; }

  base::Status EnableMemoization() {
    return memoizer_.EnableMemoization(prototype_);
  }

  PerfettoSqlEngine* engine() const { return engine_; }

  const FunctionPrototype& prototype() const { return prototype_; }

  sql_argument::Type return_type() const { return return_type_; }

  const std::string& sql() const { return sql_->sql(); }

  bool is_valid() const { return is_valid_; }

  Memoizer& memoizer() { return memoizer_; }

 private:
  PerfettoSqlEngine* engine_;
  FunctionPrototype prototype_;
  sql_argument::Type return_type_;
  std::optional<SqlSource> sql_;
  // Perfetto SQL functions support recursion. Given that each function call in
  // the stack requires a dedicated statement, we maintain a stack of prepared
  // statements and use the top one for each new call (allocating a new one if
  // needed).
  std::vector<SqliteEngine::PreparedStatement> stmts_;
  // A list of statements to verify to ensure that they don't have more rows
  // in VerifyPostConditions.
  std::vector<sqlite3_stmt*> empty_stmts_to_validate_;
  size_t current_recursion_level_ = 0;
  // Function re-registration is not allowed, but the user is allowed to define
  // the function again if the first call failed. |is_valid_| flag helps that
  // by tracking whether the current function definition is valid (in which case
  // re-registration is not allowed).
  bool is_valid_ = false;
  Memoizer memoizer_;
  // Set if we are in a middle of unrolling a recursive call.
  std::unique_ptr<RecursiveCallUnroller> recursive_call_unroller_;
};

State::~State() = default;

std::unique_ptr<CreatedFunction::UserData> CreatedFunction::MakeContext(
    PerfettoSqlEngine* engine) {
  return std::make_unique<State>(engine);
}

bool CreatedFunction::IsValid(UserData* ctx) {
  return static_cast<State*>(ctx)->is_valid();
}

void CreatedFunction::Reset(UserData* ctx, PerfettoSqlEngine* engine) {
  ctx->~UserData();
  new (ctx) State(engine);
}

void CreatedFunction::Step(sqlite3_context* ctx,
                           int argc,
                           sqlite3_value** argv) {
  auto* state = static_cast<State*>(CreatedFunction::GetUserData(ctx));

  // RAII cleanup to ensure PopStackEntry is called
  struct ScopedCleanup {
    State* state;
    ~ScopedCleanup() { state->PopStackEntry(); }
  };
  ScopedCleanup scoped_cleanup{state};

  // Enter the function and ensure that we have a statement allocated.
  if (auto status = state->PushStackEntry(); !status.ok()) {
    return sqlite::utils::SetError(ctx, status.c_message());
  }

  size_t expected_argc = state->prototype().arguments.size();
  if (static_cast<size_t>(argc) != expected_argc) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus(
                 "%s: invalid number of args; expected %zu, received %d",
                 state->prototype().function_name.c_str(), expected_argc, argc)
                 .c_message());
  }

  // Type check all the arguments.
  for (size_t i = 0; i < expected_argc; ++i) {
    sqlite3_value* arg = argv[i];
    sql_argument::Type type = state->prototype().arguments[i].type();
    base::Status status = sqlite::utils::TypeCheckSqliteValue(
        arg, sql_argument::TypeToSqlValueType(type),
        sql_argument::TypeToHumanFriendlyString(type));
    if (!status.ok()) {
      return sqlite::utils::SetError(
          ctx, base::ErrStatus("%s[arg=%s]: argument %zu %s",
                               state->prototype().function_name.c_str(),
                               sqlite3_value_text(arg), i, status.c_message())
                   .c_message());
    }
  }

  std::optional<Memoizer::MemoizedArgs> memoized_args =
      Memoizer::AsMemoizedArgs(size_t(argc), argv);

  if (memoized_args) {
    // Handle recursive call unrolling
    auto unroll_state = state->OnFunctionCall(*memoized_args);
    if (!unroll_state.ok()) {
      return sqlite::utils::SetError(ctx, unroll_state.status().c_message());
    }
    if (*unroll_state ==
        RecursiveCallUnroller::FunctionCallState::kIgnoreDueToFirstPass) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }

    if (auto status = state->UnrollRecursiveCallIfNeeded(*memoized_args);
        !status.ok()) {
      return sqlite::utils::SetError(ctx, status.c_message());
    }

    // Check for memoized value
    if (auto memoized_value =
            state->memoizer().GetMemoizedValue(*memoized_args)) {
      SqlValue out = *memoized_value;
      ReturnSqlValue(ctx, out);
      return;
    }
  }

  PERFETTO_TP_TRACE(
      metatrace::Category::FUNCTION_CALL, "SQL_FUNCTION_CALL",
      [state, argv](metatrace::Record* r) {
        r->AddArg("Function", state->prototype().function_name.c_str());
        for (uint32_t i = 0; i < state->prototype().arguments.size(); ++i) {
          std::string key = "Arg " + std::to_string(i);
          const char* value =
              reinterpret_cast<const char*>(sqlite3_value_text(argv[i]));
          r->AddArg(base::StringView(key),
                    value ? base::StringView(value) : base::StringView("NULL"));
        }
      });

  // Bind arguments and execute the user's SQL
  if (auto status = BindArguments(state->CurrentStatement(), state->prototype(),
                                  size_t(argc), argv);
      !status.ok()) {
    return sqlite::utils::SetError(ctx, status.c_message());
  }

  auto result = EvaluateScalarStatement(state->CurrentStatement(),
                                        state->engine()->sqlite_engine()->db(),
                                        state->prototype());
  if (!result.ok()) {
    return sqlite::utils::SetError(ctx, result.status().c_message());
  }

  SqlValue out = result.value();
  state->ScheduleEmptyStatementValidation(state->CurrentStatement());

  if (memoized_args) {
    state->memoizer().Memoize(*memoized_args, out);
  }

  // Return the result directly
  ReturnSqlValue(ctx, out);

  // Verify post-conditions
  if (auto verify_status = state->ValidateEmptyStatements();
      !verify_status.ok()) {
    sqlite::utils::SetError(ctx, verify_status.c_message());
  }
}

base::Status CreatedFunction::Prepare(CreatedFunction::UserData* ctx,
                                      FunctionPrototype prototype,
                                      sql_argument::Type return_type,
                                      SqlSource source) {
  State* state = static_cast<State*>(ctx);
  state->Reset(std::move(prototype), return_type, std::move(source));

  // Ideally, we would unregister the function here if the statement prep
  // failed, but SQLite doesn't allow unregistering functions inside active
  // statements. So instead we'll just try to prepare the statement when calling
  // this function, which will return an error.
  return state->PrepareStatement();
}

base::Status CreatedFunction::EnableMemoization(UserData* ctx) {
  return static_cast<State*>(ctx)->EnableMemoization();
}

}  // namespace perfetto::trace_processor
