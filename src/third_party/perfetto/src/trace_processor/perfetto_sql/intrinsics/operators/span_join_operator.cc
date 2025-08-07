/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/operators/span_join_operator.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"

namespace perfetto::trace_processor {

namespace {

constexpr char kTsColumnName[] = "ts";
constexpr char kDurColumnName[] = "dur";

bool IsRequiredColumn(const std::string& name) {
  return name == kTsColumnName;
}

bool IsSpecialColumn(const std::string& name,
                     const std::optional<std::string>& partition_col) {
  return name == kTsColumnName || name == kDurColumnName ||
         name == partition_col;
}

std::optional<std::string> HasDuplicateColumns(
    const std::vector<std::pair<SqlValue::Type, std::string>>& t1,
    const std::vector<std::pair<SqlValue::Type, std::string>>& t2,
    const std::optional<std::string>& partition_col) {
  std::unordered_set<std::string> seen_names;
  for (const auto& col : t1) {
    if (IsSpecialColumn(col.second, partition_col)) {
      continue;
    }
    if (seen_names.count(col.second) > 0) {
      return col.second;
    }
    seen_names.insert(col.second);
  }
  for (const auto& col : t2) {
    if (IsSpecialColumn(col.second, partition_col)) {
      continue;
    }
    if (seen_names.count(col.second) > 0) {
      return col.second;
    }
    seen_names.insert(col.second);
  }
  return std::nullopt;
}

std::optional<std::string> OpToString(int op) {
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
    case SQLITE_INDEX_CONSTRAINT_LIKE:
      return " like ";
    case SQLITE_INDEX_CONSTRAINT_GLOB:
      return " glob ";
    case SQLITE_INDEX_CONSTRAINT_ISNULL:
      // The "null" will be added below in EscapedSqliteValueAsString.
      return " is ";
    case SQLITE_INDEX_CONSTRAINT_ISNOTNULL:
      // The "null" will be added below in EscapedSqliteValueAsString.
      return " is not ";
    default:
      return std::nullopt;
  }
}

std::string EscapedSqliteValueAsString(sqlite3_value* value) {
  switch (sqlite3_value_type(value)) {
    case SQLITE_INTEGER:
      return std::to_string(sqlite3_value_int64(value));
    case SQLITE_FLOAT:
      return std::to_string(sqlite3_value_double(value));
    case SQLITE_TEXT: {
      // If str itself contains a single quote, we need to escape it with
      // another single quote.
      const char* str =
          reinterpret_cast<const char*>(sqlite3_value_text(value));
      return "'" + base::ReplaceAll(str, "'", "''") + "'";
    }
    case SQLITE_NULL:
      return " null";
    default:
      PERFETTO_FATAL("Unknown value type %d", sqlite3_value_type(value));
  }
}

}  // namespace

void SpanJoinOperatorModule::Vtab::PopulateColumnLocatorMap(uint32_t offset) {
  for (uint32_t i = 0; i < t1_defn.columns().size(); ++i) {
    if (i == t1_defn.ts_idx() || i == t1_defn.dur_idx() ||
        i == t1_defn.partition_idx()) {
      continue;
    }
    ColumnLocator* locator = &global_index_to_column_locator[offset++];
    locator->defn = &t1_defn;
    locator->col_index = i;
  }
  for (uint32_t i = 0; i < t2_defn.columns().size(); ++i) {
    if (i == t2_defn.ts_idx() || i == t2_defn.dur_idx() ||
        i == t2_defn.partition_idx()) {
      continue;
    }
    ColumnLocator* locator = &global_index_to_column_locator[offset++];
    locator->defn = &t2_defn;
    locator->col_index = i;
  }
}

std::string SpanJoinOperatorModule::Vtab::BestIndexStrForDefinition(
    const sqlite3_index_info* info,
    const TableDefinition& defn) {
  uint32_t count = 0;
  std::string constraints;
  for (int i = 0; i < info->nConstraint; i++) {
    const auto& c = info->aConstraint[i];
    if (!c.usable) {
      continue;
    }

    auto col_name = GetNameForGlobalColumnIndex(defn, c.iColumn);
    if (col_name.empty()) {
      continue;
    }

    // Le constraints can be passed straight to the child tables as they won't
    // affect the span join computation. Similarly, source_geq constraints
    // explicitly request that they are passed as geq constraints to the source
    // tables.
    if (col_name == kTsColumnName && !sqlite::utils::IsOpLe(c.op) &&
        c.op != kSourceGeqOpCode) {
      continue;
    }

    // Allow SQLite handle any constraints on duration apart from source_geq
    // constraints.
    if (col_name == kDurColumnName && c.op != kSourceGeqOpCode) {
      continue;
    }

    // If we're emitting shadow slices, don't propagate any constraints
    // on this table as this will break the shadow slice computation.
    if (defn.ShouldEmitPresentPartitionShadow()) {
      continue;
    }

    // If we cannot handle the constraint, skip it.
    std::optional<std::string> op = OpToString(
        c.op == kSourceGeqOpCode ? SQLITE_INDEX_CONSTRAINT_GE : c.op);
    if (!op) {
      continue;
    }

    PERFETTO_DCHECK(info->aConstraintUsage[i].argvIndex > 0);
    std::string argvIndex =
        std::to_string(info->aConstraintUsage[i].argvIndex - 1);
    constraints += "," + argvIndex + "," + "`" + col_name + "`" + *op;
    count++;
  }
  return std::to_string(count) + constraints;
}

