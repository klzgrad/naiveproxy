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

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/slice_translation_table.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {
constexpr uint32_t kMaxDepth = 512;
}

SliceTracker::SliceTracker(TraceProcessorContext* context)
    : legacy_unnestable_begin_count_string_id_(
          context->storage->InternString("legacy_unnestable_begin_count")),
      legacy_unnestable_last_begin_ts_string_id_(
          context->storage->InternString("legacy_unnestable_last_begin_ts")),
      context_(context) {}

SliceTracker::~SliceTracker() {
  FlushPendingSlices();
}

std::optional<SliceId> SliceTracker::Begin(int64_t timestamp,
                                           TrackId track_id,
                                           StringId category,
                                           StringId raw_name,
                                           SetArgsCallback args_callback) {
  const StringId name =
      context_->slice_translation_table->TranslateName(raw_name);
  tables::SliceTable::Row row(timestamp, kPendingDuration, track_id, category,
                              name);
  return StartSlice(
      timestamp, row.dur, track_id, args_callback, [this, &row]() {
        return context_->storage->mutable_slice_table()->Insert(row).id;
      });
}

void SliceTracker::BeginLegacyUnnestable(tables::SliceTable::Row row,
                                         SetArgsCallback args_callback) {
  if (row.name) {
    row.name = context_->slice_translation_table->TranslateName(*row.name);
  }

  // Ensure that the duration is pending for this row.
  // TODO(lalitm): change this to eventually use null instead of -1.
  row.dur = kPendingDuration;

  // Double check that if we've seen this track in the past, it was also
  // marked as unnestable then.
#if PERFETTO_DCHECK_IS_ON()
  auto* it = stacks_.Find(row.track_id);
  PERFETTO_DCHECK(!it || it->is_legacy_unnestable);
#endif

  // Ensure that StartSlice knows that this track is unnestable.
  stacks_[row.track_id].is_legacy_unnestable = true;

  StartSlice(row.ts, row.dur, row.track_id, args_callback, [this, &row]() {
    return context_->storage->mutable_slice_table()->Insert(row).id;
  });
}

std::optional<SliceId> SliceTracker::Scoped(int64_t timestamp,
                                            TrackId track_id,
                                            StringId category,
                                            StringId raw_name,
                                            int64_t duration,
                                            SetArgsCallback args_callback) {
  if (duration < 0) {
    context_->import_logs_tracker->RecordParserError(
        stats::slice_negative_duration, timestamp);
    return std::nullopt;
  }

  const StringId name =
      context_->slice_translation_table->TranslateName(raw_name);
  tables::SliceTable::Row row(timestamp, duration, track_id, category, name);
  return StartSlice(
      timestamp, row.dur, track_id, args_callback, [this, &row]() {
        return context_->storage->mutable_slice_table()->Insert(row).id;
      });
}

std::optional<SliceId> SliceTracker::End(int64_t timestamp,
                                         TrackId track_id,
                                         StringId category,
                                         StringId raw_name,
                                         SetArgsCallback args_callback) {
  const StringId name =
      context_->slice_translation_table->TranslateName(raw_name);
  auto finder = [this, category, name](const SlicesStack& stack) {
    return MatchingIncompleteSliceIndex(stack, name, category);
  };
  return CompleteSlice(timestamp, track_id, args_callback, finder);
}

std::optional<uint32_t> SliceTracker::AddArgs(TrackId track_id,
                                              StringId category,
                                              StringId name,
                                              SetArgsCallback args_callback) {
  auto* it = stacks_.Find(track_id);
  if (!it)
    return std::nullopt;

  auto& stack = it->slice_stack;
  if (stack.empty())
    return std::nullopt;

  auto* slices = context_->storage->mutable_slice_table();
  std::optional<uint32_t> stack_idx =
      MatchingIncompleteSliceIndex(stack, name, category);
  if (!stack_idx.has_value())
    return std::nullopt;

  tables::SliceTable::RowNumber num = stack[*stack_idx].row;
  tables::SliceTable::RowReference ref = num.ToRowReference(slices);
  PERFETTO_DCHECK(ref.dur() == kPendingDuration);

  // Add args to current pending slice.
  ArgsTracker* tracker = &stack[*stack_idx].args_tracker;
  auto bound_inserter = tracker->AddArgsTo(ref.id());
  args_callback(&bound_inserter);
  return num.row_number();
}

