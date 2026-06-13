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

#include "src/trace_processor/perfetto_sql/intrinsics/operators/etm_iterate_range_vtable.h"
#include <opencsd/ocsd_if_types.h>

#include <cstring>
#include <memory>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/etm/opencsd.h"
#include "src/trace_processor/importers/etm/sql_values.h"
#include "src/trace_processor/importers/etm/storage_handle.h"
#include "src/trace_processor/importers/etm/types.h"
#include "src/trace_processor/importers/etm/util.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor::etm {
namespace {

static constexpr char kSchema[] = R"(
    CREATE TABLE x(
      instruction_index INTEGER,
      address INTEGER,
      opcode INTEGER,
      type TEXT,
      branch_address INTEGER,
      is_conditional INTEGER,
      is_link INTEGER,
      sub_type TEXT,
      instruction_range BLOB HIDDEN
    )
  )";

enum class ColumnIndex {
  kInstructionIndex,
  kAddress,
  kOpcode,
  kType,
  kBranchAddress,
  kIsConditional,
  kIsLink,
  kSubType,
  kInstructionRange
};

constexpr char kInstructionRangeEqArg = 'r';

class IntructionCursor : public sqlite::Module<EtmIterateRangeVtable>::Cursor {
 public:
  explicit IntructionCursor(TraceStorage* storage) : storage_(storage) {}
  int Filter(int, const char* idxStr, int argc, sqlite3_value** argv) {
    std::optional<const InstructionRangeSqlValue*> range;
    if (argc != static_cast<int>(strlen(idxStr))) {
      return sqlite::utils::SetError(pVtab, "Invalid idxStr");
    }
    for (; *idxStr != 0; ++idxStr, ++argv) {
      switch (*idxStr) {
        case kInstructionRangeEqArg: {
          range = sqlite::value::Pointer<InstructionRangeSqlValue>(
              *argv, InstructionRangeSqlValue::kPtrType);
          break;
        }
        default:
          return sqlite::utils::SetError(pVtab, "Invalid idxStr");
      }
    }

    if (!range.has_value()) {
      return sqlite::utils::SetError(pVtab, "Invalid idxStr, no range");
    }

    Reset(*range);
    return SQLITE_OK;
  }

  void Next() {
    ++instruction_index_;
    ptr_ += instr_info_.instr_size;
    if (ptr_ == end_) {
      return;
    }

    instr_info_.instr_addr += instr_info_.instr_size;
    instr_info_.isa = instr_info_.next_isa;
    FeedDecoder();
  }

  bool Eof() { return ptr_ == end_; }

  int Column(sqlite3_context* ctx, int raw_n) {
    switch (static_cast<ColumnIndex>(raw_n)) {
      case ColumnIndex::kInstructionIndex:
        sqlite::result::Long(ctx, instruction_index_);
        break;
      case ColumnIndex::kAddress:
        sqlite::result::Long(ctx, static_cast<int64_t>(instr_info_.instr_addr));
        break;
      case ColumnIndex::kOpcode:
        sqlite::result::Long(ctx, instr_info_.opcode);
        break;
      case ColumnIndex::kType:
        sqlite::result::StaticString(ctx, ToString(instr_info_.type));
        break;
      case ColumnIndex::kBranchAddress:
        if (instr_info_.type == OCSD_INSTR_BR ||
            instr_info_.type == OCSD_INSTR_BR_INDIRECT) {
          sqlite::result::Long(ctx,
                               static_cast<int64_t>(instr_info_.branch_addr));
        }
        break;
      case ColumnIndex::kIsConditional:
        sqlite::result::Long(ctx, instr_info_.is_conditional);
        break;
      case ColumnIndex::kIsLink:
        sqlite::result::Long(ctx, instr_info_.is_link);
        break;
      case ColumnIndex::kSubType:
        sqlite::result::StaticString(ctx, ToString(instr_info_.sub_type));
        break;
      case ColumnIndex::kInstructionRange:
        break;
    }

    return SQLITE_OK;
  }

 private:
  void FeedDecoder() {
    PERFETTO_CHECK(static_cast<size_t>(end_ - ptr_) >=
                   sizeof(instr_info_.opcode));
    memcpy(&instr_info_.opcode, ptr_, sizeof(instr_info_.opcode));
    inst_decoder_.DecodeInstruction(&instr_info_);
  }

