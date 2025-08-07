/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/operators/etm_decode_trace_vtable.h"

#include <sqlite3.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/importers/etm/element_cursor.h"
#include "src/trace_processor/importers/etm/mapping_version.h"
#include "src/trace_processor/importers/etm/opencsd.h"
#include "src/trace_processor/importers/etm/sql_values.h"
#include "src/trace_processor/importers/etm/util.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor::etm {
namespace {

base::StatusOr<ocsd_gen_trc_elem_t> ToElementType(sqlite3_value* value) {
  SqlValue element_type = sqlite::utils::SqliteValueToSqlValue(value);
  if (element_type.type != SqlValue::kString) {
    return base::ErrStatus(
        "Invalid data type for element_type. Expected STRING");
  }
  std::optional<ocsd_gen_trc_elem_t> type = FromString(element_type.AsString());
  if (!type) {
    return base::ErrStatus("Invalid element_type value: %s",
                           element_type.AsString());
  }
  return *type;
}

base::StatusOr<tables::EtmV4TraceTable::Id> GetEtmV4TraceId(
    const TraceStorage* storage,
    sqlite3_value* argv) {
  SqlValue in_id = sqlite::utils::SqliteValueToSqlValue(argv);
  if (in_id.type != SqlValue::kLong) {
    return base::ErrStatus("trace_id must be LONG");
  }

  if (in_id.AsLong() < 0 ||
      in_id.AsLong() >= storage->etm_v4_trace_table().row_count()) {
    return base::ErrStatus("Invalid trace_id value: %" PRIu32,
                           storage->etm_v4_trace_table().row_count());
  }

  return tables::EtmV4TraceTable::Id(static_cast<uint32_t>(in_id.AsLong()));
}

static constexpr char kSchema[] = R"(
    CREATE TABLE x(
      trace_id INTEGER HIDDEN,
      trace_index INTEGER,
      element_index INTEGER,
      element_type TEXT,
      timestamp INTEGER,
      cycle_count INTEGER,
      exception_level INTEGER,
      context_id INTEGER,
      isa TEXT,
      start_address INTEGER,
      end_address INTEGER,
      mapping_id INTEGER,
      instruction_range BLOB HIDDEN
    )
  )";

enum class ColumnIndex {
  kTraceId,
  kTraceIndex,
  kElementIndex,
  kElementType,
  kTimestamp,
  kCycleCount,
  kExceptionLevel,
  kContextId,
  kIsa,
  kStartAddress,
  kEndAddress,
  kMappingId,
  kInstructionRange
};

constexpr char kTraceIdEqArg = 't';
constexpr char kElementTypeEqArg = 'e';
constexpr char kElementTypeInArg = 'E';

}  // namespace

class EtmDecodeTraceVtable::Cursor
    : public sqlite::Module<EtmDecodeTraceVtable>::Cursor {
 public:
  explicit Cursor(Vtab* vtab) : cursor_(vtab->storage) {}

  base::Status Filter(int idxNum,
                      const char* idxStr,
                      int argc,
                      sqlite3_value** argv);
  base::Status Next() { return cursor_.Next(); }
  bool Eof() { return cursor_.Eof(); }
  int Column(sqlite3_context* ctx, int raw_n);

 private:
  base::StatusOr<ElementTypeMask> GetTypeMask(sqlite3_value* argv,
                                              bool is_inlist);
  ElementCursor cursor_;
};

base::StatusOr<ElementTypeMask> EtmDecodeTraceVtable::Cursor::GetTypeMask(
    sqlite3_value* argv,
    bool is_inlist) {
  ElementTypeMask mask;
  if (!is_inlist) {
    ASSIGN_OR_RETURN(ocsd_gen_trc_elem_t type, ToElementType(argv));
    mask.set_bit(type);
    return mask;
  }
  int rc;
  sqlite3_value* type_value;
  for (rc = sqlite3_vtab_in_first(argv, &type_value); rc == SQLITE_OK;
       rc = sqlite3_vtab_in_next(argv, &type_value)) {
    ASSIGN_OR_RETURN(ocsd_gen_trc_elem_t type, ToElementType(argv));
    mask.set_bit(type);
  }
  if (rc != SQLITE_OK || rc != SQLITE_DONE) {
    return base::ErrStatus("Error");
  }
  return mask;
}

base::Status EtmDecodeTraceVtable::Cursor::Filter(int,
                                                  const char* idxStr,
                                                  int argc,
                                                  sqlite3_value** argv) {
  std::optional<tables::EtmV4TraceTable::Id> id;
  ElementTypeMask type_mask;
  type_mask.set_all();
  if (argc != static_cast<int>(strlen(idxStr))) {
    return base::ErrStatus("Invalid idxStr");
  }
  for (; *idxStr != 0; ++idxStr, ++argv) {
    switch (*idxStr) {
      case kTraceIdEqArg: {
        ASSIGN_OR_RETURN(id, GetEtmV4TraceId(cursor_.storage(), *argv));
        break;
      }
      case kElementTypeEqArg: {
        ASSIGN_OR_RETURN(ElementTypeMask tmp, GetTypeMask(*argv, false));
        type_mask &= tmp;
        break;
      }
      case kElementTypeInArg: {
        ASSIGN_OR_RETURN(ElementTypeMask tmp, GetTypeMask(*argv, true));
        type_mask &= tmp;
        break;
      }
      default:
        return base::ErrStatus("Invalid idxStr");
    }
  }

  // Given the BestIndex impl this should not happen!
  PERFETTO_CHECK(id);

  return cursor_.Filter(id, type_mask);
}

