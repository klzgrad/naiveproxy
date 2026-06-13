/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/pixel_display_tracker.h"

#include <cmath>
#include <cstdint>

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/dpu.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

PixelDisplayTracker::PixelDisplayTracker(TraceProcessorContext* context)
    : context_(context),
      frame_start_timeout_name_(
          context->storage->InternString("frame_start_timeout")),
      frame_done_timeout_name_(
          context->storage->InternString("frame_done_timeout")),
      frame_start_missing_name_(
          context->storage->InternString("frame_start_missing")),
      frame_done_missing_name_(
          context->storage->InternString("frame_done_missing")),

      vblank_irq_enable_name_(
          context_->storage->InternString("disp_vblank_irq_enable")),

      display_id_arg_(context_->storage->InternString("display_id")),
      output_id_arg_(context_->storage->InternString("output_id")),
      frames_pending_arg_(context_->storage->InternString("frames_pending")),
      te_count_arg_(context_->storage->InternString("te_count")),
      during_disable_arg_(context_->storage->InternString("during_disable")) {}

void PixelDisplayTracker::ParseDpuDispFrameStartTimeout(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  protos::pbzero::DpuDispFrameStartTimeoutFtraceEvent::Decoder ex(blob);
  static constexpr auto kBluePrint = tracks::SliceBlueprint(
      "disp_frame_start_timeout",
      tracks::DimensionBlueprints(
          tracks::UintDimensionBlueprint("panel_index")),
      tracks::FnNameBlueprint([](uint32_t panel_index) {
        return base::StackString<256>("frame_start_timeout[%u]", panel_index);
      }));

  TrackId track_id = context_->track_tracker->InternTrack(
      kBluePrint, tracks::Dimensions(ex.output_id()));
  StringId slice_name_id = frame_start_timeout_name_;

  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, slice_name_id, 0,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(display_id_arg_, Variadic::Integer(ex.display_id()));
        inserter->AddArg(output_id_arg_, Variadic::Integer(ex.output_id()));
        inserter->AddArg(frames_pending_arg_,
                         Variadic::Integer(ex.frames_pending()));
        inserter->AddArg(te_count_arg_, Variadic::Integer(ex.te_count()));
      });
}

void PixelDisplayTracker::ParseDpuDispFrameDoneTimeout(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  protos::pbzero::DpuDispFrameDoneTimeoutFtraceEvent::Decoder ex(blob);
  static constexpr auto kBluePrint = tracks::SliceBlueprint(
      "disp_frame_done_timeout",
      tracks::DimensionBlueprints(
          tracks::UintDimensionBlueprint("panel_index")),
      tracks::FnNameBlueprint([](uint32_t panel_index) {
        return base::StackString<256>("frame_done_timeout[%u]", panel_index);
      }));

  TrackId track_id = context_->track_tracker->InternTrack(
      kBluePrint, tracks::Dimensions(ex.output_id()));
  StringId slice_name_id = frame_done_timeout_name_;

  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, slice_name_id, 0,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(display_id_arg_, Variadic::Integer(ex.display_id()));
        inserter->AddArg(output_id_arg_, Variadic::Integer(ex.output_id()));
        inserter->AddArg(frames_pending_arg_,
                         Variadic::Integer(ex.frames_pending()));
        inserter->AddArg(te_count_arg_, Variadic::Integer(ex.te_count()));
        inserter->AddArg(during_disable_arg_,
                         Variadic::Integer(ex.during_disable()));
      });
}

void PixelDisplayTracker::ParseDpuDispFrameStartMissing(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  protos::pbzero::DpuDispFrameStartMissingFtraceEvent::Decoder ex(blob);
  static constexpr auto kBluePrint = tracks::SliceBlueprint(
      "disp_frame_start_missing",
      tracks::DimensionBlueprints(
          tracks::UintDimensionBlueprint("panel_index")),
      tracks::FnNameBlueprint([](uint32_t panel_index) {
        return base::StackString<256>("frame_start_missing[%u]", panel_index);
      }));

  TrackId track_id = context_->track_tracker->InternTrack(
      kBluePrint, tracks::Dimensions(ex.output_id()));
  StringId slice_name_id = frame_start_missing_name_;

  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, slice_name_id, 0,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(display_id_arg_, Variadic::Integer(ex.display_id()));
        inserter->AddArg(output_id_arg_, Variadic::Integer(ex.output_id()));
        inserter->AddArg(frames_pending_arg_,
                         Variadic::Integer(ex.frames_pending()));
        inserter->AddArg(te_count_arg_, Variadic::Integer(ex.te_count()));
      });
}

void PixelDisplayTracker::ParseDpuDispFrameDoneMissing(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  protos::pbzero::DpuDispFrameDoneMissingFtraceEvent::Decoder ex(blob);
  static constexpr auto kBluePrint = tracks::SliceBlueprint(
      "disp_frame_done_missing",
      tracks::DimensionBlueprints(
          tracks::UintDimensionBlueprint("panel_index")),
      tracks::FnNameBlueprint([](uint32_t panel_index) {
        return base::StackString<256>("frame_done_missing[%u]", panel_index);
      }));

  TrackId track_id = context_->track_tracker->InternTrack(
      kBluePrint, tracks::Dimensions(ex.output_id()));
  StringId slice_name_id = frame_done_missing_name_;

  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, slice_name_id, 0,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(display_id_arg_, Variadic::Integer(ex.display_id()));
        inserter->AddArg(output_id_arg_, Variadic::Integer(ex.output_id()));
        inserter->AddArg(frames_pending_arg_,
                         Variadic::Integer(ex.frames_pending()));
        inserter->AddArg(te_count_arg_, Variadic::Integer(ex.te_count()));
      });
}

void PixelDisplayTracker::ParseDpuDispVblankIrqEnable(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  protos::pbzero::DpuDispVblankIrqEnableFtraceEvent::Decoder ex(blob);

  static constexpr auto kBlueprint = tracks::SliceBlueprint(
      "disp_vblank_irq_enable",
      tracks::DimensionBlueprints(tracks::UintDimensionBlueprint("display_id")),
      tracks::FnNameBlueprint([](uint32_t display_id) {
        return base::StackString<256>("vblank_irq_en[%u]", display_id);
      }));

  TrackId track_id = context_->track_tracker->InternTrack(
      kBlueprint, tracks::Dimensions(ex.output_id()));
  if (ex.enable()) {
    context_->slice_tracker->Begin(
        timestamp, track_id, kNullStringId, vblank_irq_enable_name_,
        [&](ArgsTracker::BoundInserter* inserter) {
          inserter->AddArg(display_id_arg_, Variadic::Integer(ex.id()));
          inserter->AddArg(output_id_arg_, Variadic::Integer(ex.output_id()));
        });
  } else {
    context_->slice_tracker->End(timestamp, track_id);
  }
}
}  // namespace perfetto::trace_processor
