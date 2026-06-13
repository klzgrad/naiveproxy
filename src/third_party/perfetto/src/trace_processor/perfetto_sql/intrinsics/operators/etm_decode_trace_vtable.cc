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

base::StatusOr<tables::EtmV4ChunkTable::Id> GetEtmV4ChunkId(
    const TraceStorage* storage,
    sqlite3_value* argv) {
  SqlValue in_id = sqlite::utils::SqliteValueToSqlValue(argv);
  if (in_id.type != SqlValue::kLong) {
    return base::ErrStatus("chunk_id must be LONG");
  }

  if (in_id.AsLong() < 0 ||
      in_id.AsLong() >= storage->etm_v4_chunk_table().row_count()) {
    return base::ErrStatus("Invalid chunk_id value: %" PRIu32,
                           storage->etm_v4_chunk_table().row_count());
  }

  return tables::EtmV4ChunkTable::Id(static_cast<uint32_t>(in_id.AsLong()));
}

static constexpr char kSchema[] = R"(
    CREATE TABLE x(
      chunk_id INTEGER HIDDEN,
      chunk_index INTEGER,
      element_index INTEGER,
      element_type TEXT,
      timestamp INTEGER,
      cycle_count INTEGER,
      last_seen_timestamp INTEGER,
      cumulative_cycles INTEGER,
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
  kChunkId,
  kChunkIndex,
  kElementIndex,
  kElementType,
  kTimestamp,
  kCycleCount,
  kLastSeenTimestamp,
  kCumulativeCycles,
  kExceptionLevel,
  kContextId,
  kIsa,
  kStartAddress,
  kEndAddress,
  kMappingId,
  kInstructionRange
};

constexpr char kChunkIdEqArg = 't';
constexpr char kElementTypeEqArg = 'e';
constexpr char kElementTypeInArg = 'E';

}  // namespace

class EtmDecodeChunkVtable::Cursor
    : public sqlite::Module<EtmDecodeChunkVtable>::Cursor {
 private:
  // The maximum number of rows to buffer while waiting for a timestamp.
  static constexpr size_t kMaxBufferedRows = 100;

  struct State {
    // Stores the last seen timestamp.
    std::optional<int64_t> last_seen_timestamp;
    // Stores the cumulative cycle count including timestamp packets.
    std::optional<int64_t> cumulative_cycle_count;
    // Stores the last cumulative cycle count using only cycle count packets.
    int64_t last_cc_value = 0;
    // Indicates if we are waiting for a timestamp.
    bool waiting_for_timestamp = false;
  };
  struct BufferedRow {
    OcsdTraceElement element;
    uint32_t chunk_id;
    ocsd_trc_index_t index;
    uint32_t element_index;
    std::optional<uint32_t> mapping_id;
    std::optional<std::unique_ptr<InstructionRangeSqlValue>> instruction_range;
  };

 public:
  explicit Cursor(Vtab* vtab) : cursor_(vtab->storage) {}

  base::Status Filter(int idxNum,
                      const char* idxStr,
                      int argc,
                      sqlite3_value** argv);
  base::Status Next();
  bool Eof();
  int Column(sqlite3_context* ctx, int raw_n);

 private:
  void FlushBuffer() {
    flushing_buffer_ = false;
    rows_waiting_for_timestamp_.clear();
    buffer_idx_ = 0;
  }

  void StartFlushingBuffer() {
    flushing_buffer_ = true;
    buffer_idx_ = 0;
  }

  base::Status HandleWaitingForTimestamp();
  base::Status HandleFlushingBuffer();

  base::StatusOr<ElementTypeMask> GetTypeMask(sqlite3_value* argv,
                                              bool is_inlist);

  ElementCursor cursor_;
  // If multiple Cursors for EtmDecodeChunkVtable are needed at the same time
  // cumulative cycle count will struggle to be correct.
  State state_{};

  // Buffer of rows waiting for a timestamp packet (i.e. saw a sync and looking
  // for timestamp)
  std::vector<BufferedRow> rows_waiting_for_timestamp_;
  bool flushing_buffer_ = false;
  size_t buffer_idx_ = 0;
};