base::Status SpanJoinOperatorModule::TableDefinition::Create(
    PerfettoSqlEngine* engine,
    const TableDescriptor& desc,
    EmitShadowType emit_shadow_type,
    TableDefinition* defn) {
  if (desc.partition_col == kTsColumnName ||
      desc.partition_col == kDurColumnName) {
    return base::ErrStatus(
        "SPAN_JOIN: partition column cannot be any of {ts, dur} for table %s",
        desc.name.c_str());
  }

  std::vector<std::pair<SqlValue::Type, std::string>> cols;
  RETURN_IF_ERROR(sqlite::utils::GetColumnsForTable(
      engine->sqlite_engine()->db(), desc.name, cols));

  uint32_t required_columns_found = 0;
  uint32_t ts_idx = std::numeric_limits<uint32_t>::max();
  std::optional<uint32_t> dur_idx;
  uint32_t partition_idx = std::numeric_limits<uint32_t>::max();
  for (uint32_t i = 0; i < cols.size(); i++) {
    auto col = cols[i];
    if (IsRequiredColumn(col.second)) {
      ++required_columns_found;
    }
    if (base::Contains(col.second, ",")) {
      return base::ErrStatus("SPAN_JOIN: column '%s' cannot contain any ','",
                             col.second.c_str());
    }
    if (base::Contains(col.second, ':')) {
      return base::ErrStatus("SPAN_JOIN: column '%s' cannot contain any ':'",
                             col.second.c_str());
    }

    if (col.second == kTsColumnName) {
      ts_idx = i;
    } else if (col.second == kDurColumnName) {
      dur_idx = i;
    } else if (col.second == desc.partition_col) {
      partition_idx = i;
    }
  }
  if (required_columns_found != 1) {
    return base::ErrStatus("SPAN_JOIN: Missing ts column in table %s",
                           desc.name.c_str());
  }
  if (desc.IsPartitioned() && partition_idx >= cols.size()) {
    return base::ErrStatus(
        "SPAN_JOIN: Missing partition column '%s' in table '%s'",
        desc.partition_col.c_str(), desc.name.c_str());
  }

  PERFETTO_DCHECK(ts_idx < cols.size());

  *defn = TableDefinition(desc.name, desc.partition_col, std::move(cols),
                          emit_shadow_type, ts_idx, dur_idx, partition_idx);
  return base::OkStatus();
}

std::string
SpanJoinOperatorModule::TableDefinition::CreateVtabCreateTableSection() const {
  std::string cols;
  for (const auto& col : columns()) {
    if (IsSpecialColumn(col.second, partition_col())) {
      continue;
    }
    if (col.first == SqlValue::Type::kNull) {
      cols += col.second + ",";
    } else {
      cols += col.second + " " +
              sqlite::utils::SqlValueTypeToSqliteTypeName(col.first) + ",";
    }
  }
  return cols;
}

std::string SpanJoinOperatorModule::Vtab::GetNameForGlobalColumnIndex(
    const TableDefinition& defn,
    int global_column) {
  auto col_idx = static_cast<size_t>(global_column);
  if (col_idx == Column::kTimestamp) {
    return kTsColumnName;
  }
  if (col_idx == Column::kDuration) {
    return kDurColumnName;
  }
  if (col_idx == Column::kPartition &&
      partitioning != PartitioningType::kNoPartitioning) {
    return defn.partition_col();
  }

  const auto& locator = global_index_to_column_locator[col_idx];
  if (locator.defn != &defn) {
    return "";
  }
  return defn.columns()[locator.col_index].second;
}

SpanJoinOperatorModule::Query::Query(Vtab* vtab,
                                     const TableDefinition* definition)
    : defn_(definition), vtab_(vtab) {
  PERFETTO_DCHECK(!defn_->IsPartitioned() ||
                  defn_->partition_idx() < defn_->columns().size());
}

SpanJoinOperatorModule::Query::~Query() = default;

