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
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/slice_translation_table.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

SliceTracker::SliceTracker(TraceProcessorContext* context)
    : legacy_unnestable_begin_count_string_id_(
          context->storage->InternString("legacy_unnestable_begin_count")),
      legacy_unnestable_last_begin_ts_string_id_(
          context->storage->InternString("legacy_unnestable_last_begin_ts")),
      context_(context),
      overlap_start_key_(context->storage->InternString("overlap_start")),
      overlap_end_key_(context->storage->InternString("overlap_end")),
      overlap_conflicting_name_key_(
          context->storage->InternString("conflicting_slice_name")),
      overlap_conflicting_ts_key_(
          context->storage->InternString("conflicting_slice_ts")),
      overlap_conflicting_dur_key_(
          context->storage->InternString("conflicting_slice_dur")),
      max_depth_parent_name_key_(
          context->storage->InternString("parent_slice_name")),
      max_depth_current_name_key_(
          context->storage->InternString("current_slice_name")) {}

void SliceTracker::AddOverlapArgs(const OverlapInfo& info,
                                  ArgsTracker::BoundInserter& inserter) const {
  inserter.AddArg(overlap_start_key_, Variadic::Integer(info.start));
  inserter.AddArg(overlap_end_key_, Variadic::Integer(info.end));
  inserter.AddArg(overlap_conflicting_name_key_,
                  Variadic::String(info.conflicting_name));
  inserter.AddArg(overlap_conflicting_ts_key_,
                  Variadic::Integer(info.conflicting_ts));
  inserter.AddArg(overlap_conflicting_dur_key_,
                  Variadic::Integer(info.conflicting_dur));
}

void SliceTracker::AddMaxDepthArgs(StringId parent_name,
                                   StringId current_name,
                                   ArgsTracker::BoundInserter& inserter) const {
  inserter.AddArg(max_depth_parent_name_key_, Variadic::String(parent_name));
  inserter.AddArg(max_depth_current_name_key_, Variadic::String(current_name));
}

SliceTracker::~SliceTracker() {
  FlushPendingSlices();
}

void SliceTracker::RecordSliceNegativeDuration(int64_t timestamp) {
  context_->import_logs_tracker->RecordParserLog(stats::slice_negative_duration,
                                                 timestamp);
}

bool SliceTracker::PrepareStartSlice(TrackInfo& track_info,
                                     int64_t timestamp,
                                     int64_t duration,
                                     std::optional<OverlapInfo>* overlap_out) {
  if (track_info.is_legacy_unnestable) {
    PERFETTO_DCHECK(track_info.slice_stack.size() <= 1);

    track_info.legacy_unnestable_begin_count++;
    track_info.legacy_unnestable_last_begin_ts = timestamp;

    // If this is an unnestable track, don't start a new slice if one already
    // exists.
    if (!track_info.slice_stack.empty()) {
      return false;
    }
  }

  return MaybeCloseStack(track_info, timestamp, duration, overlap_out);
}

void SliceTracker::LogMaxDepthExceeded(const SliceInfo& parent,
                                       StringId name,
                                       int64_t timestamp) {
  auto* slices = context_->storage->mutable_slice_table();
  StringId parent_name_id =
      parent.row.ToRowReference(slices).name().value_or(kNullStringId);
  StringId current_name_id = name.is_null() ? kNullStringId : name;

  context_->import_logs_tracker->RecordParserLog(
      stats::slice_max_depth_exceeded, timestamp,
      [this, parent_name_id,
       current_name_id](ArgsTracker::BoundInserter& inserter) {
        AddMaxDepthArgs(parent_name_id, current_name_id, inserter);
      });
}