base::Status EtmDecodeChunkVtable::Cursor::HandleFlushingBuffer() {
  if (buffer_idx_ + 1 < rows_waiting_for_timestamp_.size() &&
      rows_waiting_for_timestamp_[buffer_idx_ + 1].element.getType() ==
          OCSD_GEN_TRC_ELEM_SYNC_MARKER) {
    flushing_buffer_ = false;
    rows_waiting_for_timestamp_.erase(rows_waiting_for_timestamp_.begin(),
                                      rows_waiting_for_timestamp_.begin() +
                                          static_cast<long>(buffer_idx_ + 1));
    buffer_idx_ = 0;
    state_.waiting_for_timestamp = true;
  } else {
    buffer_idx_++;
    if (buffer_idx_ >= rows_waiting_for_timestamp_.size()) {
      FlushBuffer();
    } else if (rows_waiting_for_timestamp_[buffer_idx_].element.getType() ==
               OCSD_GEN_TRC_ELEM_CYCLE_COUNT) {
      state_.last_cc_value +=
          rows_waiting_for_timestamp_[buffer_idx_].element.cycle_count;
      state_.cumulative_cycle_count = state_.last_cc_value;
    }
  }
  return base::OkStatus();
}

base::Status EtmDecodeChunkVtable::Cursor::HandleWaitingForTimestamp() {
  if (cursor_.element().getType() == OCSD_GEN_TRC_ELEM_TIMESTAMP) {
    state_.last_seen_timestamp =
        static_cast<int64_t>(cursor_.element().timestamp);
    state_.waiting_for_timestamp = false;

    for (auto& row : rows_waiting_for_timestamp_) {
      if (row.element.getType() == OCSD_GEN_TRC_ELEM_SYNC_MARKER) {
        row.element.timestamp = cursor_.element().timestamp;
        row.element.has_ts = true;
        if (cursor_.element().has_cc) {
          row.element.cycle_count = cursor_.element().cycle_count;
          row.element.has_cc = true;
          state_.cumulative_cycle_count =
              cursor_.element().cycle_count + state_.last_cc_value;
        }
        break;
      }
    }
    rows_waiting_for_timestamp_.push_back(
        {cursor_.element(), cursor_.chunk_id().value, cursor_.index(),
         cursor_.element_index(),
         cursor_.mapping() ? std::make_optional(cursor_.mapping()->id().value)
                           : std::nullopt,
         cursor_.has_instruction_range()
             ? std::make_optional(cursor_.GetInstructionRange())
             : std::nullopt});
    StartFlushingBuffer();
  } else {
    rows_waiting_for_timestamp_.push_back(
        {cursor_.element(), cursor_.chunk_id().value, cursor_.index(),
         cursor_.element_index(),
         cursor_.mapping() ? std::make_optional(cursor_.mapping()->id().value)
                           : std::nullopt,
         cursor_.has_instruction_range()
             ? std::make_optional(cursor_.GetInstructionRange())
             : std::nullopt});
    // If the following ever occurs then we have reached a point where a
    // sync never got a timestamp. To guard against this and accurately
    // report it we will modify last_seen_timestamp_ to be null for the rows
    // in our buffer.
    if (rows_waiting_for_timestamp_.size() >= kMaxBufferedRows) {
      StartFlushingBuffer();
      state_.last_seen_timestamp = std::nullopt;
      state_.waiting_for_timestamp = false;
    }
  }
  return base::OkStatus();
}

base::Status EtmDecodeChunkVtable::Cursor::Next() {
  if (flushing_buffer_) {
    RETURN_IF_ERROR(HandleFlushingBuffer());
    if (flushing_buffer_ || Eof()) {
      return base::OkStatus();
    }
  }

  while (true) {
    RETURN_IF_ERROR(cursor_.Next());
    if (cursor_.Eof()) {
      if (state_.waiting_for_timestamp &&
          !rows_waiting_for_timestamp_.empty()) {
        StartFlushingBuffer();
      }
      return base::OkStatus();
    }

    if (state_.waiting_for_timestamp) {
      RETURN_IF_ERROR(HandleWaitingForTimestamp());
      if (flushing_buffer_) {
        return base::OkStatus();
      }
    } else {
      if (cursor_.element().getType() == OCSD_GEN_TRC_ELEM_SYNC_MARKER) {
        state_.waiting_for_timestamp = true;
        rows_waiting_for_timestamp_.push_back(
            {cursor_.element(), cursor_.chunk_id().value, cursor_.index(),
             cursor_.element_index(),
             cursor_.mapping()
                 ? std::make_optional(cursor_.mapping()->id().value)
                 : std::nullopt,
             cursor_.has_instruction_range()
                 ? std::make_optional(cursor_.GetInstructionRange())
                 : std::nullopt});
        continue;
      }
      break;
    }
  }
  if (!flushing_buffer_ && cursor_.element().has_cc) {
    if (cursor_.element().getType() == OCSD_GEN_TRC_ELEM_SYNC_MARKER) {
      state_.cumulative_cycle_count =
          cursor_.element().cycle_count + state_.last_cc_value;
    } else if (cursor_.element().getType() == OCSD_GEN_TRC_ELEM_CYCLE_COUNT) {
      state_.last_cc_value += cursor_.element().cycle_count;
      state_.cumulative_cycle_count = state_.last_cc_value;
    }
  }
  return base::OkStatus();
}