base::Status SpanJoinOperatorModule::Query::Initialize(
    std::string sql_query,
    InitialEofBehavior eof_behavior) {
  *this = Query(vtab_, definition());
  sql_query_ = std::move(sql_query);
  base::Status status = Rewind();
  if (!status.ok())
    return status;
  if (eof_behavior == InitialEofBehavior::kTreatAsMissingPartitionShadow &&
      IsEof()) {
    state_ = State::kMissingPartitionShadow;
  }
  return status;
}

base::Status SpanJoinOperatorModule::Query::Next() {
  RETURN_IF_ERROR(NextSliceState());
  return FindNextValidSlice();
}

bool SpanJoinOperatorModule::Query::IsValidSlice() {
  // Disallow any single partition shadow slices if the definition doesn't allow
  // them.
  if (IsPresentPartitionShadow() && !defn_->ShouldEmitPresentPartitionShadow())
    return false;

  // Disallow any missing partition shadow slices if the definition doesn't
  // allow them.
  if (IsMissingPartitionShadow() && !defn_->ShouldEmitMissingPartitionShadow())
    return false;

  // Disallow any "empty" shadows; these are shadows which either have the same
  // start and end time or missing-partition shadows which have the same start
  // and end partition.
  if (IsEmptyShadow())
    return false;

  return true;
}

base::Status SpanJoinOperatorModule::Query::FindNextValidSlice() {
  // The basic idea of this function is that |NextSliceState()| always emits
  // all possible slices (including shadows for any gaps inbetween the real
  // slices) and we filter out the invalid slices (as defined by the table
  // definition) using |IsValidSlice()|.
  //
  // This has proved to be a lot cleaner to implement than trying to choose
  // when to emit and not emit shadows directly.
  while (!IsEof() && !IsValidSlice()) {
    RETURN_IF_ERROR(NextSliceState());
  }
  return base::OkStatus();
}

base::Status SpanJoinOperatorModule::Query::NextSliceState() {
  switch (state_) {
    case State::kReal: {
      // Forward the cursor to figure out where the next slice should be.
      RETURN_IF_ERROR(CursorNext());

      // Depending on the next slice, we can do two things here:
      // 1. If the next slice is on the same partition, we can just emit a
      //    single shadow until the start of the next slice.
      // 2. If the next slice is on another partition or we hit eof, just emit
      //    a shadow to the end of the whole partition.
      bool shadow_to_end = cursor_eof_ || (defn_->IsPartitioned() &&
                                           partition_ != CursorPartition());
      state_ = State::kPresentPartitionShadow;
      ts_ = AdjustedTsEnd();
      ts_end_ =
          shadow_to_end ? std::numeric_limits<int64_t>::max() : CursorTs();
      return base::OkStatus();
    }
    case State::kPresentPartitionShadow: {
      if (ts_end_ == std::numeric_limits<int64_t>::max()) {
        // If the shadow is to the end of the slice, create a missing partition
        // shadow to the start of the partition of the next slice or to the max
        // partition if we hit eof.
        state_ = State::kMissingPartitionShadow;
        ts_ = 0;
        ts_end_ = std::numeric_limits<int64_t>::max();

        missing_partition_start_ = partition_ + 1;
        missing_partition_end_ = cursor_eof_
                                     ? std::numeric_limits<int64_t>::max()
                                     : CursorPartition();
      } else {
        // If the shadow is not to the end, we must have another slice on the
        // current partition.
        state_ = State::kReal;
        ts_ = CursorTs();
        ts_end_ = ts_ + CursorDur();

        PERFETTO_DCHECK(!defn_->IsPartitioned() ||
                        partition_ == CursorPartition());
      }
      return base::OkStatus();
    }
    case State::kMissingPartitionShadow: {
      if (missing_partition_end_ == std::numeric_limits<int64_t>::max()) {
        PERFETTO_DCHECK(cursor_eof_);

        // If we have a missing partition to the max partition, we must have hit
        // eof.
        state_ = State::kEof;
      } else {
        PERFETTO_DCHECK(!defn_->IsPartitioned() ||
                        CursorPartition() == missing_partition_end_);

        // Otherwise, setup a single partition slice on the end partition to the
        // start of the next slice.
        state_ = State::kPresentPartitionShadow;
        ts_ = 0;
        ts_end_ = CursorTs();
        partition_ = missing_partition_end_;
      }
      return base::OkStatus();
    }
    case State::kEof: {
      PERFETTO_DFATAL("Called Next when EOF");
      return base::ErrStatus("Called Next when EOF");
    }
  }
  PERFETTO_FATAL("For GCC");
}

