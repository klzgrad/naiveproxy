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

#include "src/trace_processor/importers/common/args_tracker.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {

// Writes |set_id| into the arg_set_id cell at (|col|, |row|) of |df|, handling
// whichever nullability the target column happens to use.
void WriteArgSetId(dataframe::Dataframe* df,
                   uint32_t col,
                   uint32_t row,
                   ArgSetId set_id) {
  auto n = df->GetNullabilityLegacy(col);
  if (n.Is<dataframe::NonNull>()) {
    df->SetCellUncheckedLegacy<dataframe::Uint32, dataframe::NonNull>(col, row,
                                                                      set_id);
  } else if (n.Is<dataframe::DenseNull>()) {
    df->SetCellUncheckedLegacy<dataframe::Uint32, dataframe::DenseNull>(
        col, row, std::make_optional(set_id));
  } else if (n.Is<dataframe::SparseNullWithPopcountAlways>()) {
    df->SetCellUncheckedLegacy<dataframe::Uint32,
                               dataframe::SparseNullWithPopcountAlways>(
        col, row, std::make_optional(set_id));
  } else if (n.Is<dataframe::SparseNullWithPopcountUntilFinalization>()) {
    df->SetCellUncheckedLegacy<
        dataframe::Uint32, dataframe::SparseNullWithPopcountUntilFinalization>(
        col, row, std::make_optional(set_id));
  } else {
    PERFETTO_FATAL("Unsupported nullability type for args.");
  }
}

}  // namespace

ArgsInserter::ArgsInserter(GlobalArgsTracker* global,
                           dataframe::Dataframe* df,
                           uint32_t col,
                           uint32_t row,
                           uint32_t id)
    : global_(global),
      buffer_(global->AcquireArgsBuffer()),
      df_(df),
      col_(col),
      row_(row),
      id_(id) {}

ArgsInserter::ArgsInserter(ArgsInserter&& other) noexcept
    : global_(other.global_),
      buffer_(other.buffer_),
      df_(other.df_),
      col_(other.col_),
      row_(other.row_),
      id_(other.id_) {
  other.global_ = nullptr;
  other.buffer_ = nullptr;
}

ArgsInserter& ArgsInserter::operator=(ArgsInserter&& other) noexcept {
  if (this != &other) {
    // Destroy our current state (commits + releases the buffer), then
    // move-construct from other in place.
    this->~ArgsInserter();
    new (this) ArgsInserter(std::move(other));
  }
  return *this;
}

ArgsInserter::~ArgsInserter() {
  if (!global_)  // Empty, moved-from, or a test mock: nothing to commit.
    return;
  Commit();
  global_->ReleaseArgsBuffer(buffer_);
}

ArgsInserter& ArgsInserter::AddArg(StringId flat_key,
                                   StringId key,
                                   Variadic value,
                                   UpdatePolicy update_policy) {
  std::vector<CompactArg>& args = buffer_->args;
  base::FlatHashMap<StringId, uint32_t>& key_index = buffer_->key_index;

  // Collapse same-key duplicates in place (kSkipIfExists keeps the first value,
  // kAddOrUpdate the last) so AddArgSet never sees two args with the same key.
  // Small sets use a linear scan; past a threshold we build `key_index` to keep
  // large sets (e.g. array args) O(n) rather than O(n^2). Empty index => still
  // scanning linearly.
  constexpr size_t kKeyIndexThreshold = 32;
  CompactArg* existing = nullptr;
  if (key_index.size() != 0) {
    if (uint32_t* idx = key_index.Find(key)) {
      existing = &args[*idx];
    }
  } else {
    for (CompactArg& arg : args) {
      if (arg.key == key) {
        existing = &arg;
        break;
      }
    }
  }
  if (existing) {
    if (update_policy == UpdatePolicy::kSkipIfExists) {
      return *this;
    }
    PERFETTO_DCHECK(update_policy == UpdatePolicy::kAddOrUpdate);
    existing->flat_key = flat_key;
    existing->value = value;
    existing->update_policy = update_policy;
    return *this;
  }

  auto new_index = static_cast<uint32_t>(args.size());
  args.emplace_back(CompactArg{flat_key, key, value, update_policy});
  if (key_index.size() != 0) {
    key_index.Insert(key, new_index);
  } else if (args.size() >= kKeyIndexThreshold) {
    for (uint32_t i = 0; i < args.size(); ++i) {
      key_index.Insert(args[i].key, i);
    }
  }
  return *this;
}

bool ArgsInserter::NeedsTranslation(const ArgsTranslationTable& table) const {
  return std::any_of(buffer_->args.begin(), buffer_->args.end(),
                     [&table](const CompactArg& arg) {
                       return table.NeedsTranslation(arg.flat_key, arg.key,
                                                     arg.value.type);
                     });
}

ArgsInserter::CompactArgSet ArgsInserter::ToCompactArgSet() && {
  CompactArgSet compact_args;
  for (const CompactArg& arg : buffer_->args) {
    compact_args.emplace_back(arg);
  }
  // Leave the buffer empty so Commit() is a no-op: the caller now owns the
  // args.
  buffer_->args.clear();
  buffer_->key_index.Clear();
  return compact_args;
}

void ArgsInserter::Commit() {
  std::vector<CompactArg>& args = buffer_->args;
  if (args.empty()) {
    return;
  }
  ArgSetId set_id =
      global_->AddArgSet(args.data(), 0, static_cast<uint32_t>(args.size()));
  WriteArgSetId(df_, col_, row_, set_id);
}

}  // namespace perfetto::trace_processor
