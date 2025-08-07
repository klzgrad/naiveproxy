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

#include "src/trace_processor/sqlite/db_sqlite_table.h"

#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/row_map.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/runtime_table.h"
#include "src/trace_processor/db/table.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/util/regex.h"

namespace perfetto::trace_processor {
namespace {

std::optional<FilterOp> SqliteOpToFilterOp(int sqlite_op) {
  switch (sqlite_op) {
    case SQLITE_INDEX_CONSTRAINT_EQ:
      return FilterOp::kEq;
    case SQLITE_INDEX_CONSTRAINT_GT:
      return FilterOp::kGt;
    case SQLITE_INDEX_CONSTRAINT_LT:
      return FilterOp::kLt;
    case SQLITE_INDEX_CONSTRAINT_NE:
      return FilterOp::kNe;
    case SQLITE_INDEX_CONSTRAINT_GE:
      return FilterOp::kGe;
    case SQLITE_INDEX_CONSTRAINT_LE:
      return FilterOp::kLe;
    case SQLITE_INDEX_CONSTRAINT_ISNULL:
      return FilterOp::kIsNull;
    case SQLITE_INDEX_CONSTRAINT_ISNOTNULL:
      return FilterOp::kIsNotNull;
    case SQLITE_INDEX_CONSTRAINT_GLOB:
      return FilterOp::kGlob;
    case SQLITE_INDEX_CONSTRAINT_REGEXP:
      if constexpr (regex::IsRegexSupported()) {
        return FilterOp::kRegex;
      }
      return std::nullopt;
    case SQLITE_INDEX_CONSTRAINT_LIKE:
    // TODO(lalitm): start supporting these constraints.
    case SQLITE_INDEX_CONSTRAINT_LIMIT:
    case SQLITE_INDEX_CONSTRAINT_OFFSET:
    case SQLITE_INDEX_CONSTRAINT_IS:
    case SQLITE_INDEX_CONSTRAINT_ISNOT:
      return std::nullopt;
    default:
      PERFETTO_FATAL("Currently unsupported constraint");
  }
}

class SafeStringWriter {
 public:
  void AppendString(const char* s) {
    for (const char* c = s; *c; ++c) {
      buffer_.emplace_back(*c);
    }
  }

  void AppendString(const std::string& s) {
    for (char c : s) {
      buffer_.emplace_back(c);
    }
  }

  base::StringView GetStringView() const {
    return {buffer_.data(), buffer_.size()};
  }