base::Status SpanJoinOperatorModule::Query::Rewind() {
  auto res = vtab_->engine->sqlite_engine()->PrepareStatement(
      SqlSource::FromTraceProcessorImplementation(sql_query_));
  cursor_eof_ = false;
  RETURN_IF_ERROR(res.status());
  stmt_ = std::move(res);

  RETURN_IF_ERROR(CursorNext());

  // Setup the first slice as a missing partition shadow from the lowest
  // partition until the first slice partition. We will handle finding the real
  // slice in |FindNextValidSlice()|.
  state_ = State::kMissingPartitionShadow;
  ts_ = 0;
  ts_end_ = std::numeric_limits<int64_t>::max();
  missing_partition_start_ = std::numeric_limits<int64_t>::min();

  if (cursor_eof_) {
    missing_partition_end_ = std::numeric_limits<int64_t>::max();
  } else if (defn_->IsPartitioned()) {
    missing_partition_end_ = CursorPartition();
  } else {
    missing_partition_end_ = std::numeric_limits<int64_t>::min();
  }

  // Actually compute the first valid slice.
  return FindNextValidSlice();
}

base::Status SpanJoinOperatorModule::Query::CursorNext() {
  if (defn_->IsPartitioned()) {
    auto partition_idx = static_cast<int>(defn_->partition_idx());
    // Fastforward through any rows with null partition keys.
    int row_type;
    do {
      cursor_eof_ = !stmt_->Step();
      RETURN_IF_ERROR(stmt_->status());
      row_type = sqlite3_column_type(stmt_->sqlite_stmt(), partition_idx);
    } while (!cursor_eof_ && row_type == SQLITE_NULL);

    if (!cursor_eof_ && row_type != SQLITE_INTEGER) {
      return base::ErrStatus("SPAN_JOIN: partition is not an INT column");
    }
  } else {
    cursor_eof_ = !stmt_->Step();
  }
  return base::OkStatus();
}

void SpanJoinOperatorModule::Query::ReportSqliteResult(sqlite3_context* context,
                                                       size_t index) {
  if (state_ != State::kReal) {
    return sqlite::result::Null(context);
  }

  sqlite3_stmt* stmt = stmt_->sqlite_stmt();
  int idx = static_cast<int>(index);
  switch (sqlite3_column_type(stmt, idx)) {
    case SQLITE_INTEGER:
      return sqlite::result::Long(context, sqlite3_column_int64(stmt, idx));
    case SQLITE_FLOAT:
      return sqlite::result::Double(context, sqlite3_column_double(stmt, idx));
    case SQLITE_TEXT: {
      // TODO(lalitm): note for future optimizations: if we knew the addresses
      // of the string intern pool, we could check if the string returned here
      // comes from the pool, and pass it as non-transient.
      const auto* ptr =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
      return sqlite::result::TransientString(context, ptr);
    }
    case SQLITE_BLOB: {
      return sqlite::result::TransientBytes(context,
                                            sqlite3_column_blob(stmt, idx),
                                            sqlite3_column_bytes(stmt, idx));
    }
  }
}

SpanJoinOperatorModule::TableDefinition::TableDefinition(
    std::string name,
    std::string partition_col,
    std::vector<std::pair<SqlValue::Type, std::string>> cols,
    EmitShadowType emit_shadow_type,
    uint32_t ts_idx,
    std::optional<uint32_t> dur_idx,
    uint32_t partition_idx)
    : emit_shadow_type_(emit_shadow_type),
      name_(std::move(name)),
      partition_col_(std::move(partition_col)),
      cols_(std::move(cols)),
      ts_idx_(ts_idx),
      dur_idx_(dur_idx),
      partition_idx_(partition_idx) {}

base::Status SpanJoinOperatorModule::TableDescriptor::Parse(
    const std::string& raw_descriptor,
    TableDescriptor* descriptor) {
  // Descriptors have one of the following forms:
  // table_name [PARTITIONED column_name]

  // Find the table name.
  base::StringSplitter splitter(raw_descriptor, ' ');
  if (!splitter.Next())
    return base::ErrStatus("SPAN_JOIN: Missing table name");

  descriptor->name = splitter.cur_token();
  if (!splitter.Next())
    return base::OkStatus();

  if (!base::CaseInsensitiveEqual(splitter.cur_token(), "PARTITIONED"))
    return base::ErrStatus("SPAN_JOIN: Invalid token");

  if (!splitter.Next())
    return base::ErrStatus("SPAN_JOIN: Missing partitioning column");

  descriptor->partition_col = splitter.cur_token();
  return base::OkStatus();
}