std::optional<SliceId> SliceTracker::StartSlice(
    int64_t timestamp,
    int64_t duration,
    TrackId track_id,
    SetArgsCallback args_callback,
    std::function<SliceId()> inserter) {
  auto& track_info = stacks_[track_id];
  auto& stack = track_info.slice_stack;

  if (track_info.is_legacy_unnestable) {
    PERFETTO_DCHECK(stack.size() <= 1);

    track_info.legacy_unnestable_begin_count++;
    track_info.legacy_unnestable_last_begin_ts = timestamp;

    // If this is an unnestable track, don't start a new slice if one already
    // exists.
    if (!stack.empty()) {
      return std::nullopt;
    }
  }

  auto* slices = context_->storage->mutable_slice_table();
  if (!MaybeCloseStack(timestamp, duration, stack, track_id)) {
    return std::nullopt;
  }

  size_t depth = stack.size();

  std::optional<tables::SliceTable::RowReference> parent_ref =
      depth == 0 ? std::nullopt
                 : std::make_optional(stack.back().row.ToRowReference(slices));
  std::optional<tables::SliceTable::Id> parent_id =
      parent_ref ? std::make_optional(parent_ref->id()) : std::nullopt;

  SliceId id = inserter();
  tables::SliceTable::RowReference ref = *slices->FindById(id);
  if (depth >= kMaxDepth) {
    auto parent_name = context_->storage->GetString(
        parent_ref->name().value_or(kNullStringId));
    auto name =
        context_->storage->GetString(ref.name().value_or(kNullStringId));
    PERFETTO_DLOG("Last slice: %s", parent_name.c_str());
    PERFETTO_DLOG("Current slice: %s", name.c_str());
    PERFETTO_DFATAL("Slices with too large depth found.");
    return std::nullopt;
  }
  StackPush(track_id, ref);

  // Post fill all the relevant columns. All the other columns should have
  // been filled by the inserter.
  ref.set_depth(static_cast<uint32_t>(depth));
  if (parent_id)
    ref.set_parent_id(*parent_id);

  if (args_callback) {
    auto bound_inserter = stack.back().args_tracker.AddArgsTo(id);
    args_callback(&bound_inserter);
  }
  return id;
}

std::optional<SliceId> SliceTracker::CompleteSlice(
    int64_t timestamp,
    TrackId track_id,
    SetArgsCallback args_callback,
    std::function<std::optional<uint32_t>(const SlicesStack&)> finder) {
  auto* it = stacks_.Find(track_id);
  if (!it)
    return std::nullopt;

  TrackInfo& track_info = *it;
  SlicesStack& stack = track_info.slice_stack;
  if (!MaybeCloseStack(timestamp, kPendingDuration, stack, track_id)) {
    return std::nullopt;
  }
  if (stack.empty()) {
    return std::nullopt;
  }

  auto* slices = context_->storage->mutable_slice_table();
  std::optional<uint32_t> stack_idx = finder(stack);

  // If we are trying to close slices that are not open on the stack (e.g.,
  // slices that began before tracing started), bail out.
  if (!stack_idx)
    return std::nullopt;

  const auto& slice_info = stack[stack_idx.value()];

  tables::SliceTable::RowReference ref = slice_info.row.ToRowReference(slices);
  PERFETTO_DCHECK(ref.dur() == kPendingDuration);
  ref.set_dur(timestamp - ref.ts());

  ArgsTracker& tracker = stack[stack_idx.value()].args_tracker;
  if (args_callback) {
    auto bound_inserter = tracker.AddArgsTo(ref.id());
    args_callback(&bound_inserter);
  }

  // Add the legacy unnestable args if they exist.
  if (track_info.is_legacy_unnestable) {
    auto bound_inserter = tracker.AddArgsTo(ref.id());
    bound_inserter.AddArg(
        legacy_unnestable_begin_count_string_id_,
        Variadic::Integer(track_info.legacy_unnestable_begin_count));
    bound_inserter.AddArg(
        legacy_unnestable_last_begin_ts_string_id_,
        Variadic::Integer(track_info.legacy_unnestable_last_begin_ts));
  }

  // If this slice is the top slice on the stack, pop it off.
  if (*stack_idx == stack.size() - 1) {
    StackPop(track_id);
  }
  return ref.id();
}

