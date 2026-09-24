// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"

#include <memory>

#include <benchmark/benchmark.h>

#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/sqlite/sql_source.h"

namespace perfetto::trace_processor {
namespace {

// Pure dispatch overhead through PerfettoSqlConnection::Execute().
// Each iteration pushes one kRoot frame on |execution_stack_| and pops it.
// This is the workload the (removed) Execute() fast path was optimizing.
void BM_Connection_Execute_TrivialSelect(benchmark::State& state) {
  StringPool pool;
  auto conn = PerfettoSqlConnection::CreateConnectionToNewDatabase(
      &pool, /*enable_extra_checks=*/false);
  for (auto _ : state) {
    auto res = conn->Execute(SqlSource::FromExecuteQuery("SELECT 1"));
    PERFETTO_CHECK(res.ok());
  }
}
BENCHMARK(BM_Connection_Execute_TrivialSelect);

// Re-entrant Execute(): the outer body uses a CREATE PERFETTO FUNCTION
// whose body re-enters the engine via the runtime function machinery.
// Exercises |execution_stack_| at depth > 1 — the case where SmallVector
// inline storage is exceeded and a heap allocation kicks in.
void BM_Connection_Execute_NestedFunction(benchmark::State& state) {
  StringPool pool;
  auto conn = PerfettoSqlConnection::CreateConnectionToNewDatabase(
      &pool, /*enable_extra_checks=*/false);
  auto setup = conn->Execute(SqlSource::FromExecuteQuery(
      "CREATE PERFETTO FUNCTION inner_one() RETURNS INT AS SELECT 1"));
  PERFETTO_CHECK(setup.ok());
  for (auto _ : state) {
    auto res = conn->Execute(SqlSource::FromExecuteQuery("SELECT inner_one()"));
    PERFETTO_CHECK(res.ok());
  }
}
BENCHMARK(BM_Connection_Execute_NestedFunction);

// Small statement that goes through the PerfettoSQL-extension path rather
// than the vanilla-SQLite path: catches regressions in the non-fast-path
// code. (CREATE PERFETTO TABLE is a no-op-ish after the first call when
// OR REPLACE is used.)
void BM_Connection_Execute_PerfettoSqlExtension(benchmark::State& state) {
  StringPool pool;
  auto conn = PerfettoSqlConnection::CreateConnectionToNewDatabase(
      &pool, /*enable_extra_checks=*/false);
  for (auto _ : state) {
    auto res = conn->Execute(SqlSource::FromExecuteQuery(
        "CREATE OR REPLACE PERFETTO TABLE t AS SELECT 1 AS x"));
    PERFETTO_CHECK(res.ok());
  }
}
BENCHMARK(BM_Connection_Execute_PerfettoSqlExtension);

}  // namespace
}  // namespace perfetto::trace_processor