std::string SpanJoinOperatorModule::TableDefinition::CreateSqlQuery(
    base::StringSplitter& idx,
    sqlite3_value** argv) const {
  std::vector<std::string> col_names;
  for (const auto& c : columns()) {
    col_names.push_back("`" + c.second + "`");
  }

  PERFETTO_CHECK(idx.Next());
  std::optional<uint32_t> cs_count = base::StringToUInt32(idx.cur_token());
  PERFETTO_CHECK(cs_count);
  std::vector<std::string> cs;
  cs.reserve(*cs_count);
  for (uint32_t i = 0; i < *cs_count; ++i) {
    PERFETTO_CHECK(idx.Next());
    std::optional<uint32_t> argv_idx = base::StringToUInt32(idx.cur_token());
    PERFETTO_CHECK(argv_idx);

    PERFETTO_CHECK(idx.Next());
    cs.emplace_back(idx.cur_token() +
                    EscapedSqliteValueAsString(argv[*argv_idx]));
  }

  std::string sql = "SELECT " + base::Join(col_names, ", ");
  sql += " FROM " + name();
  if (!cs.empty()) {
    sql += " WHERE " + base::Join(cs, " AND ");
  }
  sql += " ORDER BY ";
  sql += IsPartitioned() ? base::Join({"`" + partition_col() + "`", "ts"}, ", ")
                         : "ts";
  sql += ";";
  return sql;
}

int SpanJoinOperatorModule::Create(sqlite3* db,
                                   void* ctx,
                                   int argc,
                                   const char* const* argv,
                                   sqlite3_vtab** vtab_out,
                                   char** pzErr) {
  // argv[0] - argv[2] are SQLite populated fields which are always present.
  if (argc != 5) {
    *pzErr = sqlite3_mprintf("SPAN_JOIN: expected exactly two arguments");
    return SQLITE_ERROR;
  }

  auto* context = GetContext(ctx);
  std::unique_ptr<Vtab> vtab = std::make_unique<Vtab>();
  vtab->engine = context->engine;
  vtab->module_name = argv[0];

  TableDescriptor t1_desc;
  auto status = TableDescriptor::Parse(
      std::string(reinterpret_cast<const char*>(argv[3])), &t1_desc);
  if (!status.ok()) {
    *pzErr = sqlite3_mprintf("%s", status.c_message());
    return SQLITE_ERROR;
  }

  TableDescriptor t2_desc;
  status = TableDescriptor::Parse(
      std::string(reinterpret_cast<const char*>(argv[4])), &t2_desc);
  if (!status.ok()) {
    *pzErr = sqlite3_mprintf("%s", status.c_message());
    return SQLITE_ERROR;
  }

  // Check that the partition columns match between the two tables.
  if (t1_desc.partition_col == t2_desc.partition_col) {
    vtab->partitioning = t1_desc.IsPartitioned()
                             ? PartitioningType::kSamePartitioning
                             : PartitioningType::kNoPartitioning;
  } else if (t1_desc.IsPartitioned() && t2_desc.IsPartitioned()) {
    *pzErr = sqlite3_mprintf(
        "SPAN_JOIN: mismatching partitions between the two tables; "
        "(partition %s in table %s, partition %s in table %s)",
        t1_desc.partition_col.c_str(), t1_desc.name.c_str(),
        t2_desc.partition_col.c_str(), t2_desc.name.c_str());
    return SQLITE_ERROR;
  } else {
    vtab->partitioning = PartitioningType::kMixedPartitioning;
  }

  bool t1_part_mixed =
      t1_desc.IsPartitioned() &&
      vtab->partitioning == PartitioningType::kMixedPartitioning;
  bool t2_part_mixed =
      t2_desc.IsPartitioned() &&
      vtab->partitioning == PartitioningType::kMixedPartitioning;

  EmitShadowType t1_shadow_type;
  if (vtab->IsOuterJoin()) {
    if (t1_part_mixed ||
        vtab->partitioning == PartitioningType::kNoPartitioning) {
      t1_shadow_type = EmitShadowType::kPresentPartitionOnly;
    } else {
      t1_shadow_type = EmitShadowType::kAll;
    }
  } else {
    t1_shadow_type = EmitShadowType::kNone;
  }
  status = TableDefinition::Create(vtab->engine, t1_desc, t1_shadow_type,
                                   &vtab->t1_defn);
  if (!status.ok()) {
    *pzErr = sqlite3_mprintf("%s", status.c_message());
    return SQLITE_ERROR;
  }

  EmitShadowType t2_shadow_type;
  if (vtab->IsOuterJoin() || vtab->IsLeftJoin()) {
    if (t2_part_mixed ||
        vtab->partitioning == PartitioningType::kNoPartitioning) {
      t2_shadow_type = EmitShadowType::kPresentPartitionOnly;
    } else {
      t2_shadow_type = EmitShadowType::kAll;
    }
  } else {
    t2_shadow_type = EmitShadowType::kNone;
  }
  status = TableDefinition::Create(vtab->engine, t2_desc, t2_shadow_type,
                                   &vtab->t2_defn);
  if (!status.ok()) {
    *pzErr = sqlite3_mprintf("%s", status.c_message());
    return SQLITE_ERROR;
  }

  if (!vtab->t1_defn.dur_idx().has_value() &&
      !vtab->t2_defn.dur_idx().has_value()) {
    *pzErr = sqlite3_mprintf(
        "SPAN_JOIN: column %s must be present in at least one of tables %s and "
        "%s",
        kDurColumnName, vtab->t1_defn.name().c_str(),
        vtab->t2_defn.name().c_str());
    return SQLITE_ERROR;
  }

  if (auto dupe = HasDuplicateColumns(
          vtab->t1_defn.columns(), vtab->t2_defn.columns(),
          vtab->partitioning == PartitioningType::kNoPartitioning
              ? std::nullopt
              : std::make_optional(vtab->partition_col()))) {
    *pzErr = sqlite3_mprintf(
        "SPAN_JOIN: column %s present in both tables %s and %s", dupe->c_str(),
        vtab->t1_defn.name().c_str(), vtab->t2_defn.name().c_str());
    return SQLITE_ERROR;
  }

  // Create the map from column index to the column in the child sub-queries.
  vtab->PopulateColumnLocatorMap(
      vtab->partitioning == PartitioningType::kNoPartitioning ? 2 : 3);

  std::string primary_key = "ts";
  std::string partition;
  if (vtab->partitioning != PartitioningType::kNoPartitioning) {
    partition = vtab->partition_col() + " BIGINT,";
    primary_key += ", " + vtab->partition_col();
  }
  std::string t1_section = vtab->t1_defn.CreateVtabCreateTableSection();
  std::string t2_section = vtab->t2_defn.CreateVtabCreateTableSection();
  static constexpr char kStmt[] = R"(
    CREATE TABLE x(
      ts BIGINT,
      dur BIGINT,
      %s
      %s
      %s
      PRIMARY KEY(%s)
    )
  )";
  base::StackString<1024> create_table_str(
      kStmt, partition.c_str(), t1_section.c_str(), t2_section.c_str(),
      primary_key.c_str());
  vtab->create_table_stmt = create_table_str.ToStdString();
  if (int ret = sqlite3_declare_vtab(db, create_table_str.c_str());
      ret != SQLITE_OK) {
    return ret;
  }
  *vtab_out = vtab.release();
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Connect(sqlite3* db,
                                    void* ctx,
                                    int argc,
                                    const char* const* argv,
                                    sqlite3_vtab** vtab,
                                    char** pzErr) {
  return Create(db, ctx, argc, argv, vtab, pzErr);
}

