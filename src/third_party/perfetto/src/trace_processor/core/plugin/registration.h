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

#ifndef SRC_TRACE_PROCESSOR_CORE_PLUGIN_REGISTRATION_H_
#define SRC_TRACE_PROCESSOR_CORE_PLUGIN_REGISTRATION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/core/dataframe/specs.h"

struct sqlite3_module;
struct sqlite3_context;
struct sqlite3_value;

namespace perfetto::trace_processor::core::dataframe {
class Dataframe;
}  // namespace perfetto::trace_processor::core::dataframe

namespace perfetto::trace_processor {

// Registration entry for a scalar SQL function. Built via the
// MakeFunctionRegistration() helper in perfetto_sql_connection.h and consumed
// by PerfettoSqlConnection::Initialize.
struct FunctionRegistration {
  using Step = void(sqlite3_context*, int argc, sqlite3_value** argv);
  using CtxDestructor = void(void*);

  std::string name;
  int argc = 0;
  Step* step = nullptr;
  void* ctx = nullptr;
  CtxDestructor* ctx_destructor = nullptr;
  bool deterministic = true;
};

// Registration entry for a SQL aggregate function.
struct AggregateFunctionRegistration {
  using Step = void(sqlite3_context*, int argc, sqlite3_value** argv);
  using Final = void(sqlite3_context*);
  using CtxDestructor = void(void*);

  std::string name;
  int argc = 0;
  Step* step = nullptr;
  Final* final_fn = nullptr;
  void* ctx = nullptr;
  CtxDestructor* ctx_destructor = nullptr;
  bool deterministic = true;
};

// Registration entry for a SQL window function.
struct WindowFunctionRegistration {
  using Step = void(sqlite3_context*, int argc, sqlite3_value** argv);
  using Inverse = void(sqlite3_context*, int argc, sqlite3_value** argv);
  using Value = void(sqlite3_context*);
  using Final = void(sqlite3_context*);
  using CtxDestructor = void(void*);

  std::string name;
  int argc = 0;
  Step* step = nullptr;
  Inverse* inverse = nullptr;
  Value* value = nullptr;
  Final* final_fn = nullptr;
  void* ctx = nullptr;
  CtxDestructor* ctx_destructor = nullptr;
  bool deterministic = true;
};

// Lightweight struct for plugin dataframe registration.
struct PluginDataframe {
  core::dataframe::Dataframe* dataframe;
  std::string name;

  // Indexes to attach to `dataframe` once it has been finalized. Each
  // entry is the list of column names that form one index.
  std::vector<std::vector<std::string>> indexes;
};

namespace sqlite {
class ModuleStateManagerBase;
}  // namespace sqlite

// Registration entry for a sqlite virtual table module.
struct SqliteModuleRegistration {
  using Destructor = void (*)(void*);

  std::string name;
  const sqlite3_module* module = nullptr;
  void* context = nullptr;
  Destructor destructor = nullptr;
  bool is_state_manager = false;
};

// Helper to create a SqliteModuleRegistration with a non-owning context.
template <typename Module>
SqliteModuleRegistration MakeSqliteModule(std::string name,
                                          typename Module::Context* ctx) {
  SqliteModuleRegistration reg;
  reg.name = std::move(name);
  reg.module = &Module::kModule;
  reg.context = ctx;
  reg.is_state_manager = std::is_base_of_v<sqlite::ModuleStateManagerBase,
                                           typename Module::Context>;
  return reg;
}

// Helper to create a SqliteModuleRegistration with an owning context.
template <typename Module>
SqliteModuleRegistration MakeSqliteModule(
    std::string name,
    std::unique_ptr<typename Module::Context> ctx) {
  SqliteModuleRegistration reg;
  reg.name = std::move(name);
  reg.module = &Module::kModule;
  reg.context = ctx.release();
  reg.destructor = [](void* p) {
    delete static_cast<typename Module::Context*>(p);
  };
  reg.is_state_manager = std::is_base_of_v<sqlite::ModuleStateManagerBase,
                                           typename Module::Context>;
  return reg;
}

// Interface which can be subclassed to allow generation of tables dynamically
// at filter time.
//
// This class is used to implement table-valued functions and other similar
// tables.
class StaticTableFunction {
 public:
  class Cursor {
   public:
    virtual ~Cursor();

    // Executes the table function with the provided arguments.
    //
    // Returns true on success, false on failure.
    virtual bool Run(const std::vector<SqlValue>& arguments) = 0;

    // Returns the dataframe that was generated by the last call to Run.
    const dataframe::Dataframe* dataframe() const { return dataframe_; }

    // Returns the status of the last call to Run.
    const base::Status& status() const { return status_; }

   protected:
    [[nodiscard]] bool OnSuccess(const dataframe::Dataframe* df) {
      dataframe_ = df;
      return true;
    }
    [[nodiscard]] bool OnFailure(base::Status status) {
      status_ = std::move(status);
      return false;
    }
    const dataframe::Dataframe* dataframe_ = nullptr;
    base::Status status_;
  };

  virtual ~StaticTableFunction();

  // Makes a cursor to repeatedly call Run with different arguments.
  virtual std::unique_ptr<Cursor> MakeCursor() = 0;

  // Returns the schema of the table that will be returned by ComputeTable.
  virtual dataframe::DataframeSpec CreateSpec() = 0;

  // Returns the name of the dynamic table.
  // This will be used to register the table with SQLite.
  virtual std::string TableName() = 0;

  // Returns the number of arguments that the table function takes.
  virtual uint32_t GetArgumentCount() const = 0;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_PLUGIN_REGISTRATION_H_