// Returns the first incomplete slice in the stack with matching name and
// category. We assume null category/name matches everything. Returns
// std::nullopt if no matching slice is found.
std::optional<uint32_t> SliceTracker::MatchingIncompleteSliceIndex(
    const SlicesStack& stack,
    StringId name,
    StringId category) {
  auto* slices = context_->storage->mutable_slice_table();
  for (int i = static_cast<int>(stack.size()) - 1; i >= 0; i--) {
    tables::SliceTable::RowReference ref =
        stack[static_cast<size_t>(i)].row.ToRowReference(slices);
    if (ref.dur() != kPendingDuration)
      continue;
    std::optional<StringId> other_category = ref.category();
    if (!category.is_null() && (!other_category || other_category->is_null() ||
                                category != other_category)) {
      continue;
    }
    std::optional<StringId> other_name = ref.name();
    if (!name.is_null() && other_name && !other_name->is_null() &&
        name != other_name) {
      continue;
    }
    return static_cast<uint32_t>(i);
  }
  return std::nullopt;
}

void SliceTracker::MaybeAddTranslatableArgs(SliceInfo& slice_info) {
  if (!slice_info.args_tracker.NeedsTranslation(
          *context_->args_translation_table)) {
    return;
  }
  const auto& table = context_->storage->slice_table();
  tables::SliceTable::ConstRowReference ref =
      slice_info.row.ToRowReference(table);
  translatable_args_.emplace_back(TranslatableArgs{
      ref.id(),
      std::move(slice_info.args_tracker)
          .ToCompactArgSet(table.dataframe(),
                           tables::SliceTable::ColumnIndex::arg_set_id,
                           slice_info.row.row_number())});
}

void SliceTracker::FlushPendingSlices() {
  // Clear the remaining stack entries. This ensures that any pending args are
  // written to the storage. We don't close any slices with kPendingDuration so
  // that the UI can still distinguish such "incomplete" slices.
  //
  // TODO(eseckler): Reconsider whether we want to close pending slices by
  // setting their duration to |trace_end - event_start|. Might still want some
  // additional way of flagging these events as "incomplete" to the UI.

  // Make sure that args for all incomplete slice are translated.
  for (auto it = stacks_.GetIterator(); it; ++it) {
    auto& track_info = it.value();
    for (auto& slice_info : track_info.slice_stack) {
      MaybeAddTranslatableArgs(slice_info);
    }
  }

  // Translate and flush all pending args.
  for (const auto& translatable_arg : translatable_args_) {
    ArgsTracker args_tracker(context_);
    auto bound_inserter = args_tracker.AddArgsTo(translatable_arg.slice_id);
    context_->args_translation_table->TranslateArgs(
        translatable_arg.compact_arg_set, bound_inserter);
  }
  translatable_args_.clear();

  stacks_.Clear();
}

void SliceTracker::SetOnSliceBeginCallback(OnSliceBeginCallback callback) {
  on_slice_begin_callback_ = callback;
}

std::optional<SliceId> SliceTracker::GetTopmostSliceOnTrack(
    TrackId track_id) const {
  const auto* iter = stacks_.Find(track_id);
  if (!iter)
    return std::nullopt;
  const auto& stack = iter->slice_stack;
  if (stack.empty())
    return std::nullopt;
  const auto& slice = context_->storage->slice_table();
  return stack.back().row.ToRowReference(slice).id();
}

