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

#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"

#include <sqlite3.h>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/dataframe/cursor_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"

namespace perfetto::trace_processor {

namespace {

std::optional<dataframe::Op> SqliteOpToDataframeOp(int op) {
  switch (op) {
    case SQLITE_INDEX_CONSTRAINT_EQ:
      return dataframe::Eq();
    case SQLITE_INDEX_CONSTRAINT_NE:
      return dataframe::Ne();
    case SQLITE_INDEX_CONSTRAINT_LT:
      return dataframe::Lt();
    case SQLITE_INDEX_CONSTRAINT_LE:
      return dataframe::Le();
    case SQLITE_INDEX_CONSTRAINT_GT:
      return dataframe::Gt();
    case SQLITE_INDEX_CONSTRAINT_GE:
      return dataframe::Ge();
    case SQLITE_INDEX_CONSTRAINT_GLOB:
      return dataframe::Glob();
    case SQLITE_INDEX_CONSTRAINT_ISNULL:
      return dataframe::IsNull();
    case SQLITE_INDEX_CONSTRAINT_ISNOTNULL:
      return dataframe::IsNotNull();
    default:
      return std::nullopt;
  }
}

std::string OpToString(int op) {
  switch (op) {
    case SQLITE_INDEX_CONSTRAINT_EQ:
      return "=";
    case SQLITE_INDEX_CONSTRAINT_NE:
      return "!=";
    case SQLITE_INDEX_CONSTRAINT_GE:
      return ">=";
    case SQLITE_INDEX_CONSTRAINT_GT:
      return ">";
    case SQLITE_INDEX_CONSTRAINT_LE:
      return "<=";
    case SQLITE_INDEX_CONSTRAINT_LT:
      return "<";
    case SQLITE_INDEX_CONSTRAINT_MATCH:
      return " match ";
    case SQLITE_INDEX_CONSTRAINT_LIKE:
      return " like ";
    case SQLITE_INDEX_CONSTRAINT_GLOB:
      return " glob ";
    case SQLITE_INDEX_CONSTRAINT_REGEXP:
      return " regexp ";
    case SQLITE_INDEX_CONSTRAINT_ISNULL:
      return " is null";
    case SQLITE_INDEX_CONSTRAINT_ISNOTNULL:
      return " is not null";
    case SQLITE_INDEX_CONSTRAINT_LIMIT:
      return "limit";
    case SQLITE_INDEX_CONSTRAINT_OFFSET:
      return "offset";
    case SQLITE_INDEX_CONSTRAINT_FUNCTION:
      return "function";
    default:
      return "unknown";
  }
}

std::string ToSqliteCreateTableType(dataframe::StorageType type) {
  switch (type.index()) {
    case dataframe::StorageType::GetTypeIndex<dataframe::Id>():
    case dataframe::StorageType::GetTypeIndex<dataframe::Uint32>():
    case dataframe::StorageType::GetTypeIndex<dataframe::Int32>():
    case dataframe::StorageType::GetTypeIndex<dataframe::Int64>():
      return "INTEGER";
    case dataframe::StorageType::GetTypeIndex<dataframe::Double>():
      return "DOUBLE";
    case dataframe::StorageType::GetTypeIndex<dataframe::String>():
      return "TEXT";
    default:
      PERFETTO_FATAL("Unimplemented");
  }
}

std::string CreateTableStmt(const dataframe::DataframeSpec& spec) {
  std::string id;
  std::string create_stmt = "CREATE TABLE x(";
  for (uint32_t i = 0; i < spec.column_specs.size(); ++i) {
    create_stmt += spec.column_names[i] + " " +
                   ToSqliteCreateTableType(spec.column_specs[i].type);
    if (spec.column_names[i] == "_auto_id") {
      create_stmt += " HIDDEN";
    }
    if (spec.column_names[i] == "id" || spec.column_names[i] == "_auto_id") {
      id = spec.column_names[i];
    }
    create_stmt += ", ";
  }
  create_stmt += "PRIMARY KEY(" + id + ")) WITHOUT ROWID";
  return create_stmt;
}

}  // namespace

int DataframeModule::Create(sqlite3* db,
                            void* raw_ctx,
                            int argc,
                            const char* const* argv,
                            sqlite3_vtab** vtab,
                            char** err) {
  PERFETTO_CHECK(argc == 3);

  auto* ctx = GetContext(raw_ctx);
  auto state = std::move(ctx->temporary_create_state);
  PERFETTO_CHECK(state);

  std::string create_stmt = CreateTableStmt(state->dataframe->CreateSpec());
  if (int r = sqlite3_declare_vtab(db, create_stmt.c_str()); r != SQLITE_OK) {
    *err = sqlite3_mprintf("failed to declare vtab %s", create_stmt.c_str());
    return r;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = ctx->OnCreate(argc, argv, std::move(state));
  res->name = argv[2];
  *vtab = res.release();
  return SQLITE_OK;
}

int DataframeModule::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> v(GetVtab(vtab));
  sqlite::ModuleStateManager<DataframeModule>::OnDestroy(v->state);
  return SQLITE_OK;
}

int DataframeModule::Connect(sqlite3* db,
                             void* raw_ctx,
                             int argc,
                             const char* const* argv,
                             sqlite3_vtab** vtab,
                             char**) {
  PERFETTO_CHECK(argc == 3);

  auto* vtab_state = GetContext(raw_ctx)->OnConnect(argc, argv);
  auto* state =
      sqlite::ModuleStateManager<DataframeModule>::GetState(vtab_state);
  std::string create_stmt = CreateTableStmt(state->dataframe->CreateSpec());
  if (int r = sqlite3_declare_vtab(db, create_stmt.c_str()); r != SQLITE_OK) {
    return r;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = vtab_state;
  res->name = argv[2];
  *vtab = res.release();
  return SQLITE_OK;
}

int DataframeModule::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> v(GetVtab(vtab));
  return SQLITE_OK;
}

int DataframeModule::BestIndex(sqlite3_vtab* tab, sqlite3_index_info* info) {
  auto* v = GetVtab(tab);
  auto* s = sqlite::ModuleStateManager<DataframeModule>::GetState(v->state);

  std::optional<int> limit_constraint_idx;
  std::optional<int> offset_constraint_idx;

  std::vector<dataframe::FilterSpec> filter_specs;
  dataframe::LimitSpec limit_spec;
  filter_specs.reserve(static_cast<size_t>(info->nConstraint));
  bool has_unknown_constraint = false;
  for (int i = 0; i < info->nConstraint; ++i) {
    if (!info->aConstraint[i].usable) {
      continue;
    }
    sqlite3_value* rhs;
    int ret = sqlite3_vtab_rhs_value(info, i, &rhs);
    PERFETTO_CHECK(ret == SQLITE_OK || ret == SQLITE_NOTFOUND);

    int op = info->aConstraint[i].op;

    // Special handling for limit/offset values when we have a constant value.
    bool is_limit_offset = op == SQLITE_INDEX_CONSTRAINT_LIMIT ||
                           op == SQLITE_INDEX_CONSTRAINT_OFFSET;
    if (is_limit_offset && rhs &&
        sqlite::value::Type(rhs) == sqlite::Type::kInteger) {
      int64_t value = sqlite::value::Int64(rhs);
      if (value >= 0 && value <= std::numeric_limits<uint32_t>::max()) {
        auto cast = static_cast<uint32_t>(value);
        if (op == SQLITE_INDEX_CONSTRAINT_LIMIT) {
          limit_spec.limit = cast;
          limit_constraint_idx = i;
        } else {
          PERFETTO_DCHECK(op == SQLITE_INDEX_CONSTRAINT_OFFSET);
          limit_spec.offset = cast;
          offset_constraint_idx = i;
        }
        continue;
      }
    }
    auto df_op = SqliteOpToDataframeOp(op);
    if (!df_op) {
      has_unknown_constraint = true;
      continue;
    }
    // Convert the eq constraint to a Is constraint if possible.
    if (df_op->Is<dataframe::Eq>() && sqlite3_vtab_in(info, i, -1)) {
      df_op = dataframe::In();
      PERFETTO_CHECK(sqlite3_vtab_in(info, i, 1));
    }
    filter_specs.emplace_back(dataframe::FilterSpec{
        static_cast<uint32_t>(info->aConstraint[i].iColumn),
        static_cast<uint32_t>(i),
        *df_op,
        std::nullopt,
    });
  }

  // If we have a constraint we don't understand, we should ignore the limit
  // and offset constraints.
  if (has_unknown_constraint) {
    limit_spec = dataframe::LimitSpec{};
    limit_constraint_idx = std::nullopt;
    offset_constraint_idx = std::nullopt;
  }

  bool should_sort_using_order_by = true;
  std::vector<dataframe::DistinctSpec> distinct_specs;
  if (info->nOrderBy > 0) {
    int vtab_distinct = sqlite3_vtab_distinct(info);
    switch (vtab_distinct) {
      case 0: /* normal sorting */
      // TODO(lalitm): add special handling for group by.
      case 1: /* group by */
        break;
      case 2: /* distinct */
      case 3: /* distinct + order by */ {
        uint64_t cols_used_it = info->colUsed;
        for (uint32_t i = 0; i < 64; ++i) {
          if (cols_used_it & 1u) {
            distinct_specs.push_back(dataframe::DistinctSpec{i});
          }
          cols_used_it >>= 1;
        }
        should_sort_using_order_by = (vtab_distinct == 3);
        break;
      }
      default:
        PERFETTO_FATAL("Unreachable");
    }
  }

  std::vector<dataframe::SortSpec> sort_specs;
  if (should_sort_using_order_by) {
    sort_specs.reserve(static_cast<size_t>(info->nOrderBy));
    for (int i = 0; i < info->nOrderBy; ++i) {
      sort_specs.emplace_back(dataframe::SortSpec{
          static_cast<uint32_t>(info->aOrderBy[i].iColumn),
          info->aOrderBy[i].desc ? dataframe::SortDirection::kDescending
                                 : dataframe::SortDirection::kAscending});
    }
  }
  info->orderByConsumed = true;

  SQLITE_ASSIGN_OR_RETURN(
      tab, auto plan,
      s->dataframe->PlanQuery(filter_specs, distinct_specs, sort_specs,
                              limit_spec, info->colUsed));
  int max_argv = 0;
  for (const auto& c : filter_specs) {
    if (auto value_index = c.value_index; value_index) {
      info->aConstraintUsage[c.source_index].argvIndex =
          static_cast<int>(*value_index) + 1;
      info->aConstraintUsage[c.source_index].omit = true;
      max_argv =
          std::max(max_argv, info->aConstraintUsage[c.source_index].argvIndex);
    }
  }
  if (limit_constraint_idx) {
    info->aConstraintUsage[*limit_constraint_idx].omit = true;
    info->aConstraintUsage[*limit_constraint_idx].argvIndex = ++max_argv;
  }
  if (offset_constraint_idx) {
    info->aConstraintUsage[*offset_constraint_idx].omit = true;
    info->aConstraintUsage[*offset_constraint_idx].argvIndex = ++max_argv;
  }
  info->needToFreeIdxStr = true;
  info->estimatedCost = plan.estimated_cost();
  info->estimatedRows = plan.estimated_row_count();
  if (plan.max_row_count() <= 1) {
    info->idxFlags |= SQLITE_INDEX_SCAN_UNIQUE;
  }
  info->idxNum = v->best_idx_num++;
  PERFETTO_TP_TRACE(
      metatrace::Category::QUERY_TIMELINE, "DATAFRAME_BEST_INDEX",
      [info, v, s, &plan](metatrace::Record* record) {
        base::StackString<32> unique("%d",
                                     info->idxFlags & SQLITE_INDEX_SCAN_UNIQUE);
        record->AddArg("name", v->name);
        record->AddArg("unique", unique.string_view());
        record->AddArg("idxNum",
                       base::StackString<32>("%d", info->idxNum).string_view());
        record->AddArg(
            "estimatedCost",
            base::StackString<32>("%f", info->estimatedCost).string_view());
        record->AddArg(
            "estimatedRows",
            base::StackString<32>("%lld", info->estimatedRows).string_view());
        record->AddArg(
            "orderByConsumed",
            base::StackString<32>("%d", info->orderByConsumed).string_view());
        for (uint64_t u = info->colUsed, i = 0, j = 0; u != 0; u >>= 1, ++i) {
          if (u & 1) {
            base::StackString<32> c("colUsed[%" PRIu64 "]", j++);
            record->AddArg(c.string_view(), s->dataframe->column_names()[i]);
          }
        }
        auto str = plan.BytecodeToString();
        for (uint32_t i = 0; i < str.size(); ++i) {
          base::StackString<32> c("bytecode[%u]", i);
          record->AddArg(c.string_view(), str[i]);
        }
        for (int i = 0, j = 0; i < info->nConstraint; ++i) {
          if (!info->aConstraint[i].usable) {
            continue;
          }
          {
            base::StackString<32> c("constraint[%d].column", j);
            auto col_idx = static_cast<uint32_t>(info->aConstraint[i].iColumn);
            record->AddArg(c.string_view(),
                           s->dataframe->column_names()[col_idx]);
          }
          {
            base::StackString<32> c("constraint[%d].op", j);
            record->AddArg(c.string_view(),
                           OpToString(info->aConstraint[i].op));
          }
          {
            base::StackString<32> c("constraint[%d].argvIndex", j);
            record->AddArg(c.string_view(),
                           std::to_string(info->aConstraintUsage[i].argvIndex));
          }
          {
            base::StackString<32> c("constraint[%d].omit", j);
            record->AddArg(c.string_view(),
                           std::to_string(info->aConstraintUsage[i].omit));
          }
          {
            base::StackString<32> c("constraint[%d].in", j);
            record->AddArg(c.string_view(),
                           std::to_string(sqlite3_vtab_in(info, i, -1)));
          }
          j++;
        }
        for (int i = 0; i < info->nOrderBy; ++i) {
          {
            base::StackString<32> c("order_by[%d].column", i);
            auto col_idx = static_cast<uint32_t>(info->aOrderBy[i].iColumn);
            record->AddArg(c.string_view(),
                           s->dataframe->column_names()[col_idx]);
          }
          {
            base::StackString<32> c("order_by[%d].desc", i);
            record->AddArg(c.string_view(),
                           std::to_string(info->aOrderBy[i].desc));
          }
        }
      });
  info->idxStr = sqlite3_mprintf("%s", std::move(plan).Serialize().data());
  return SQLITE_OK;
}

int DataframeModule::Open(sqlite3_vtab*, sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  *cursor = c.release();
  return SQLITE_OK;
}

int DataframeModule::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  return SQLITE_OK;
}