  void Reset(const InstructionRangeSqlValue* range) {
    if (!range) {
      ptr_ = nullptr;
      end_ = nullptr;
      return;
    }
    const auto& config =
        StorageHandle(storage_).GetEtmV4Config(range->config_id);
    instr_info_.pe_type.arch = config.etm_v4_config().archVersion();
    instr_info_.pe_type.profile = config.etm_v4_config().coreProfile();
    instr_info_.dsb_dmb_waypoints = 0;  // Not used in ETM
    instr_info_.wfi_wfe_branch = config.etm_v4_config().wfiwfeBranch();
    instr_info_.isa = range->isa;
    instr_info_.instr_addr = range->st_addr;

    ptr_ = range->start;
    end_ = range->end;
    instruction_index_ = 0;
    FeedDecoder();
  }

  TraceStorage* storage_;
  const uint8_t* ptr_ = nullptr;
  const uint8_t* end_ = nullptr;
  ocsd_instr_info instr_info_;
  TrcIDecode inst_decoder_;
  uint32_t instruction_index_ = 0;
};

IntructionCursor* GetInstructionCursor(sqlite3_vtab_cursor* cursor) {
  return static_cast<IntructionCursor*>(cursor);
}

}  // namespace

int EtmIterateRangeVtable::Connect(sqlite3* db,
                                   void* ctx,
                                   int,
                                   const char* const*,
                                   sqlite3_vtab** vtab,
                                   char**) {
  if (int ret = sqlite3_declare_vtab(db, kSchema); ret != SQLITE_OK) {
    return ret;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->storage = GetContext(ctx);
  *vtab = res.release();
  return SQLITE_OK;
}

int EtmIterateRangeVtable::Disconnect(sqlite3_vtab* vtab) {
  delete GetVtab(vtab);
  return SQLITE_OK;
}

int EtmIterateRangeVtable::BestIndex(sqlite3_vtab* tab,
                                     sqlite3_index_info* info) {
  bool seen_range = false;
  int argv_index = 1;
  std::string idx_str;
  for (int i = 0; i < info->nConstraint; ++i) {
    auto& in = info->aConstraint[i];
    auto& out = info->aConstraintUsage[i];

    if (in.iColumn == static_cast<int>(ColumnIndex::kInstructionRange)) {
      if (!in.usable) {
        return SQLITE_CONSTRAINT;
      }
      if (in.op != SQLITE_INDEX_CONSTRAINT_EQ) {
        return sqlite::utils::SetError(
            tab, "instruction_range only supports equality constraints");
      }
      idx_str += kInstructionRangeEqArg;
      out.argvIndex = argv_index++;
      out.omit = true;
      seen_range = true;
      continue;
    }
  }

  if (!seen_range) {
    return sqlite::utils::SetError(tab,
                                   "Constraint required on instruction_range");
  }

  info->idxStr = sqlite3_mprintf("%s", idx_str.c_str());
  info->needToFreeIdxStr = true;

  if (info->nOrderBy == 1 &&
      info->aOrderBy[0].iColumn ==
          static_cast<int>(ColumnIndex::kInstructionIndex) &&
      !info->aOrderBy[0].desc) {
    info->orderByConsumed = true;
  }

  return SQLITE_OK;
}

int EtmIterateRangeVtable::Open(sqlite3_vtab* vtab,
                                sqlite3_vtab_cursor** cursor) {
  *cursor = new IntructionCursor(GetVtab(vtab)->storage);
  return SQLITE_OK;
}

int EtmIterateRangeVtable::Close(sqlite3_vtab_cursor* cursor) {
  delete GetInstructionCursor(cursor);
  return SQLITE_OK;
}

int EtmIterateRangeVtable::Filter(sqlite3_vtab_cursor* cur,
                                  int idxNum,
                                  const char* idxStr,
                                  int argc,
                                  sqlite3_value** argv) {
  GetInstructionCursor(cur)->Filter(idxNum, idxStr, argc, argv);
  return SQLITE_OK;
}

int EtmIterateRangeVtable::Next(sqlite3_vtab_cursor* cur) {
  GetInstructionCursor(cur)->Next();
  return SQLITE_OK;
}

int EtmIterateRangeVtable::Eof(sqlite3_vtab_cursor* cur) {
  return GetInstructionCursor(cur)->Eof();
}

int EtmIterateRangeVtable::Column(sqlite3_vtab_cursor* cur,
                                  sqlite3_context* ctx,
                                  int raw_n) {
  return GetInstructionCursor(cur)->Column(ctx, raw_n);
}

int EtmIterateRangeVtable::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor::etm