bool SliceTracker::MaybeCloseStack(int64_t new_ts,
                                   int64_t new_dur,
                                   const SlicesStack& stack,
                                   TrackId track_id) {
  auto* slices = context_->storage->mutable_slice_table();
  bool incomplete_descendent = false;
  for (int i = static_cast<int>(stack.size()) - 1; i >= 0; i--) {
    tables::SliceTable::RowReference ref =
        stack[static_cast<size_t>(i)].row.ToRowReference(slices);

    int64_t start_ts = ref.ts();
    int64_t dur = ref.dur();
    int64_t end_ts = start_ts + dur;
    if (dur == kPendingDuration) {
      incomplete_descendent = true;
      continue;
    }

    if (incomplete_descendent) {
      PERFETTO_DCHECK(new_ts >= start_ts);

      // Only process slices if the ts is past the end of the slice.
      if (new_ts <= end_ts)
        continue;

      // This usually happens because we have two slices that are partially
      // overlapping.
      // [  slice  1    ]
      //          [     slice 2     ]
      // This is invalid in chrome and should be fixed. Duration events should
      // either be nested or disjoint, never partially intersecting.
      // KI: if tracing both binder and system calls on android, "binder reply"
      // slices will try to escape the enclosing sys_ioctl.
      PERFETTO_DLOG(
          "Incorrect ordering of begin/end slice events. "
          "Truncating incomplete descendants to the end of slice "
          "%s[%" PRId64 ", %" PRId64 "] due to an event at ts=%" PRId64 ".",
          context_->storage->GetString(ref.name().value_or(kNullStringId))
              .c_str(),
          start_ts, end_ts, new_ts);
      context_->storage->IncrementStats(stats::misplaced_end_event);

      // Every slice below this one should have a pending duration. Update
      // of them to have the end ts of the current slice and pop them
      // all off.
      for (int j = static_cast<int>(stack.size()) - 1; j > i; --j) {
        tables::SliceTable::RowReference child_ref =
            stack[static_cast<size_t>(j)].row.ToRowReference(slices);
        PERFETTO_DCHECK(child_ref.dur() == kPendingDuration);
        child_ref.set_dur(end_ts - child_ref.ts());
        StackPop(track_id);
      }

      // Also pop the current row itself and reset the incomplete flag.
      StackPop(track_id);
      incomplete_descendent = false;

      continue;
    }

    // Slices that have ended before the new slice begins can be popped from the
    // stack.
    bool ends_before = end_ts < new_ts;

    // If a slice ends at exactly the same timestamp as another slice, there are
    // multiple cases to consider:
    // 1) previous is a slice, current is a instant.
    // 2) previous is a slice, current is a slice
    // 3) previous is a instant, current is a slice
    // 4) previous is a instant, current is a instant.
    //
    // In general, we follow the principle of: intervals are closed on left and
    // open on right. For instants, this really means they only "interfere"
    // with other instants.
    //
    // Case 1) we want to pop.
    // Case 2) we want to pop.
    // Case 3) we want to pop.
    // Case 4) we want to keep (instants "stack" on top of each other).
    bool ends_same_and_should_drop =
        end_ts == new_ts && !(dur == 0 && new_dur == 0);

    if (ends_before || ends_same_and_should_drop) {
      StackPop(track_id);
      continue;
    }

    if (new_dur == kPendingDuration) {
      // If we don't have a duration, nothing to close.
      continue;
    }

    // This is a sanity check for invalid nesting. This can happen in cases
    // like the following:
    // [  slice  1    ]
    //          [     slice 2     ]
    // This is invalid stacking by the producer and should be fixed. Duration
    // events should either be nested or disjoint, never partially intersecting.
    if (new_ts < end_ts && new_ts + new_dur > end_ts) {
      context_->storage->IncrementStats(
          stats::slice_drop_overlapping_complete_event);
      return false;
    }
  }
  return true;
}

void SliceTracker::StackPop(TrackId track_id) {
  auto& stack = stacks_[track_id].slice_stack;
  MaybeAddTranslatableArgs(stack.back());
  stack.pop_back();
}

void SliceTracker::StackPush(TrackId track_id,
                             tables::SliceTable::RowReference ref) {
  stacks_[track_id].slice_stack.push_back(
      SliceInfo{ref.ToRowNumber(), ArgsTracker(context_)});
  if (on_slice_begin_callback_) {
    on_slice_begin_callback_(track_id, ref.id());
  }
}

}  // namespace perfetto::trace_processor