int DataframeModule::Filter(sqlite3_vtab_cursor* cur,
                            int idxNum,
                            const char* idxStr,
                            int argc,
                            sqlite3_value** argv) {
  auto* c = GetCursor(cur);
  if (idxStr != c->last_idx_str) {
    auto plan = dataframe::Dataframe::QueryPlan::Deserialize(idxStr);
    PERFETTO_TP_TRACE(
        metatrace::Category::QUERY_DETAILED, "DATAFRAME_FILTER_PREPARE",
        [&plan, idxNum](metatrace::Record* record) {
          record->AddArg("idxNum",
                         base::StackString<32>("%d", idxNum).string_view());
          auto str = plan.BytecodeToString();
          for (uint32_t i = 0; i < str.size(); ++i) {
            base::StackString<32> c("bytecode[%u]", i);
            record->AddArg(c.string_view(), str[i]);
          }
        });
    auto* v = GetVtab(cur->pVtab);
    auto* s = sqlite::ModuleStateManager<DataframeModule>::GetState(v->state);
    s->dataframe->PrepareCursor(plan, c->df_cursor);
    c->last_idx_str = idxStr;
  }
  // SQLite's API claims it will never pass more than 16 arguments
  // so assert that here as our std::array is fixed size.
  PERFETTO_DCHECK(argc <= 16);
  SqliteValueFetcher fetcher{{}, {}, argv};
  memcpy(static_cast<void*>(fetcher.sqlite_value.data()),
         static_cast<void*>(argv),
         sizeof(sqlite3_value*) * static_cast<size_t>(argc));
  c->df_cursor.Execute(fetcher);
  return SQLITE_OK;
}

int DataframeModule::Next(sqlite3_vtab_cursor* cur) {
  GetCursor(cur)->df_cursor.Next();
  return SQLITE_OK;
}

int DataframeModule::Eof(sqlite3_vtab_cursor* cur) {
  return GetCursor(cur)->df_cursor.Eof();
}

int DataframeModule::Column(sqlite3_vtab_cursor* cur,
                            sqlite3_context* ctx,
                            int raw_n) {
  SqliteResultCallback visitor{{}, ctx};
  GetCursor(cur)->df_cursor.Cell(static_cast<uint32_t>(raw_n), visitor);
  return SQLITE_OK;
}

int DataframeModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