bool EtmDecodeChunkVtable::Cursor::Eof() {
  if (flushing_buffer_) {
    return buffer_idx_ >= rows_waiting_for_timestamp_.size();
  }
  return cursor_.Eof();
}

base::StatusOr<ElementTypeMask> EtmDecodeChunkVtable::Cursor::GetTypeMask(
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
    return base::ErrStatus("Error processing IN list for element_type");
  }
  return mask;
}

base::Status EtmDecodeChunkVtable::Cursor::Filter(int,
                                                  const char* idxStr,
                                                  int argc,
                                                  sqlite3_value** argv) {
  state_ = State{};
  rows_waiting_for_timestamp_.clear();
  rows_waiting_for_timestamp_.reserve(kMaxBufferedRows);
  std::optional<tables::EtmV4ChunkTable::Id> id;
  ElementTypeMask type_mask;
  type_mask.set_all();
  if (argc != static_cast<int>(strlen(idxStr))) {
    return base::ErrStatus("Invalid idxStr");
  }
  for (; *idxStr != 0; ++idxStr, ++argv) {
    switch (*idxStr) {
      case kChunkIdEqArg: {
        ASSIGN_OR_RETURN(id, GetEtmV4ChunkId(cursor_.storage(), *argv));
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

int EtmDecodeChunkVtable::Cursor::Column(sqlite3_context* ctx, int raw_n) {
  const OcsdTraceElement* elem;
  uint32_t chunkId, elementIndex;
  ocsd_trc_index_t index;
  std::optional<uint32_t> mappingId;
  std::optional<std::unique_ptr<InstructionRangeSqlValue>> instructionRange;
  if (flushing_buffer_) {
    const auto& row = rows_waiting_for_timestamp_[buffer_idx_];
    elem = &row.element;
    chunkId = row.chunk_id;
    index = row.index;
    elementIndex = row.element_index;
    mappingId = row.mapping_id;
    if (row.instruction_range) {
      instructionRange =
          std::move(const_cast<BufferedRow&>(row).instruction_range);
    }
  } else {
    elem = &cursor_.element();
    chunkId = cursor_.chunk_id().value;
    index = cursor_.index();
    elementIndex = cursor_.element_index();
    mappingId = cursor_.mapping()
                    ? std::make_optional(cursor_.mapping()->id().value)
                    : std::nullopt;
    if (cursor_.has_instruction_range()) {
      instructionRange = cursor_.GetInstructionRange();
    }
  }

  switch (static_cast<ColumnIndex>(raw_n)) {
    case ColumnIndex::kChunkId:
      sqlite::result::Long(ctx, chunkId);
      break;
    case ColumnIndex::kChunkIndex:
      sqlite::result::Long(ctx, static_cast<int64_t>(index));
      break;
    case ColumnIndex::kElementIndex:
      sqlite::result::Long(ctx, elementIndex);
      break;
    case ColumnIndex::kElementType:
      sqlite::result::StaticString(ctx, ToString(elem->getType()));
      break;
    case ColumnIndex::kTimestamp:
      if (elem->getType() == OCSD_GEN_TRC_ELEM_TIMESTAMP || elem->has_ts) {
        sqlite::result::Long(ctx, static_cast<int64_t>(elem->timestamp));
      }
      break;
    case ColumnIndex::kCycleCount:
      if (elem->has_cc) {
        sqlite::result::Long(ctx, elem->cycle_count);
      }
      break;
    case ColumnIndex::kLastSeenTimestamp:
      if (state_.last_seen_timestamp &&
          elem->getType() != OCSD_GEN_TRC_ELEM_TIMESTAMP) {
        sqlite::result::Long(ctx, *state_.last_seen_timestamp);
      }
      break;
    case ColumnIndex::kCumulativeCycles:
      if (state_.cumulative_cycle_count &&
          elem->getType() != OCSD_GEN_TRC_ELEM_TIMESTAMP) {
        sqlite::result::Long(ctx, *state_.cumulative_cycle_count);
      }
      break;
    case ColumnIndex::kExceptionLevel:
      if (elem->context.el_valid) {
        sqlite::result::Long(ctx, elem->context.exception_level);
      }
      break;
    case ColumnIndex::kContextId:
      if (elem->context.ctxt_id_valid) {
        sqlite::result::Long(ctx, elem->context.context_id);
      }
      break;
    case ColumnIndex::kIsa:
      sqlite::result::StaticString(ctx, ToString(elem->isa));
      break;
    case ColumnIndex::kStartAddress:
      sqlite::result::Long(ctx, static_cast<int64_t>(elem->st_addr));
      break;
    case ColumnIndex::kEndAddress:
      sqlite::result::Long(ctx, static_cast<int64_t>(elem->en_addr));
      break;
    case ColumnIndex::kMappingId:
      if (mappingId) {
        sqlite::result::Long(ctx, static_cast<int64_t>(*mappingId));
      }
      break;
    case ColumnIndex::kInstructionRange:
      if (instructionRange) {
        sqlite::result::UniquePointer(ctx, std::move(*instructionRange),
                                      InstructionRangeSqlValue::kPtrType);
      }
      break;
  }

  return SQLITE_OK;
}

int EtmDecodeChunkVtable::Connect(sqlite3* db,
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

int EtmDecodeChunkVtable::Disconnect(sqlite3_vtab* vtab) {
  delete GetVtab(vtab);
  return SQLITE_OK;
}

int EtmDecodeChunkVtable::BestIndex(sqlite3_vtab* tab,
                                    sqlite3_index_info* info) {
  bool seen_id_eq = false;
  int argv_index = 1;
  std::string idx_str;
  for (int i = 0; i < info->nConstraint; ++i) {
    auto& in = info->aConstraint[i];
    auto& out = info->aConstraintUsage[i];

    if (in.iColumn == static_cast<int>(ColumnIndex::kChunkId)) {
      if (!in.usable) {
        return SQLITE_CONSTRAINT;
      }
      if (in.op != SQLITE_INDEX_CONSTRAINT_EQ) {
        return sqlite::utils::SetError(
            tab, "chunk_id only supports equality constraints");
      }
      seen_id_eq = true;

      idx_str += kChunkIdEqArg;
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
    return sqlite::utils::SetError(tab, "Constraint required on chunk_id");
  }

  info->idxStr = sqlite3_mprintf("%s", idx_str.c_str());
  info->needToFreeIdxStr = true;

  return SQLITE_OK;
}

int EtmDecodeChunkVtable::Open(sqlite3_vtab* sql_vtab,
                               sqlite3_vtab_cursor** cursor) {
  *cursor = new Cursor(GetVtab(sql_vtab));
  return SQLITE_OK;
}

int EtmDecodeChunkVtable::Close(sqlite3_vtab_cursor* cursor) {
  delete GetCursor(cursor);
  return SQLITE_OK;
}

int EtmDecodeChunkVtable::Filter(sqlite3_vtab_cursor* cur,
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

int EtmDecodeChunkVtable::Next(sqlite3_vtab_cursor* cur) {
  auto status = GetCursor(cur)->Next();
  if (!status.ok()) {
    return sqlite::utils::SetError(cur->pVtab, status);
  }
  return SQLITE_OK;
}

int EtmDecodeChunkVtable::Eof(sqlite3_vtab_cursor* cur) {
  return GetCursor(cur)->Eof();
}

int EtmDecodeChunkVtable::Column(sqlite3_vtab_cursor* cur,
                                 sqlite3_context* ctx,
                                 int raw_n) {
  return GetCursor(cur)->Column(ctx, raw_n);
}

int EtmDecodeChunkVtable::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor::etm