int SpanJoinOperatorModule::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int SpanJoinOperatorModule::BestIndex(sqlite3_vtab* tab,
                                      sqlite3_index_info* info) {
  int argvIndex = 1;
  for (int i = 0; i < info->nConstraint; ++i) {
    if (!info->aConstraint[i].usable) {
      continue;
    }
    info->aConstraintUsage[i].argvIndex = argvIndex++;
  }

  Vtab* table = GetVtab(tab);
  if (table->partitioning == PartitioningType::kNoPartitioning) {
    // If both tables are not partitioned and we have a single order by on ts,
    // we return data in the correct order.
    info->orderByConsumed = info->nOrderBy == 1 &&
                            info->aOrderBy[0].iColumn == Column::kTimestamp &&
                            !info->aOrderBy[0].desc;
  } else {
    // If one of the tables is partitioned, and we have an order by on the
    // partition column followed (optionally) by an order by on timestamp, we
    // return data in the correct order.
    bool is_first_ob_partition =
        info->nOrderBy > 0 && info->aOrderBy[0].iColumn == Column::kPartition &&
        !info->aOrderBy[0].desc;
    bool is_second_ob_ts = info->nOrderBy >= 2 &&
                           info->aOrderBy[1].iColumn == Column::kTimestamp &&
                           !info->aOrderBy[1].desc;
    info->orderByConsumed =
        (info->nOrderBy == 1 && is_first_ob_partition) ||
        (info->nOrderBy == 2 && is_first_ob_partition && is_second_ob_ts);
  }

  for (int i = 0; i < info->nConstraint; ++i) {
    if (info->aConstraint[i].op == kSourceGeqOpCode) {
      info->aConstraintUsage[i].omit = true;
    }
  }

  std::string t1 = table->BestIndexStrForDefinition(info, table->t1_defn);
  std::string t2 = table->BestIndexStrForDefinition(info, table->t2_defn);
  info->idxStr = sqlite3_mprintf("%s,%s", t1.c_str(), t2.c_str());
  info->needToFreeIdxStr = true;

  return SQLITE_OK;
}