int EtmDecodeTraceVtable::Cursor::Column(sqlite3_context* ctx, int raw_n) {
  switch (static_cast<ColumnIndex>(raw_n)) {
    case ColumnIndex::kTraceId:
      sqlite::result::Long(ctx, cursor_.trace_id().value);
      break;
    case ColumnIndex::kTraceIndex:
      sqlite::result::Long(ctx, static_cast<int64_t>(cursor_.index()));
      break;
    case ColumnIndex::kElementIndex:
      sqlite::result::Long(ctx, cursor_.element_index());
      break;
    case ColumnIndex::kElementType:
      sqlite::result::StaticString(ctx, ToString(cursor_.element().getType()));
      break;
    case ColumnIndex::kTimestamp:
      if (cursor_.element().getType() == OCSD_GEN_TRC_ELEM_TIMESTAMP ||
          cursor_.element().has_ts) {
        sqlite::result::Long(ctx,
                             static_cast<int64_t>(cursor_.element().timestamp));
      }
      break;
    case ColumnIndex::kCycleCount:
      if (cursor_.element().has_cc) {
        sqlite::result::Long(ctx, cursor_.element().cycle_count);
      }
      break;
    case ColumnIndex::kExceptionLevel:
      if (cursor_.element().context.el_valid) {
        sqlite::result::Long(ctx, cursor_.element().context.exception_level);
      }
      break;
    case ColumnIndex::kContextId:
      if (cursor_.element().context.ctxt_id_valid) {
        sqlite::result::Long(ctx, cursor_.element().context.context_id);
      }
      break;
    case ColumnIndex::kIsa:
      sqlite::result::StaticString(ctx, ToString(cursor_.element().isa));
      break;
    case ColumnIndex::kStartAddress:
      sqlite::result::Long(ctx,
                           static_cast<int64_t>(cursor_.element().st_addr));
      break;
    case ColumnIndex::kEndAddress:
      sqlite::result::Long(ctx,
                           static_cast<int64_t>(cursor_.element().en_addr));
      break;
    case ColumnIndex::kMappingId:
      if (cursor_.mapping()) {
        sqlite::result::Long(ctx, cursor_.mapping()->id().value);
      }
      break;
    case ColumnIndex::kInstructionRange:
      if (cursor_.has_instruction_range()) {
        sqlite::result::UniquePointer(ctx, cursor_.GetInstructionRange(),
                                      InstructionRangeSqlValue::kPtrType);
      }
      break;
  }

  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Connect(sqlite3* db,
                                  void* ctx,
                                  int,
                                  const char* const*,
                                  sqlite3_vtab** vtab,
                                  char**) {
  if (int ret = sqlite3_declare_vtab(db, kSchema); ret != SQLITE_OK) {
    return ret;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>(GetContext(ctx));
  *vtab = res.release();
  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Disconnect(sqlite3_vtab* vtab) {
  delete GetVtab(vtab);
  return SQLITE_OK;
}

int EtmDecodeTraceVtable::BestIndex(sqlite3_vtab* tab,
                                    sqlite3_index_info* info) {
  bool seen_id_eq = false;
  int argv_index = 1;
  std::string idx_str;
  for (int i = 0; i < info->nConstraint; ++i) {
    auto& in = info->aConstraint[i];
    auto& out = info->aConstraintUsage[i];

    if (in.iColumn == static_cast<int>(ColumnIndex::kTraceId)) {
      if (!in.usable) {
        return SQLITE_CONSTRAINT;
      }
      if (in.op != SQLITE_INDEX_CONSTRAINT_EQ) {
        return sqlite::utils::SetError(
            tab, "trace_id only supports equality constraints");
      }
      seen_id_eq = true;

      idx_str += kTraceIdEqArg;
      out.argvIndex = argv_index++;
      out.omit = true;
      continue;
    }
    if (in.usable &&
        in.iColumn == static_cast<int>(ColumnIndex::kElementType)) {
      if (in.op != SQLITE_INDEX_CONSTRAINT_EQ) {
        continue;
      }

      if (sqlite3_vtab_in(info, i, 1)) {
        idx_str += kElementTypeInArg;
      } else {
        idx_str += kElementTypeEqArg;
      }

      out.argvIndex = argv_index++;
      out.omit = true;
      continue;
    }
  }
  if (!seen_id_eq) {
    return sqlite::utils::SetError(tab, "Constraint required on trace_id");
  }

  info->idxStr = sqlite3_mprintf("%s", idx_str.c_str());
  info->needToFreeIdxStr = true;

  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Open(sqlite3_vtab* sql_vtab,
                               sqlite3_vtab_cursor** cursor) {
  *cursor = new Cursor(GetVtab(sql_vtab));
  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Close(sqlite3_vtab_cursor* cursor) {
  delete GetCursor(cursor);
  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Filter(sqlite3_vtab_cursor* cur,
                                 int idxNum,
                                 const char* idxStr,
                                 int argc,
                                 sqlite3_value** argv) {
  auto status = GetCursor(cur)->Filter(idxNum, idxStr, argc, argv);
  if (!status.ok()) {
    return sqlite::utils::SetError(cur->pVtab, status);
  }
  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Next(sqlite3_vtab_cursor* cur) {
  auto status = GetCursor(cur)->Next();
  if (!status.ok()) {
    return sqlite::utils::SetError(cur->pVtab, status);
  }
  return SQLITE_OK;
}

int EtmDecodeTraceVtable::Eof(sqlite3_vtab_cursor* cur) {
  return GetCursor(cur)->Eof();
}

int EtmDecodeTraceVtable::Column(sqlite3_vtab_cursor* cur,
                                 sqlite3_context* ctx,
                                 int raw_n) {
  return GetCursor(cur)->Column(ctx, raw_n);
}

int EtmDecodeTraceVtable::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor::etm