SliceTracker::StartedSlice SliceTracker::StartSlice(
    int64_t timestamp,
    int64_t duration,
    TrackId track_id,
    StringId category,
    StringId raw_name,
    bool want_args,
    std::optional<OverlapInfo>* overlap_out) {
  const StringId name =
      context_->slice_translation_table->TranslateName(raw_name);

  // Resolve the track once and thread it through; nothing below rehashes.
  TrackInfo& track_info = GetOrCreateTrackInfo(track_id);
  if (!PrepareStartSlice(track_info, timestamp, duration, overlap_out))
    return {};

  auto& stack = track_info.slice_stack;
  size_t depth = stack.size();
  if (PERFETTO_UNLIKELY(depth >= kMaxDepth)) {
    LogMaxDepthExceeded(stack.back(), name, timestamp);
    return {};
  }

  // Set depth/parent pre-insert so they ride the single table insert.
  auto* slices = context_->storage->mutable_slice_table();
  tables::SliceTable::Row row(timestamp, duration, track_id, category, name);
  row.depth = static_cast<uint32_t>(depth);
  if (depth != 0)
    row.parent_id = stack.back().row.ToRowReference(slices).id();

  auto inserted = slices->Insert(std::move(row));
  StackPush(track_info, track_id, inserted.row_number, inserted.id);

  StartedSlice result;
  result.id = inserted.id;
  if (want_args)
    result.inserter = GetArgsInserter(stack.back(), inserted.id);
  return result;
}

SliceTracker::EndedSlice SliceTracker::CompleteSliceBegin(int64_t timestamp,
                                                          TrackId track_id,
                                                          StringId category,
                                                          StringId raw_name,
                                                          bool want_args) {
  const StringId name =
      context_->slice_translation_table->TranslateName(raw_name);

  EndedSlice result;
  auto* it = FindTrackInfo(track_id);
  if (!it)
    return result;

  TrackInfo& track_info = *it;
  auto& stack = track_info.slice_stack;
  if (!MaybeCloseStack(track_info, timestamp, kPendingDuration,
                       /*overlap_out=*/nullptr)) {
    return result;
  }
  if (stack.empty())
    return result;

  std::optional<uint32_t> stack_idx =
      MatchingIncompleteSliceIndex(stack, name, category);

  // If we are trying to close slices that are not open on the stack (e.g.,
  // slices that began before tracing started), bail out.
  if (!stack_idx)
    return result;

  auto* slices = context_->storage->mutable_slice_table();
  SliceInfo& slice_info = stack[*stack_idx];
  tables::SliceTable::RowReference ref = slice_info.row.ToRowReference(slices);
  PERFETTO_DCHECK(ref.dur() == kPendingDuration);
  ref.set_dur(timestamp - ref.ts());

  result.id = ref.id();
  result.state.track_info = &track_info;
  result.state.stack_idx = *stack_idx;
  if (want_args)
    result.inserter = GetArgsInserter(slice_info, ref.id());
  return result;
}

std::optional<uint32_t> SliceTracker::AddArgsImpl(TrackId track_id,
                                                  StringId category,
                                                  StringId name,
                                                  bool want_args,
                                                  ArgsInserter** inserter) {
  auto* it = FindTrackInfo(track_id);
  if (!it)
    return std::nullopt;

  auto& stack = it->slice_stack;
  if (stack.empty())
    return std::nullopt;

  auto* slices = context_->storage->mutable_slice_table();
  std::optional<uint32_t> stack_idx =
      MatchingIncompleteSliceIndex(stack, name, category);
  if (!stack_idx)
    return std::nullopt;

  SliceInfo& slice_info = stack[*stack_idx];
  tables::SliceTable::RowNumber num = slice_info.row;
  tables::SliceTable::RowReference ref = num.ToRowReference(slices);
  PERFETTO_DCHECK(ref.dur() == kPendingDuration);

  if (want_args)
    *inserter = GetArgsInserter(slice_info, ref.id());
  return num.row_number();
}

void SliceTracker::CompleteSliceFinalize(const CompleteSliceState& state) {
  TrackInfo& track_info = *state.track_info;
  auto& stack = track_info.slice_stack;
  SliceInfo& slice_info = stack[state.stack_idx];

  // Add the legacy unnestable args if they exist.
  if (track_info.is_legacy_unnestable)
    AddLegacyUnnestableArgs(slice_info, track_info);

  // If this slice is the top slice on the stack, pop it off.
  if (state.stack_idx == stack.size() - 1)
    StackPop(track_info);
}

ArgsInserter* SliceTracker::GetArgsInserter(SliceInfo& slice_info, SliceId id) {
  // Lazily bind the slice's inserter; reused so all its args merge into one
  // set.
  if (!slice_info.args) {
    slice_info.args = ArgsTracker(context_).AddArgsTo(id);
  }
  return &*slice_info.args;
}

