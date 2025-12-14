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
#include <limits>
#include <optional>
#include <tuple>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/small_vector.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

ArgsTracker::ArgsTracker(TraceProcessorContext* context) : context_(context) {}

ArgsTracker::~ArgsTracker() {
  Flush();
}

void ArgsTracker::AddArg(void* ptr,
                         uint32_t col,
                         uint32_t row,
                         StringId flat_key,
                         StringId key,
                         Variadic value,
                         UpdatePolicy update_policy) {
  args_.emplace_back();

  auto* rid_arg = &args_.back();
  rid_arg->ptr = ptr;
  rid_arg->col = col;
  rid_arg->row = row;
  rid_arg->flat_key = flat_key;
  rid_arg->key = key;
  rid_arg->value = value;
  rid_arg->update_policy = update_policy;
}

void ArgsTracker::Flush() {
  using Arg = GlobalArgsTracker::Arg;

  if (args_.empty())
    return;

  // We need to ensure that the args with the same arg set (arg_set_id + row)
  // and key are grouped together. This is important for joining the args from
  // different events (e.g. trace event begin and trace event end might both
  // have arguments).
  //
  // To achieve that (and do it quickly) we do two steps:
  // - First, group all of the values within the same key together and compute
  // the smallest index for each key.
  // - Then we sort the args by column, row, smallest_index_for_key (to group
  // keys) and index (to preserve the original ordering).

  struct Entry {
    size_t index;
    StringId key;
    size_t smallest_index_for_key = 0;

    Entry(size_t i, StringId k) : index(i), key(k) {}
  };

  base::SmallVector<Entry, 16> entries;
  for (const auto& arg : args_) {
    entries.emplace_back(entries.size(), arg.key);
  }

  // Step 1: Compute the `smallest_index_for_key`.
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return std::tie(a.key, a.index) < std::tie(b.key, b.index);
  });

  // As the data is sorted by (`key`, `index`) now, then the objects with the
  // same key will be contiguous and within this block it will be sorted by
  // index. That means that `smallest_index_for_key` for the entire block should
  // be the value of the first index in the block.
  entries[0].smallest_index_for_key = entries[0].index;
  for (size_t i = 1; i < entries.size(); ++i) {
    entries[i].smallest_index_for_key =
        entries[i].key == entries[i - 1].key
            ? entries[i - 1].smallest_index_for_key
            : entries[i].index;
  }

  // Step 2: sort in the desired order: grouping by arg set first (column, row),
  // then ensuring that the args with the same key are grouped together
  // (smallest_index_for_key) and then preserving the original order within
  // these group (index).
  std::sort(entries.begin(), entries.end(),
            [&](const Entry& a, const Entry& b) {
              const Arg& first_arg = args_[a.index];
              const Arg& second_arg = args_[b.index];
              return std::tie(first_arg.ptr, first_arg.col, first_arg.row,
                              a.smallest_index_for_key, a.index) <
                     std::tie(second_arg.ptr, second_arg.col, second_arg.row,
                              b.smallest_index_for_key, b.index);
            });

  // Apply permutation of entries[].index to args.
  base::SmallVector<Arg, 16> sorted_args;
  for (auto& entry : entries) {
    sorted_args.emplace_back(args_[entry.index]);
  }

  // Insert args.
  for (uint32_t i = 0; i < args_.size();) {
    const GlobalArgsTracker::Arg& arg = sorted_args[i];
    void* ptr = arg.ptr;
    uint32_t col = arg.col;
    uint32_t row = arg.row;

    uint32_t next_rid_idx = i + 1;
    while (next_rid_idx < sorted_args.size() &&
           ptr == sorted_args[next_rid_idx].ptr &&
           col == sorted_args[next_rid_idx].col &&
           row == sorted_args[next_rid_idx].row) {
      next_rid_idx++;
    }

    ArgSetId set_id = context_->global_args_tracker->AddArgSet(
        sorted_args.data(), i, next_rid_idx);
    auto* df = static_cast<dataframe::Dataframe*>(ptr);
    auto n = df->GetNullabilityLegacy(col);
    if (n.Is<dataframe::NonNull>()) {
      df->SetCellUncheckedLegacy<dataframe::Uint32, dataframe::NonNull>(
          arg.col, row, set_id);
    } else if (n.Is<dataframe::DenseNull>()) {
      df->SetCellUncheckedLegacy<dataframe::Uint32, dataframe::DenseNull>(
          arg.col, row, std::make_optional(set_id));
    } else if (n.Is<dataframe::SparseNullWithPopcountAlways>()) {
      df->SetCellUncheckedLegacy<dataframe::Uint32,
                                 dataframe::SparseNullWithPopcountAlways>(
          arg.col, row, std::make_optional(set_id));
    } else if (n.Is<dataframe::SparseNullWithPopcountUntilFinalization>()) {
      df->SetCellUncheckedLegacy<
          dataframe::Uint32,
          dataframe::SparseNullWithPopcountUntilFinalization>(
          arg.col, row, std::make_optional(set_id));
    } else {
      PERFETTO_FATAL("Unsupported nullability type for args.");
    }

    i = next_rid_idx;
  }
  args_.clear();
}

ArgsTracker::CompactArgSet ArgsTracker::ToCompactArgSet(
    const dataframe::Dataframe& dataframe,
    uint32_t col,
    uint32_t row) && {
  CompactArgSet compact_args;
  for (const auto& arg : args_) {
    PERFETTO_DCHECK(arg.ptr == &dataframe);
    PERFETTO_DCHECK(arg.col == col);
    PERFETTO_DCHECK(arg.row == row);
    compact_args.emplace_back(arg.ToCompactArg());
  }
  args_.clear();
  return compact_args;
}

bool ArgsTracker::NeedsTranslation(const ArgsTranslationTable& table) const {
  return std::any_of(
      args_.begin(), args_.end(), [&table](const GlobalArgsTracker::Arg& arg) {
        return table.NeedsTranslation(arg.flat_key, arg.key, arg.value.type);
      });
}

ArgsTracker::BoundInserter::BoundInserter(ArgsTracker* args_tracker,
                                          dataframe::Dataframe* dataframe,
                                          uint32_t col,
                                          uint32_t row)
    : args_tracker_(args_tracker), ptr_(dataframe), col_(col), row_(row) {}

ArgsTracker::BoundInserter::~BoundInserter() = default;

}  // namespace perfetto::trace_processor