 private:
  base::SmallVector<char, 2048> buffer_;
};

std::string CreateTableStatementFromSchema(const Table::Schema& schema,
                                           const char* table_name) {
  std::string stmt = "CREATE TABLE x(";
  for (const auto& col : schema.columns) {
    std::string c =
        col.name + " " + sqlite::utils::SqlValueTypeToSqliteTypeName(col.type);
    if (col.is_hidden) {
      c += " HIDDEN";
    }
    stmt += c + ",";
  }

  auto it =
      std::find_if(schema.columns.begin(), schema.columns.end(),
                   [](const Table::Schema::Column& c) { return c.is_id; });
  if (it == schema.columns.end()) {
    PERFETTO_FATAL(
        "id column not found in %s. All tables need to contain an id column;",
        table_name);
  }
  stmt += "PRIMARY KEY(" + it->name + ")";
  stmt += ") WITHOUT ROWID;";
  return stmt;
}

int SqliteValueToSqlValueChecked(SqlValue* sql_val,
                                 sqlite3_value* value,
                                 const Constraint& cs,
                                 sqlite3_vtab* vtab) {
  *sql_val = sqlite::utils::SqliteValueToSqlValue(value);
  if constexpr (regex::IsRegexSupported()) {
    if (cs.op == FilterOp::kRegex) {
      if (cs.value.type != SqlValue::kString) {
        return sqlite::utils::SetError(vtab, "Value has to be a string");
      }
      if (auto st = regex::Regex::Create(cs.value.AsString()); !st.ok()) {
        return sqlite::utils::SetError(vtab, st.status().c_message());
      }
    }
  }
  return SQLITE_OK;
}

inline uint32_t ReadLetterAndInt(char letter, base::StringSplitter* splitter) {
  PERFETTO_CHECK(splitter->Next());
  PERFETTO_DCHECK(splitter->cur_token_size() >= 2);
  PERFETTO_DCHECK(splitter->cur_token()[0] == letter);
  return *base::CStringToUInt32(splitter->cur_token() + 1);
}

inline uint64_t ReadLetterAndLong(char letter, base::StringSplitter* splitter) {
  PERFETTO_CHECK(splitter->Next());
  PERFETTO_DCHECK(splitter->cur_token_size() >= 2);
  PERFETTO_DCHECK(splitter->cur_token()[0] == letter);
  return *base::CStringToUInt64(splitter->cur_token() + 1);
}

int ReadIdxStrAndUpdateCursor(DbSqliteModule::Cursor* cursor,
                              const char* idx_str,
                              sqlite3_value** argv) {
  base::StringSplitter splitter(idx_str, ',');

  uint32_t cs_count = ReadLetterAndInt('C', &splitter);

  Query q;
  q.constraints.resize(cs_count);

  uint32_t c_offset = 0;
  for (auto& cs : q.constraints) {
    PERFETTO_CHECK(splitter.Next());
    cs.col_idx = *base::CStringToUInt32(splitter.cur_token());
    PERFETTO_CHECK(splitter.Next());
    cs.op = static_cast<FilterOp>(*base::CStringToUInt32(splitter.cur_token()));

    if (int ret = SqliteValueToSqlValueChecked(&cs.value, argv[c_offset++], cs,
                                               cursor->pVtab);
        ret != SQLITE_OK) {
      return ret;
    }
  }

  uint32_t ob_count = ReadLetterAndInt('O', &splitter);

  q.orders.resize(ob_count);
  for (auto& ob : q.orders) {
    PERFETTO_CHECK(splitter.Next());
    ob.col_idx = *base::CStringToUInt32(splitter.cur_token());
    PERFETTO_CHECK(splitter.Next());
    ob.desc = *base::CStringToUInt32(splitter.cur_token());
  }

  // DISTINCT
  q.order_type =
      static_cast<Query::OrderType>(ReadLetterAndInt('D', &splitter));

  // Cols used
  q.cols_used = ReadLetterAndLong('U', &splitter);

  // LIMIT
  if (ReadLetterAndInt('L', &splitter)) {
    auto val_op = sqlite::utils::SqliteValueToSqlValue(argv[c_offset++]);
    if (val_op.type != SqlValue::kLong) {
      return sqlite::utils::SetError(cursor->pVtab,
                                     "LIMIT value has to be an INT");
    }
    q.limit = val_op.AsLong();
  }

  // OFFSET
  if (ReadLetterAndInt('F', &splitter)) {
    auto val_op = sqlite::utils::SqliteValueToSqlValue(argv[c_offset++]);
    if (val_op.type != SqlValue::kLong) {
      return sqlite::utils::SetError(cursor->pVtab,
                                     "OFFSET value has to be an INT");
    }
    q.offset = static_cast<uint32_t>(val_op.AsLong());
  }

  cursor->query = std::move(q);
  return SQLITE_OK;
}

void FilterAndSortMetatrace(const std::string& table_name,
                            const Table::Schema& schema,
                            DbSqliteModule::Cursor* cursor,
                            metatrace::Record* r) {
  r->AddArg("Table", table_name);
  for (const Constraint& c : cursor->query.constraints) {
    SafeStringWriter writer;
    writer.AppendString(schema.columns[c.col_idx].name);

    writer.AppendString(" ");
    switch (c.op) {
      case FilterOp::kEq:
        writer.AppendString("=");
        break;
      case FilterOp::kGe:
        writer.AppendString(">=");
        break;
      case FilterOp::kGt:
        writer.AppendString(">");
        break;
      case FilterOp::kLe:
        writer.AppendString("<=");
        break;
      case FilterOp::kLt:
        writer.AppendString("<");
        break;
      case FilterOp::kNe:
        writer.AppendString("!=");
        break;
      case FilterOp::kIsNull:
        writer.AppendString("IS");
        break;
      case FilterOp::kIsNotNull:
        writer.AppendString("IS NOT");
        break;
      case FilterOp::kGlob:
        writer.AppendString("GLOB");
        break;
      case FilterOp::kRegex:
        writer.AppendString("REGEXP");
        break;
    }
    writer.AppendString(" ");

    switch (c.value.type) {
      case SqlValue::kString:
        writer.AppendString(c.value.AsString());
        break;
      case SqlValue::kBytes:
        writer.AppendString("<bytes>");
        break;
      case SqlValue::kNull:
        writer.AppendString("<null>");
        break;
      case SqlValue::kDouble: {
        writer.AppendString(std::to_string(c.value.AsDouble()));
        break;
      }
      case SqlValue::kLong: {
        writer.AppendString(std::to_string(c.value.AsLong()));
        break;
      }
    }
    r->AddArg("Constraint", writer.GetStringView());
  }

  for (const auto& o : cursor->query.orders) {
    SafeStringWriter writer;
    writer.AppendString(schema.columns[o.col_idx].name);
    if (o.desc)
      writer.AppendString(" desc");
    r->AddArg("Order by", writer.GetStringView());
  }
}

}  // namespace

int DbSqliteModule::Create(sqlite3* db,
                           void* ctx,
                           int argc,
                           const char* const* argv,
                           sqlite3_vtab** vtab,
                           char**) {
  PERFETTO_CHECK(argc == 3);
  auto* context = GetContext(ctx);
  auto state = std::move(context->temporary_create_state);
  PERFETTO_CHECK(state);

  std::string sql = CreateTableStatementFromSchema(state->schema, argv[2]);
  if (int ret = sqlite3_declare_vtab(db, sql.c_str()); ret != SQLITE_OK) {
    return ret;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = context->OnCreate(argc, argv, std::move(state));
  res->table_name = argv[2];
  *vtab = res.release();
  return SQLITE_OK;
}

int DbSqliteModule::Destroy(sqlite3_vtab* vtab) {
  auto* t = GetVtab(vtab);
  auto* s = sqlite::ModuleStateManager<DbSqliteModule>::GetState(t->state);
  if (s->computation == TableComputation::kStatic) {
    // SQLite does not read error messages returned from xDestroy so just pick
    // the closest appropriate error code.
    return SQLITE_READONLY;
  }
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  sqlite::ModuleStateManager<DbSqliteModule>::OnDestroy(tab->state);
  return SQLITE_OK;
}

int DbSqliteModule::Connect(sqlite3* db,
                            void* ctx,
                            int argc,
                            const char* const* argv,
                            sqlite3_vtab** vtab,
                            char**) {
  PERFETTO_CHECK(argc == 3);
  auto* context = GetContext(ctx);

  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = context->OnConnect(argc, argv);
  res->table_name = argv[2];

  auto* state =
      sqlite::ModuleStateManager<DbSqliteModule>::GetState(res->state);
  std::string sql = CreateTableStatementFromSchema(state->schema, argv[2]);
  if (int ret = sqlite3_declare_vtab(db, sql.c_str()); ret != SQLITE_OK) {
    return ret;
  }
  *vtab = res.release();
  return SQLITE_OK;
}

int DbSqliteModule::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int DbSqliteModule::BestIndex(sqlite3_vtab* vtab, sqlite3_index_info* info) {
  auto* t = GetVtab(vtab);
  auto* s = sqlite::ModuleStateManager<DbSqliteModule>::GetState(t->state);

  const Table* table = nullptr;
  switch (s->computation) {
    case TableComputation::kStatic:
      table = s->static_table;
      break;
    case TableComputation::kRuntime:
      table = s->runtime_table.get();
      break;
  }

  uint32_t row_count;
  int argv_index;
  switch (s->computation) {
    case TableComputation::kStatic:
    case TableComputation::kRuntime:
      row_count = table->row_count();
      argv_index = 1;
      break;
  }

  std::vector<int> cs_idxes;

  // Limit and offset are a nonstandard type of constraint. We can check if they
  // are present in the query here, but we won't save them as standard
  // constraints and only add them to `idx_str` later.
  int limit = -1;
  int offset = -1;
  bool has_unknown_constraint = false;

  cs_idxes.reserve(static_cast<uint32_t>(info->nConstraint));
  for (int i = 0; i < info->nConstraint; ++i) {
    const auto& c = info->aConstraint[i];
    if (!c.usable || info->aConstraintUsage[i].omit) {
      continue;
    }
    if (std::optional<FilterOp> opt_op = SqliteOpToFilterOp(c.op); !opt_op) {
      if (c.op == SQLITE_INDEX_CONSTRAINT_LIMIT) {
        limit = i;
      } else if (c.op == SQLITE_INDEX_CONSTRAINT_OFFSET) {
        offset = i;
      } else {
        has_unknown_constraint = true;
      }
      continue;
    }
    cs_idxes.push_back(i);
  }

  std::vector<int> ob_idxes(static_cast<uint32_t>(info->nOrderBy));
  std::iota(ob_idxes.begin(), ob_idxes.end(), 0);

  // Reorder constraints to consider the constraints on columns which are
  // cheaper to filter first.
  {
    std::sort(
        cs_idxes.begin(), cs_idxes.end(), [s, info, &table](int a, int b) {
          auto a_idx = static_cast<uint32_t>(info->aConstraint[a].iColumn);
          auto b_idx = static_cast<uint32_t>(info->aConstraint[b].iColumn);
          const auto& a_col = s->schema.columns[a_idx];
          const auto& b_col = s->schema.columns[b_idx];

          // Id columns are the most efficient to filter, as they don't have a
          // support in real data.
          if (a_col.is_id || b_col.is_id)
            return a_col.is_id && !b_col.is_id;

          // Set id columns are inherently sorted and have fast filtering
          // operations.
          if (a_col.is_set_id || b_col.is_set_id)
            return a_col.is_set_id && !b_col.is_set_id;

          // Intrinsically sorted column is more efficient to sort than
          // extrinsically sorted column.
          if (a_col.is_sorted || b_col.is_sorted)
            return a_col.is_sorted && !b_col.is_sorted;

          // Extrinsically sorted column is more efficient to sort than unsorted
          // column.
          if (table) {
            auto a_has_idx = table->GetIndex({a_idx});
            auto b_has_idx = table->GetIndex({b_idx});
            if (a_has_idx || b_has_idx)
              return a_has_idx && !b_has_idx;
          }

          bool a_is_eq = sqlite::utils::IsOpEq(info->aConstraint[a].op);
          bool b_is_eq = sqlite::utils::IsOpEq(info->aConstraint[a].op);
          if (a_is_eq || b_is_eq) {
            return a_is_eq && !b_is_eq;
          }

          // TODO(lalitm): introduce more orderings here based on empirical
          // data.
          return false;
        });
  }

  // Remove any order by constraints which also have an equality constraint.
  {
    auto p = [info, &cs_idxes](int o_idx) {
      auto& o = info->aOrderBy[o_idx];
      auto inner_p = [info, &o](int c_idx) {
        auto& c = info->aConstraint[c_idx];
        return c.iColumn == o.iColumn && sqlite::utils::IsOpEq(c.op);
      };
      return std::any_of(cs_idxes.begin(), cs_idxes.end(), inner_p);
    };
    ob_idxes.erase(std::remove_if(ob_idxes.begin(), ob_idxes.end(), p),
                   ob_idxes.end());
  }

  // Go through the order by constraints in reverse order and eliminate
  // constraints until the first non-sorted column or the first order by in
  // descending order.
  {
    auto p = [info, s](int o_idx) {
      auto& o = info->aOrderBy[o_idx];
      const auto& col = s->schema.columns[static_cast<uint32_t>(o.iColumn)];
      return o.desc || !col.is_sorted;
    };
    auto first_non_sorted_it =
        std::find_if(ob_idxes.rbegin(), ob_idxes.rend(), p);
    auto pop_count = std::distance(ob_idxes.rbegin(), first_non_sorted_it);
    ob_idxes.resize(ob_idxes.size() - static_cast<uint32_t>(pop_count));
  }

  // Create index string. It contains information query Trace Processor will
  // have to run. It can be split into 6 segments: C (constraints), O (orders),
  // D (distinct), U (used), L (limit) and F (offset). It can be directly mapped
  // into `Query` type. The number after C and O signifies how many
  // constraints/orders there are. The number after D maps to the
  // Query::Distinct enum value.
  //
  // "C2,0,0,2,1,O1,0,1,D1,U5,L0,F1" maps to:
  // - "C2,0,0,2,1" - two constraints: kEq on first column and kNe on third
  //   column.
  // - "O1,0,1" - one order by: descending on first column.
  // - "D1" - kUnsorted distinct.
  // - "U5" - Columns 0 and 2 used.
  // - "L1" - LIMIT set. "L0" if no limit.
  // - "F1" - OFFSET set. Can only be set if "L1".

  // Constraints:
  std::string idx_str = "C";
  idx_str += std::to_string(cs_idxes.size());
  for (int i : cs_idxes) {
    const auto& c = info->aConstraint[i];
    auto& o = info->aConstraintUsage[i];
    o.omit = true;
    o.argvIndex = argv_index++;

    auto op = SqliteOpToFilterOp(c.op);
    PERFETTO_DCHECK(op);

    idx_str += ',';
    idx_str += std::to_string(c.iColumn);
    idx_str += ',';
    idx_str += std::to_string(static_cast<uint32_t>(*op));
  }
  idx_str += ",";

  // Orders:
  idx_str += "O";
  idx_str += std::to_string(ob_idxes.size());
  for (int i : ob_idxes) {
    idx_str += ',';
    idx_str += std::to_string(info->aOrderBy[i].iColumn);
    idx_str += ',';
    idx_str += std::to_string(info->aOrderBy[i].desc);
  }
  idx_str += ",";

  // Distinct:
  idx_str += "D";
  if (ob_idxes.size() == 1 && PERFETTO_POPCOUNT(info->colUsed) == 1) {
    switch (sqlite3_vtab_distinct(info)) {
      case 0:
      case 1:
        idx_str += std::to_string(static_cast<int>(Query::OrderType::kSort));
        break;
      case 2:
        idx_str +=
            std::to_string(static_cast<int>(Query::OrderType::kDistinct));
        break;
      case 3:
        idx_str += std::to_string(
            static_cast<int>(Query::OrderType::kDistinctAndSort));
        break;
      default:
        PERFETTO_FATAL("Invalid sqlite3_vtab_distinct result");
    }
  } else {
    // TODO(mayzner): Remove this if condition after implementing multicolumn
    // distinct.
    idx_str += std::to_string(static_cast<int>(Query::OrderType::kSort));
  }
  idx_str += ",";

  // Columns used.
  idx_str += "U";
  idx_str += std::to_string(info->colUsed);
  idx_str += ",";

  // LIMIT. Save as "L1" if limit is present and "L0" if not.
  idx_str += "L";
  if (limit == -1 || has_unknown_constraint) {
    idx_str += "0";
  } else {
    auto& o = info->aConstraintUsage[limit];
    o.omit = true;
    o.argvIndex = argv_index++;
    idx_str += "1";
  }
  idx_str += ",";

  // OFFSET. Save as "F1" if offset is present and "F0" if not.
  idx_str += "F";
  if (offset == -1 || has_unknown_constraint) {
    idx_str += "0";
  } else {
    auto& o = info->aConstraintUsage[offset];
    o.omit = true;
    o.argvIndex = argv_index++;
    idx_str += "1";
  }

  info->idxStr = sqlite3_mprintf("%s", idx_str.c_str());

  info->idxNum = t->best_index_num++;
  info->needToFreeIdxStr = true;

  // We can sort on any column correctly.
  info->orderByConsumed = true;

  auto cost_and_rows =
      EstimateCost(s->schema, row_count, info, cs_idxes, ob_idxes);
  info->estimatedCost = cost_and_rows.cost;
  info->estimatedRows = cost_and_rows.rows;

  PERFETTO_TP_TRACE(
      metatrace::Category::QUERY_TIMELINE, "DB_SQLITE_BEST_INDEX",
      [&](metatrace::Record* record) {
        record->AddArg("name", t->table_name.c_str());
        record->AddArg("idxStr", info->idxStr);
        record->AddArg("idxNum",
                       base::StackString<32>("%d", info->idxNum).c_str());
        record->AddArg(
            "estimatedCost",
            base::StackString<32>("%f", info->estimatedCost).c_str());
        record->AddArg(
            "estimatedRows",
            base::StackString<32>("%lld", info->estimatedRows).c_str());
      });

  return SQLITE_OK;
}

int DbSqliteModule::Open(sqlite3_vtab* tab, sqlite3_vtab_cursor** cursor) {
  auto* t = GetVtab(tab);
  auto* s = sqlite::ModuleStateManager<DbSqliteModule>::GetState(t->state);
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  switch (s->computation) {
    case TableComputation::kStatic:
      c->upstream_table = s->static_table;
      break;
    case TableComputation::kRuntime:
      c->upstream_table = s->runtime_table.get();
      break;
  }
  *cursor = c.release();
  return SQLITE_OK;
}

int DbSqliteModule::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  return SQLITE_OK;
}

int DbSqliteModule::Filter(sqlite3_vtab_cursor* cursor,
                           int idx_num,
                           const char* idx_str,
                           int,
                           sqlite3_value** argv) {
  auto* c = GetCursor(cursor);
  auto* t = GetVtab(cursor->pVtab);
  auto* s = sqlite::ModuleStateManager<DbSqliteModule>::GetState(t->state);

  // Clear out the iterator before filtering to ensure the destructor is run
  // before the table's destructor.
  c->iterator = std::nullopt;

  size_t offset = 0;
  bool is_same_idx = idx_num == c->last_idx_num;
  if (PERFETTO_LIKELY(is_same_idx)) {
    for (auto& cs : c->query.constraints) {
      if (int ret = SqliteValueToSqlValueChecked(&cs.value, argv[offset++], cs,
                                                 c->pVtab);
          ret != SQLITE_OK) {
        return ret;
      }
    }
  } else {
    if (int r = ReadIdxStrAndUpdateCursor(c, idx_str, argv + offset);
        r != SQLITE_OK) {
      return r;
    }
    c->last_idx_num = idx_num;
  }

  // Setup the upstream table based on the computation state.
  switch (s->computation) {
    case TableComputation::kStatic:
    case TableComputation::kRuntime:
      break;
  }

  PERFETTO_TP_TRACE(metatrace::Category::QUERY_DETAILED,
                    "DB_TABLE_FILTER_AND_SORT",
                    [s, t, c](metatrace::Record* r) {
                      FilterAndSortMetatrace(t->table_name, s->schema, c, r);
                    });

  RowMap filter_map = c->upstream_table->QueryToRowMap(c->query);
  if (filter_map.IsRange() && filter_map.size() <= 1) {
    // Currently, our criteria where we have a special fast path is if it's
    // a single ranged row. We have this fast path for joins on id columns
    // where we get repeated queries filtering down to a single row. The
    // other path performs allocations when creating the new table as well
    // as the iterator on the new table whereas this path only uses a single
    // number and lives entirely on the stack.

    // TODO(lalitm): investigate some other criteria where it is beneficial
    // to have a fast path and expand to them.
    c->mode = Cursor::Mode::kSingleRow;
    c->single_row = filter_map.size() == 1
                        ? std::make_optional(filter_map.Get(0))
                        : std::nullopt;
    c->eof = !c->single_row.has_value();
  } else {
    c->mode = Cursor::Mode::kTable;
    c->iterator = c->upstream_table->ApplyAndIterateRows(std::move(filter_map));
    c->eof = !*c->iterator;
  }
  return SQLITE_OK;
}

int DbSqliteModule::Next(sqlite3_vtab_cursor* cursor) {
  auto* c = GetCursor(cursor);
  if (c->mode == Cursor::Mode::kSingleRow) {
    c->eof = true;
  } else {
    c->eof = !++*c->iterator;
  }
  return SQLITE_OK;
}

int DbSqliteModule::Eof(sqlite3_vtab_cursor* cursor) {
  return GetCursor(cursor)->eof;
}

int DbSqliteModule::Column(sqlite3_vtab_cursor* cursor,
                           sqlite3_context* ctx,
                           int N) {
  Cursor* c = GetCursor(cursor);
  auto idx = static_cast<uint32_t>(N);
  SqlValue value = c->mode == Cursor::Mode::kSingleRow
                       ? c->upstream_table->columns()[idx].Get(*c->single_row)
                       : c->iterator->Get(idx);

  // We can say kSqliteStatic for strings because all strings are expected
  // to come from the string pool. Thus they will be valid for the lifetime
  // of trace processor. Similarly, for bytes, we can also use
  // kSqliteStatic because for our iterator will hold onto the pointer as
  // long as we don't call Next(). However, that only happens when Next() is
  // called on the Cursor itself, at which point SQLite no longer cares
  // about the bytes pointer.
  sqlite::utils::ReportSqlValue(ctx, value, sqlite::utils::kSqliteStatic,
                                sqlite::utils::kSqliteStatic);
  return SQLITE_OK;
}

int DbSqliteModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

DbSqliteModule::QueryCost DbSqliteModule::EstimateCost(
    const Table::Schema& schema,
    uint32_t row_count,
    sqlite3_index_info* info,
    const std::vector<int>& cs_idxes,
    const std::vector<int>& ob_idxes) {
  // Currently our cost estimation algorithm is quite simplistic but is good
  // enough for the simplest cases.
  // TODO(lalitm): replace hardcoded constants with either more heuristics
  // based on the exact type of constraint or profiling the queries
  // themselves.

  // We estimate the fixed cost of set-up and tear-down of a query in terms of
  // the number of rows scanned.
  constexpr double kFixedQueryCost = 100.0;

  // Setup the variables for estimating the number of rows we will have at the
  // end of filtering. Note that |current_row_count| should always be at least
  // 1 unless we are absolutely certain that we will return no rows as
  // otherwise SQLite can make some bad choices.
  uint32_t current_row_count = row_count;

  // If the table is empty, any constraint set only pays the fixed cost. Also
  // we can return 0 as the row count as we are certain that we will return no
  // rows.
  if (current_row_count == 0) {
    return QueryCost{kFixedQueryCost, 0};
  }

  // Setup the variables for estimating the cost of filtering.
  double filter_cost = 0.0;
  for (int i : cs_idxes) {
    if (current_row_count < 2) {
      break;
    }
    const auto& c = info->aConstraint[i];
    PERFETTO_DCHECK(c.usable);
    PERFETTO_DCHECK(info->aConstraintUsage[i].omit);
    PERFETTO_DCHECK(info->aConstraintUsage[i].argvIndex > 0);
    const auto& col_schema = schema.columns[static_cast<uint32_t>(c.iColumn)];
    if (sqlite::utils::IsOpEq(c.op) && col_schema.is_id) {
      // If we have an id equality constraint, we can very efficiently filter
      // down to a single row in C++. However, if we're joining with another
      // table, SQLite will do this once per row which can be extremely
      // expensive because of all the virtual table (which is implemented
      // using virtual function calls) machinery. Indicate this by saying that
      // an entire filter call is ~10x the cost of iterating a single row.
      filter_cost += 10;
      current_row_count = 1;
    } else if (sqlite::utils::IsOpEq(c.op)) {
      // if the column is sorted, then binary search. Model this by adding by
      // the log of the number of rows as a good approximation. Otherwise, we'll
      // need to do a full table scan.
      filter_cost +=
          col_schema.is_sorted ? log2(current_row_count) : current_row_count;

      // As an extremely rough heuristic, assume that an equalty constraint
      // will cut down the number of rows by approximately double log of the
      // number of rows.
      double estimated_rows = current_row_count / (2 * log2(current_row_count));
      current_row_count = std::max(static_cast<uint32_t>(estimated_rows), 1u);
    } else if (col_schema.is_sorted &&
               (sqlite::utils::IsOpLe(c.op) || sqlite::utils::IsOpLt(c.op) ||
                sqlite::utils::IsOpGt(c.op) || sqlite::utils::IsOpGe(c.op))) {
      // On a sorted column, if we see any partition constraints, we can do
      // this filter very efficiently. Model this using the log of the  number
      // of rows as a good approximation.
      filter_cost += log2(current_row_count);

      // As an extremely rough heuristic, assume that an partition constraint
      // will cut down the number of rows by approximately double log of the
      // number of rows.
      double estimated_rows = current_row_count / (2 * log2(current_row_count));
      current_row_count = std::max(static_cast<uint32_t>(estimated_rows), 1u);
    } else {
      // Otherwise, we will need to do a full table scan and we estimate we
      // will maybe (at best) halve the number of rows.
      filter_cost += current_row_count;
      current_row_count = std::max(current_row_count / 2u, 1u);
    }
  }

  // Now, to figure out the cost of sorting, multiply the final row count
  // by |qc.order_by().size()| * log(row count). This should act as a crude
  // estimation of the cost.
  double sort_cost =
      static_cast<double>(static_cast<uint32_t>(ob_idxes.size()) *
                          current_row_count) *
      log2(current_row_count);

  // The cost of iterating rows is more expensive than just filtering the rows
  // so multiply by an appropriate factor.
  double iteration_cost = current_row_count * 2.0;

  // To get the final cost, add up all the individual components.
  double final_cost =
      kFixedQueryCost + filter_cost + sort_cost + iteration_cost;
  return QueryCost{final_cost, current_row_count};
}

DbSqliteModule::State::State(Table* _table, Table::Schema _schema)
    : State(TableComputation::kStatic, std::move(_schema)) {
  static_table = _table;
}

DbSqliteModule::State::State(std::unique_ptr<RuntimeTable> _table)
    : State(TableComputation::kRuntime, _table->schema()) {
  runtime_table = std::move(_table);
}

DbSqliteModule::State::State(TableComputation _computation,
                             Table::Schema _schema)
    : computation(_computation), schema(std::move(_schema)) {}

}  // namespace perfetto::trace_processor