int SpanJoinOperatorModule::Open(sqlite3_vtab* tab,
                                 sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>(GetVtab(tab));
  *cursor = c.release();
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Filter(sqlite3_vtab_cursor* cursor,
                                   int,
                                   const char* idxStr,
                                   int,
                                   sqlite3_value** argv) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_DETAILED, "SPAN_JOIN_XFILTER");

  Cursor* c = GetCursor(cursor);
  Vtab* table = GetVtab(cursor->pVtab);

  base::StringSplitter splitter(std::string(idxStr), ',');
  bool t1_partitioned_mixed =
      c->t1.definition()->IsPartitioned() &&
      table->partitioning == PartitioningType::kMixedPartitioning;
  auto t1_eof = table->IsOuterJoin() && !t1_partitioned_mixed
                    ? Query::InitialEofBehavior::kTreatAsMissingPartitionShadow
                    : Query::InitialEofBehavior::kTreatAsEof;
  base::Status status =
      c->t1.Initialize(table->t1_defn.CreateSqlQuery(splitter, argv), t1_eof);
  if (!status.ok()) {
    return sqlite::utils::SetError(table, status.c_message());
  }

  bool t2_partitioned_mixed =
      c->t2.definition()->IsPartitioned() &&
      table->partitioning == PartitioningType::kMixedPartitioning;
  auto t2_eof =
      (table->IsLeftJoin() || table->IsOuterJoin()) && !t2_partitioned_mixed
          ? Query::InitialEofBehavior::kTreatAsMissingPartitionShadow
          : Query::InitialEofBehavior::kTreatAsEof;
  status =
      c->t2.Initialize(table->t2_defn.CreateSqlQuery(splitter, argv), t2_eof);
  if (!status.ok()) {
    return sqlite::utils::SetError(table, status.c_message());
  }

  status = c->FindOverlappingSpan();
  if (!status.ok()) {
    return sqlite::utils::SetError(table, status.c_message());
  }
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Next(sqlite3_vtab_cursor* cursor) {
  Cursor* c = GetCursor(cursor);
  Vtab* table = GetVtab(cursor->pVtab);
  base::Status status = c->next_query->Next();
  if (!status.ok()) {
    return sqlite::utils::SetError(table, status.c_message());
  }
  status = c->FindOverlappingSpan();
  if (!status.ok()) {
    return sqlite::utils::SetError(table, status.c_message());
  }
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Eof(sqlite3_vtab_cursor* cur) {
  Cursor* c = GetCursor(cur);
  return c->t1.IsEof() || c->t2.IsEof();
}

int SpanJoinOperatorModule::Column(sqlite3_vtab_cursor* cursor,
                                   sqlite3_context* context,
                                   int N) {
  Cursor* c = GetCursor(cursor);
  Vtab* table = GetVtab(cursor->pVtab);

  PERFETTO_DCHECK(c->t1.IsReal() || c->t2.IsReal());

  switch (N) {
    case Column::kTimestamp: {
      auto max_ts = std::max(c->t1.ts(), c->t2.ts());
      sqlite::result::Long(context, static_cast<sqlite3_int64>(max_ts));
      break;
    }
    case Column::kDuration: {
      auto max_start = std::max(c->t1.ts(), c->t2.ts());
      auto min_end = std::min(c->t1.raw_ts_end(), c->t2.raw_ts_end());
      auto dur = min_end - max_start;
      sqlite::result::Long(context, static_cast<sqlite3_int64>(dur));
      break;
    }
    case Column::kPartition: {
      if (table->partitioning != PartitioningType::kNoPartitioning) {
        int64_t partition;
        if (table->partitioning == PartitioningType::kMixedPartitioning) {
          partition = c->last_mixed_partition_;
        } else {
          partition = c->t1.IsReal() ? c->t1.partition() : c->t2.partition();
        }
        sqlite::result::Long(context, static_cast<sqlite3_int64>(partition));
        break;
      }
      PERFETTO_FALLTHROUGH;
    }
    default: {
      const auto* locator =
          table->global_index_to_column_locator.Find(static_cast<size_t>(N));
      PERFETTO_CHECK(locator);
      if (locator->defn == c->t1.definition()) {
        c->t1.ReportSqliteResult(context, locator->col_index);
      } else {
        c->t2.ReportSqliteResult(context, locator->col_index);
      }
    }
  }
  return SQLITE_OK;
}

int SpanJoinOperatorModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

int SpanJoinOperatorModule::FindFunction(sqlite3_vtab*,
                                         int,
                                         const char* name,
                                         FindFunctionFn** fn,
                                         void**) {
  if (base::CaseInsensitiveEqual(name, "source_geq")) {
    *fn = [](sqlite3_context* ctx, int, sqlite3_value**) {
      return sqlite::result::Error(ctx, "Should not be called.");
    };
    return kSourceGeqOpCode;
  }
  return 0;
}

bool SpanJoinOperatorModule::Cursor::IsOverlappingSpan() const {
  // If either of the tables are eof, then we cannot possibly have an
  // overlapping span.
  if (t1.IsEof() || t2.IsEof())
    return false;

  // One of the tables always needs to have a real span to have a valid
  // overlapping span.
  if (!t1.IsReal() && !t2.IsReal())
    return false;

  using PartitioningType = PartitioningType;
  if (vtab->partitioning == PartitioningType::kSamePartitioning) {
    // If both tables are partitioned, then ensure that the partitions overlap.
    bool partition_in_bounds = (t1.FirstPartition() >= t2.FirstPartition() &&
                                t1.FirstPartition() <= t2.LastPartition()) ||
                               (t2.FirstPartition() >= t1.FirstPartition() &&
                                t2.FirstPartition() <= t1.LastPartition());
    if (!partition_in_bounds)
      return false;
  }

  // We consider all slices to be [start, end) - that is the range of
  // timestamps has an open interval at the start but a closed interval
  // at the end. (with the exception of dur == -1 which we treat as if
  // end == start for the purpose of this function).
  return (t1.ts() == t2.ts() && t1.IsReal() && t2.IsReal()) ||
         (t1.ts() >= t2.ts() && t1.ts() < t2.AdjustedTsEnd()) ||
         (t2.ts() >= t1.ts() && t2.ts() < t1.AdjustedTsEnd());
}

base::Status SpanJoinOperatorModule::Cursor::FindOverlappingSpan() {
  // We loop until we find a slice which overlaps from the two tables.
  while (true) {
    if (vtab->partitioning == PartitioningType::kMixedPartitioning) {
      // If we have a mixed partition setup, we need to have special checks
      // for eof and to reset the unpartitioned cursor every time the partition
      // changes in the partitioned table.
      auto* partitioned = t1.definition()->IsPartitioned() ? &t1 : &t2;
      auto* unpartitioned = t1.definition()->IsPartitioned() ? &t2 : &t1;

      // If the partitioned table reaches eof, then we are really done.
      if (partitioned->IsEof())
        break;

      // If the partition has changed from the previous one, reset the cursor
      // and keep a lot of the new partition.
      if (last_mixed_partition_ != partitioned->partition()) {
        base::Status status = unpartitioned->Rewind();
        if (!status.ok())
          return status;
        last_mixed_partition_ = partitioned->partition();
      }
    } else if (t1.IsEof() || t2.IsEof()) {
      // For both no partition and same partition cases, either cursor ending
      // ends the whole span join.
      break;
    }

    // Find which slice finishes first.
    next_query = FindEarliestFinishQuery();

    // If the current span is overlapping, just finish there to emit the current
    // slice.
    if (IsOverlappingSpan())
      break;

    // Otherwise, step to the next row.
    base::Status status = next_query->Next();
    if (!status.ok())
      return status;
  }
  return base::OkStatus();
}

SpanJoinOperatorModule::Query*
SpanJoinOperatorModule::Cursor::FindEarliestFinishQuery() {
  int64_t t1_part;
  int64_t t2_part;

  switch (vtab->partitioning) {
    case PartitioningType::kMixedPartitioning: {
      // If either table is EOF, forward the other table to try and make
      // the partitions not match anymore.
      if (t1.IsEof())
        return &t2;
      if (t2.IsEof())
        return &t1;

      // Otherwise, just make the partition equal from both tables.
      t1_part = last_mixed_partition_;
      t2_part = last_mixed_partition_;
      break;
    }
    case PartitioningType::kSamePartitioning: {
      // Get the partition values from the cursor.
      t1_part = t1.LastPartition();
      t2_part = t2.LastPartition();
      break;
    }
    case PartitioningType::kNoPartitioning: {
      t1_part = 0;
      t2_part = 0;
      break;
    }
  }

  // Prefer to forward the earliest cursors based on the following
  // lexiographical ordering:
  // 1. partition
  // 2. end timestamp
  // 3. whether the slice is real or shadow (shadow < real)
  bool t1_less = std::make_tuple(t1_part, t1.AdjustedTsEnd(), t1.IsReal()) <
                 std::make_tuple(t2_part, t2.AdjustedTsEnd(), t2.IsReal());
  return t1_less ? &t1 : &t2;
}

}  // namespace perfetto::trace_processor