void SliceTracker::AddLegacyUnnestableArgs(SliceInfo& slice_info,
                                           const TrackInfo& track_info) {
  auto* slices = context_->storage->mutable_slice_table();
  SliceId id = slice_info.row.ToRowReference(slices).id();
  ArgsInserter* inserter = GetArgsInserter(slice_info, id);
  inserter->AddArg(legacy_unnestable_begin_count_string_id_,
                   Variadic::Integer(track_info.legacy_unnestable_begin_count));
  inserter->AddArg(
      legacy_unnestable_last_begin_ts_string_id_,
      Variadic::Integer(track_info.legacy_unnestable_last_begin_ts));
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
  PERFETTO_DCHECK(slice_info.args);
  if (!slice_info.args->NeedsTranslation(*context_->args_translation_table)) {
    return;
  }
  const auto& table = context_->storage->slice_table();
  tables::SliceTable::ConstRowReference ref =
      slice_info.row.ToRowReference(table);
  translatable_args_.emplace_back(TranslatableArgs{
      ref.id(), std::move(*slice_info.args).ToCompactArgSet()});
}

void SliceTracker::FlushPendingSlices() {
  // Clear the remaining stack entries. This ensures that any pending args are
  // written to the storage. We don't close any slices with kPendingDuration so
  // that the UI can still distinguish such "incomplete" slices.
  //
  // TODO(eseckler): Reconsider whether we want to close pending slices by
  // setting their duration to |trace_end - event_start|. Might still want some
  // additional way of flagging these events as "incomplete" to the UI.

  // Defer translatable args; the rest commit when the stacks are cleared below.
  for (auto it = stacks_.GetIterator(); it; ++it) {
    auto& track_info = it.value();
    for (auto& slice_info : track_info.slice_stack) {
      if (slice_info.args)
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
  on_slice_begin_callback_ = std::move(callback);
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

bool SliceTracker::MaybeCloseStack(TrackInfo& track_info,
                                   int64_t new_ts,
                                   int64_t new_dur,
                                   std::optional<OverlapInfo>* overlap_out) {
  auto& stack = track_info.slice_stack;
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
      context_->stats_tracker->IncrementStats(stats::misplaced_end_event);

      // Every slice below this one should have a pending duration. Update
      // of them to have the end ts of the current slice and pop them
      // all off.
      for (int j = static_cast<int>(stack.size()) - 1; j > i; --j) {
        tables::SliceTable::RowReference child_ref =
            stack[static_cast<size_t>(j)].row.ToRowReference(slices);
        PERFETTO_DCHECK(child_ref.dur() == kPendingDuration);
        child_ref.set_dur(end_ts - child_ref.ts());
        StackPop(track_info);
      }

      // Also pop the current row itself and reset the incomplete flag.
      StackPop(track_info);
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
      StackPop(track_info);
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
      // The incoming slice [new_ts, new_ts + new_dur) starts inside the
      // already-open slice [start_ts, end_ts) but ends after it, so the shared
      // (ambiguous) region is [new_ts, end_ts).
      OverlapInfo info{new_ts, end_ts, ref.name().value_or(kNullStringId),
                       start_ts, dur};
      if (overlap_out) {
        // The caller wants to recover (e.g. spill onto an overflow track) and
        // will do its own logging; just report the details.
        *overlap_out = info;
      } else {
        // Nobody can recover this slice, so drop it but log the offending
        // events (rather than only bumping a stat) so the user can find and fix
        // them.
        context_->import_logs_tracker->RecordParserLog(
            stats::slice_drop_overlapping_complete_event, new_ts,
            [this, info](ArgsTracker::BoundInserter& inserter) {
              AddOverlapArgs(info, inserter);
            });
      }
      return false;
    }
  }
  return true;
}

void SliceTracker::StackPop(TrackInfo& track_info) {
  auto& stack = track_info.slice_stack;
  SliceInfo& info = stack.back();
  if (info.args) {
    // Move translatable args out first (deferred to end-of-trace); reset then
    // commits whatever remains.
    MaybeAddTranslatableArgs(info);
    info.args.reset();
  }
  stack.pop_back();
}

void SliceTracker::StackPush(TrackInfo& track_info,
                             TrackId track_id,
                             tables::SliceTable::RowNumber row_number,
                             SliceId id) {
  track_info.slice_stack.push_back(SliceInfo{row_number, std::nullopt});
  if (on_slice_begin_callback_) {
    on_slice_begin_callback_(track_id, id);
  }
}

}  // namespace perfetto::trace_processor
