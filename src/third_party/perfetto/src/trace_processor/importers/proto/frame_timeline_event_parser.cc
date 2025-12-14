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

#include "src/trace_processor/importers/proto/frame_timeline_event_parser.h"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/trace/android/frame_timeline_event.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {

bool IsBadTimestamp(int64_t ts) {
  // Very small or very large timestamps are likely a mistake.
  // See b/185978397
  constexpr int64_t kBadTimestamp =
      std::numeric_limits<int64_t>::max() - (10LL * 1000 * 1000 * 1000);
  return std::abs(ts) >= kBadTimestamp;
}

StringId JankTypeBitmaskToStringId(TraceProcessorContext* context,
                                   int32_t jank_type) {
  if (jank_type == FrameTimelineEvent::JANK_UNSPECIFIED)
    return context->storage->InternString("Unspecified");
  if (jank_type == FrameTimelineEvent::JANK_NONE)
    return context->storage->InternString("None");

  std::vector<std::string> jank_reasons;
  if (jank_type & FrameTimelineEvent::JANK_SF_SCHEDULING)
    jank_reasons.emplace_back("SurfaceFlinger Scheduling");
  if (jank_type & FrameTimelineEvent::JANK_PREDICTION_ERROR)
    jank_reasons.emplace_back("Prediction Error");
  if (jank_type & FrameTimelineEvent::JANK_DISPLAY_HAL)
    jank_reasons.emplace_back("Display HAL");
  if (jank_type & FrameTimelineEvent::JANK_SF_CPU_DEADLINE_MISSED)
    jank_reasons.emplace_back("SurfaceFlinger CPU Deadline Missed");
  if (jank_type & FrameTimelineEvent::JANK_SF_GPU_DEADLINE_MISSED)
    jank_reasons.emplace_back("SurfaceFlinger GPU Deadline Missed");
  if (jank_type & FrameTimelineEvent::JANK_APP_DEADLINE_MISSED)
    jank_reasons.emplace_back("App Deadline Missed");
  if (jank_type & FrameTimelineEvent::JANK_APP_RESYNCED_JITTER)
    jank_reasons.emplace_back("App Resynced Jitter");
  if (jank_type & FrameTimelineEvent::JANK_BUFFER_STUFFING)
    jank_reasons.emplace_back("Buffer Stuffing");
  if (jank_type & FrameTimelineEvent::JANK_UNKNOWN)
    jank_reasons.emplace_back("Unknown Jank");
  if (jank_type & FrameTimelineEvent::JANK_SF_STUFFING)
    jank_reasons.emplace_back("SurfaceFlinger Stuffing");
  if (jank_type & FrameTimelineEvent::JANK_DROPPED)
    jank_reasons.emplace_back("Dropped Frame");
  if (jank_type & FrameTimelineEvent::JANK_NON_ANIMATING)
    jank_reasons.emplace_back("Non Animating");
  if (jank_type & FrameTimelineEvent::JANK_DISPLAY_NOT_ON)
    jank_reasons.emplace_back("Display not ON");

  std::string jank_str(
      std::accumulate(jank_reasons.begin(), jank_reasons.end(), std::string(),
                      [](const std::string& l, const std::string& r) {
                        return l.empty() ? r : l + ", " + r;
                      }));
  return context->storage->InternString(base::StringView(jank_str));
}

bool DisplayFrameJanky(int32_t jank_type) {
  if (jank_type == FrameTimelineEvent::JANK_UNSPECIFIED ||
      jank_type == FrameTimelineEvent::JANK_NON_ANIMATING ||
      jank_type == FrameTimelineEvent::JANK_DISPLAY_NOT_ON ||
      jank_type == FrameTimelineEvent::JANK_NONE)
    return false;

  int32_t display_frame_jank_bitmask =
      FrameTimelineEvent::JANK_SF_SCHEDULING |
      FrameTimelineEvent::JANK_PREDICTION_ERROR |
      FrameTimelineEvent::JANK_DISPLAY_HAL |
      FrameTimelineEvent::JANK_SF_CPU_DEADLINE_MISSED |
      FrameTimelineEvent::JANK_SF_GPU_DEADLINE_MISSED;
  return (jank_type & display_frame_jank_bitmask) != 0;
}

bool SurfaceFrameJanky(int32_t jank_type) {
  if (jank_type == FrameTimelineEvent::JANK_UNSPECIFIED ||
      jank_type == FrameTimelineEvent::JANK_NONE ||
      jank_type == FrameTimelineEvent::JANK_NON_ANIMATING ||
      jank_type == FrameTimelineEvent::JANK_DISPLAY_NOT_ON)
    return false;

  int32_t surface_frame_jank_bitmask =
      FrameTimelineEvent::JANK_APP_DEADLINE_MISSED |
      FrameTimelineEvent ::JANK_APP_RESYNCED_JITTER |
      FrameTimelineEvent::JANK_UNKNOWN;
  return (jank_type & surface_frame_jank_bitmask) != 0;
}

bool ValidatePredictionType(TraceProcessorContext* context,
                            int32_t prediction_type) {
  if (prediction_type >= FrameTimelineEvent::PREDICTION_VALID /*1*/ &&
      prediction_type <= FrameTimelineEvent::PREDICTION_UNKNOWN /*3*/)
    return true;
  context->storage->IncrementStats(stats::frame_timeline_event_parser_errors);
  return false;
}

bool ValidatePresentType(TraceProcessorContext* context, int32_t present_type) {
  if (present_type >= FrameTimelineEvent::PRESENT_ON_TIME /*1*/ &&
      present_type <= FrameTimelineEvent::PRESENT_UNKNOWN /*5*/)
    return true;
  context->storage->IncrementStats(stats::frame_timeline_event_parser_errors);
  return false;
}

using ExpectedDisplayFrameStartDecoder =
    protos::pbzero::FrameTimelineEvent::ExpectedDisplayFrameStart::Decoder;
using ActualDisplayFrameStartDecoder =
    protos::pbzero::FrameTimelineEvent::ActualDisplayFrameStart::Decoder;

using ExpectedSurfaceFrameStartDecoder =
    protos::pbzero::FrameTimelineEvent::ExpectedSurfaceFrameStart::Decoder;
using ActualSurfaceFrameStartDecoder =
    protos::pbzero::FrameTimelineEvent::ActualSurfaceFrameStart::Decoder;

using FrameEndDecoder = protos::pbzero::FrameTimelineEvent::FrameEnd::Decoder;

constexpr auto kExpectedBlueprint = TrackCompressor::SliceBlueprint(
    "android_expected_frame_timeline",
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint),
    tracks::StaticNameBlueprint("Expected Timeline"));

constexpr auto kActualBlueprint = TrackCompressor::SliceBlueprint(
    "android_actual_frame_timeline",
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint),
    tracks::StaticNameBlueprint("Actual Timeline"));

}  // namespace

FrameTimelineEventParser::FrameTimelineEventParser(
    TraceProcessorContext* context)
    : context_(context),
      present_type_ids_{
          {context->storage->InternString(
               "Unspecified Present") /* PRESENT_UNSPECIFIED */,
           context->storage->InternString(
               "On-time Present") /* PRESENT_ON_TIME */,
           context->storage->InternString("Late Present") /* PRESENT_LATE */,
           context->storage->InternString("Early Present") /* PRESENT_EARLY */,
           context->storage->InternString(
               "Dropped Frame") /* PRESENT_DROPPED */,
           context->storage->InternString(
               "Unknown Present") /* PRESENT_UNKNOWN */}},
      present_type_experimental_ids_(present_type_ids_),
      prediction_type_ids_{
          {context->storage->InternString(
               "Unspecified Prediction") /* PREDICTION_UNSPECIFIED */,
           context->storage->InternString(
               "Valid Prediction") /* PREDICTION_VALID */,
           context->storage->InternString(
               "Expired Prediction") /* PREDICTION_EXPIRED */,
           context->storage->InternString(
               "Unknown Prediction") /* PREDICTION_UNKNOWN */}},
      jank_severity_type_ids_{{context->storage->InternString("Unknown"),
                               context->storage->InternString("None"),
                               context->storage->InternString("Partial"),
                               context->storage->InternString("Full")}},
      surface_frame_token_id_(
          context->storage->InternString("Surface frame token")),
      display_frame_token_id_(
          context->storage->InternString("Display frame token")),
      present_delay_millis_id_(
          context->storage->InternString("Present Delay (ms) (experimental)")),
      vsync_resynced_jitter_millis_id_(context->storage->InternString(
          "Vsync Resynced Jitter (ms) (experimental)")),
      present_type_id_(context->storage->InternString("Present type")),
      present_type_experimental_id_(
          context->storage->InternString("Present type (experimental)")),
      on_time_finish_id_(context->storage->InternString("On time finish")),
      gpu_composition_id_(context->storage->InternString("GPU composition")),
      jank_type_id_(context->storage->InternString("Jank type")),
      jank_type_experimental_id_(
          context->storage->InternString("Jank type (experimental)")),
      jank_severity_type_id_(
          context->storage->InternString("Jank severity type")),
      jank_severity_score_id_(
          context->storage->InternString("Jank Severity Score (experimental)")),
      layer_name_id_(context->storage->InternString("Layer name")),
      prediction_type_id_(context->storage->InternString("Prediction type")),
      jank_tag_id_(context->storage->InternString("Jank tag")),
      jank_tag_experimental_id_(
          context->storage->InternString("Jank tag (experimental)")),
      is_buffer_id_(context->storage->InternString("Is Buffer?")),
      jank_tag_unspecified_id_(context->storage->InternString("Unspecified")),
      jank_tag_none_id_(context->storage->InternString("No Jank")),
      jank_tag_self_id_(context->storage->InternString("Self Jank")),
      jank_tag_other_id_(context->storage->InternString("Other Jank")),
      jank_tag_dropped_id_(context->storage->InternString("Dropped Frame")),
      jank_tag_buffer_stuffing_id_(
          context->storage->InternString("Buffer Stuffing")),
      jank_tag_sf_stuffing_id_(
          context->storage->InternString("SurfaceFlinger Stuffing")),
      jank_tag_none_animating_id_(
          context->storage->InternString("Non Animating")),
      jank_tag_display_not_on_id_(
          context->storage->InternString("Display not ON")) {}

void FrameTimelineEventParser::ParseExpectedDisplayFrameStart(int64_t timestamp,
                                                              ConstBytes blob) {
  ExpectedDisplayFrameStartDecoder event(blob);

  if (!event.has_cookie() || !event.has_token() || !event.has_pid()) {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
    return;
  }

  int64_t cookie = event.cookie();
  int64_t token = event.token();
  StringId name_id =
      context_->storage->InternString(base::StringView(std::to_string(token)));
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(event.pid()));
  cookie_map_[cookie] = std::make_pair(upid, TrackType::kExpected);

  TrackId track_id = context_->track_compressor->InternBegin(
      kExpectedBlueprint, tracks::Dimensions(upid), cookie);
  context_->slice_tracker->Begin(
      timestamp, track_id, kNullStringId, name_id,
      [this, token](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(display_frame_token_id_, Variadic::Integer(token));
      });
}

StringId FrameTimelineEventParser::CalculateDisplayFrameJankTag(
    int32_t jank_type) {
  StringId jank_tag;
  if (jank_type == FrameTimelineEvent::JANK_UNSPECIFIED) {
    jank_tag = jank_tag_unspecified_id_;
  } else if (DisplayFrameJanky(jank_type)) {
    jank_tag = jank_tag_self_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_SF_STUFFING) {
    jank_tag = jank_tag_sf_stuffing_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_DROPPED) {
    jank_tag = jank_tag_dropped_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_NON_ANIMATING) {
    jank_tag = jank_tag_none_animating_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_DISPLAY_NOT_ON) {
    jank_tag = jank_tag_display_not_on_id_;
  } else {
    jank_tag = jank_tag_none_id_;
  }

  return jank_tag;
}

void FrameTimelineEventParser::ParseActualDisplayFrameStart(int64_t timestamp,
                                                            ConstBytes blob) {
  ActualDisplayFrameStartDecoder event(blob);

  if (!event.has_cookie() || !event.has_token() || !event.has_pid()) {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
    return;
  }

  int64_t cookie = event.cookie();
  int64_t token = event.token();
  double jank_severity_score = static_cast<double>(event.jank_severity_score());
  double present_delay_millis =
      static_cast<double>(event.present_delay_millis());
  StringId name_id =
      context_->storage->InternString(base::StringView(std::to_string(token)));
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(event.pid()));
  cookie_map_[cookie] = std::make_pair(upid, TrackType::kActual);

  TrackId track_id = context_->track_compressor->InternBegin(
      kActualBlueprint, tracks::Dimensions(upid), cookie);

  // parse present type
  StringId present_type = present_type_ids_[0];
  if (event.has_present_type() &&
      ValidatePresentType(context_, event.present_type())) {
    present_type = present_type_ids_[static_cast<size_t>(event.present_type())];
  }

  // parse present type experimental
  StringId present_type_experimental = present_type_experimental_ids_[0];
  if (event.has_present_type_experimental() &&
      ValidatePresentType(context_, event.present_type_experimental())) {
    present_type_experimental =
        present_type_experimental_ids_[static_cast<size_t>(
            event.present_type_experimental())];
  }

  // parse jank type
  StringId jank_type = JankTypeBitmaskToStringId(context_, event.jank_type());

  // parse jank type experimental
  StringId jank_type_experimental =
      JankTypeBitmaskToStringId(context_, event.jank_type_experimental());

  // parse jank severity type
  StringId jank_severity_type;
  if (event.has_jank_severity_type()) {
    jank_severity_type = jank_severity_type_ids_[static_cast<size_t>(
        event.jank_severity_type())];
  } else {
    // NOTE: Older traces don't have this field. If JANK_NONE use
    // |severity_type| "None", and is not present, use "Unknown".
    jank_severity_type = (event.jank_type() == FrameTimelineEvent::JANK_NONE)
                             ? jank_severity_type_ids_[1]  /* None */
                             : jank_severity_type_ids_[0]; /* Unknown */
  }

  // parse prediction type
  StringId prediction_type = prediction_type_ids_[0];
  if (event.has_prediction_type() &&
      ValidatePredictionType(context_, event.prediction_type())) {
    prediction_type =
        prediction_type_ids_[static_cast<size_t>(event.prediction_type())];
  }

  const StringId jank_tag = CalculateDisplayFrameJankTag(event.jank_type());
  const StringId jank_tag_experimental =
      CalculateDisplayFrameJankTag(event.jank_type_experimental());

  std::optional<SliceId> opt_slice_id = context_->slice_tracker->Begin(
      timestamp, track_id, kNullStringId, name_id,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(display_frame_token_id_, Variadic::Integer(token));
        inserter->AddArg(present_delay_millis_id_,
                         Variadic::Real(present_delay_millis));
        inserter->AddArg(present_type_id_, Variadic::String(present_type));
        inserter->AddArg(present_type_experimental_id_,
                         Variadic::String(present_type_experimental));
        inserter->AddArg(on_time_finish_id_,
                         Variadic::Integer(event.on_time_finish()));
        inserter->AddArg(gpu_composition_id_,
                         Variadic::Integer(event.gpu_composition()));
        inserter->AddArg(jank_type_id_, Variadic::String(jank_type));
        inserter->AddArg(jank_type_experimental_id_,
                         Variadic::String(jank_type_experimental));
        inserter->AddArg(jank_severity_type_id_,
                         Variadic::String(jank_severity_type));
        inserter->AddArg(jank_severity_score_id_,
                         Variadic::Real(jank_severity_score));
        inserter->AddArg(prediction_type_id_,
                         Variadic::String(prediction_type));
        inserter->AddArg(jank_tag_id_, Variadic::String(jank_tag));
        inserter->AddArg(jank_tag_experimental_id_,
                         Variadic::String(jank_tag_experimental));
      });

  // SurfaceFrames will always be parsed before the matching DisplayFrame
  // (since the app works on the frame before SurfaceFlinger does). Because
  // of this it's safe to add all the flow events here and then forget the
  // surface_slice id - we shouldn't see more surfaces_slices that should be
  // connected to this slice after this point.
  auto range = display_token_to_surface_slice_.equal_range(token);
  if (opt_slice_id) {
    for (auto it = range.first; it != range.second; ++it) {
      SliceId surface_slice = it->second;     // App
      SliceId display_slice = *opt_slice_id;  // SurfaceFlinger
      context_->flow_tracker->InsertFlow(surface_slice, display_slice);
    }
  }
  display_token_to_surface_slice_.erase(range.first, range.second);
}

void FrameTimelineEventParser::ParseExpectedSurfaceFrameStart(int64_t timestamp,
                                                              ConstBytes blob) {
  ExpectedSurfaceFrameStartDecoder event(blob);

  if (!event.has_cookie() || !event.has_token() ||
      !event.has_display_frame_token() || !event.has_pid()) {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
    return;
  }

  int64_t cookie = event.cookie();
  int64_t token = event.token();
  int64_t display_frame_token = event.display_frame_token();
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(event.pid()));
  cookie_map_[cookie] = std::make_pair(upid, TrackType::kExpected);

  auto token_set_it = expected_timeline_token_map_.find(upid);
  if (token_set_it != expected_timeline_token_map_.end()) {
    auto& token_set = token_set_it->second;
    if (token_set.find(token) != token_set.end()) {
      // If we already have an expected timeline for a token, the expectations
      // are same for all frames that use the token. No need to add duplicate
      // entries.
      return;
    }
  }
  // This is the first time we are seeing this token for this process. Add to
  // the map.
  expected_timeline_token_map_[upid].insert(token);

  StringId layer_name_id = event.has_layer_name()
                               ? context_->storage->InternString(
                                     base::StringView(event.layer_name()))
                               : kNullStringId;
  StringId name_id =
      context_->storage->InternString(base::StringView(std::to_string(token)));

  TrackId track_id = context_->track_compressor->InternBegin(
      kExpectedBlueprint, tracks::Dimensions(upid), cookie);
  context_->slice_tracker->Begin(
      timestamp, track_id, kNullStringId, name_id,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(surface_frame_token_id_, Variadic::Integer(token));
        inserter->AddArg(display_frame_token_id_,
                         Variadic::Integer(display_frame_token));
        inserter->AddArg(layer_name_id_, Variadic::String(layer_name_id));
      });
}

StringId FrameTimelineEventParser::CalculateSurfaceFrameJankTag(
    int32_t jank_type,
    std::optional<int32_t> present_type_opt) {
  StringId jank_tag;
  if (jank_type == FrameTimelineEvent::JANK_UNSPECIFIED) {
    jank_tag = jank_tag_unspecified_id_;
  } else if (SurfaceFrameJanky(jank_type)) {
    jank_tag = jank_tag_self_id_;
  } else if (DisplayFrameJanky(jank_type)) {
    jank_tag = jank_tag_other_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_BUFFER_STUFFING) {
    jank_tag = jank_tag_buffer_stuffing_id_;
  } else if (present_type_opt.has_value() &&
             *present_type_opt == FrameTimelineEvent::PRESENT_DROPPED) {
    jank_tag = jank_tag_dropped_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_NON_ANIMATING) {
    jank_tag = jank_tag_none_animating_id_;
  } else if (jank_type == FrameTimelineEvent::JANK_DISPLAY_NOT_ON) {
    jank_tag = jank_tag_display_not_on_id_;
  } else {
    jank_tag = jank_tag_none_id_;
  }

  return jank_tag;
}

void FrameTimelineEventParser::ParseActualSurfaceFrameStart(int64_t timestamp,
                                                            ConstBytes blob) {
  ActualSurfaceFrameStartDecoder event(blob);

  if (!event.has_cookie() || !event.has_token() ||
      !event.has_display_frame_token() || !event.has_pid()) {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
    return;
  }

  int64_t cookie = event.cookie();
  int64_t token = event.token();
  int64_t display_frame_token = event.display_frame_token();
  double jank_severity_score = static_cast<double>(event.jank_severity_score());
  double present_delay_millis =
      static_cast<double>(event.present_delay_millis());
  double vsync_resynced_jitter_millis =
      static_cast<double>(event.vsync_resynced_jitter_millis());
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(event.pid()));
  cookie_map_[cookie] = std::make_pair(upid, TrackType::kActual);

  StringId layer_name_id;
  if (event.has_layer_name())
    layer_name_id =
        context_->storage->InternString(base::StringView(event.layer_name()));
  StringId name_id =
      context_->storage->InternString(base::StringView(std::to_string(token)));

  TrackId track_id = context_->track_compressor->InternBegin(
      kActualBlueprint, tracks::Dimensions(upid), cookie);

  // parse present type
  StringId present_type = present_type_ids_[0];
  bool present_type_validated = false;
  if (event.has_present_type() &&
      ValidatePresentType(context_, event.present_type())) {
    present_type_validated = true;
    present_type = present_type_ids_[static_cast<size_t>(event.present_type())];
  }

  // parse present type experimental
  StringId present_type_experimental = present_type_experimental_ids_[0];
  bool present_type_experimental_validated = false;
  if (event.has_present_type_experimental() &&
      ValidatePresentType(context_, event.present_type_experimental())) {
    present_type_experimental_validated = true;
    present_type_experimental =
        present_type_experimental_ids_[static_cast<size_t>(
            event.present_type_experimental())];
  }

  // parse jank type
  StringId jank_type = JankTypeBitmaskToStringId(context_, event.jank_type());

  // parse jank type experimental
  StringId jank_type_experimental =
      JankTypeBitmaskToStringId(context_, event.jank_type_experimental());

  // parse jank severity type
  StringId jank_severity_type;
  if (event.has_jank_severity_type()) {
    jank_severity_type = jank_severity_type_ids_[static_cast<size_t>(
        event.jank_severity_type())];
  } else {
    // NOTE: Older traces don't have this field. If JANK_NONE use
    // |severity_type| "None", and is not present, use "Unknown".
    jank_severity_type = (event.jank_type() == FrameTimelineEvent::JANK_NONE)
                             ? jank_severity_type_ids_[1]  /* None */
                             : jank_severity_type_ids_[0]; /* Unknown */
  }

  // parse prediction type
  StringId prediction_type = prediction_type_ids_[0];
  if (event.has_prediction_type() &&
      ValidatePredictionType(context_, event.prediction_type())) {
    prediction_type =
        prediction_type_ids_[static_cast<size_t>(event.prediction_type())];
  }

  const StringId jank_tag = CalculateSurfaceFrameJankTag(
      event.jank_type(), present_type_validated
                             ? std::make_optional(event.present_type())
                             : std::nullopt);
  const StringId jank_tag_experimental = CalculateSurfaceFrameJankTag(
      event.jank_type_experimental(),
      present_type_experimental_validated
          ? std::make_optional(event.present_type_experimental())
          : std::nullopt);

  StringId is_buffer = context_->storage->InternString("Unspecified");
  if (event.has_is_buffer()) {
    if (event.is_buffer()) {
      is_buffer = context_->storage->InternString("Yes");
    } else {
      is_buffer = context_->storage->InternString("No");
    }
  }

  std::optional<SliceId> opt_slice_id = context_->slice_tracker->Begin(
      timestamp, track_id, kNullStringId, name_id,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(surface_frame_token_id_, Variadic::Integer(token));
        inserter->AddArg(display_frame_token_id_,
                         Variadic::Integer(display_frame_token));
        inserter->AddArg(present_delay_millis_id_,
                         Variadic::Real(present_delay_millis));
        inserter->AddArg(vsync_resynced_jitter_millis_id_,
                         Variadic::Real(vsync_resynced_jitter_millis));
        inserter->AddArg(layer_name_id_, Variadic::String(layer_name_id));
        inserter->AddArg(present_type_id_, Variadic::String(present_type));
        inserter->AddArg(present_type_experimental_id_,
                         Variadic::String(present_type_experimental));
        inserter->AddArg(on_time_finish_id_,
                         Variadic::Integer(event.on_time_finish()));
        inserter->AddArg(gpu_composition_id_,
                         Variadic::Integer(event.gpu_composition()));
        inserter->AddArg(jank_type_id_, Variadic::String(jank_type));
        inserter->AddArg(jank_type_experimental_id_,
                         Variadic::String(jank_type_experimental));
        inserter->AddArg(jank_severity_type_id_,
                         Variadic::String(jank_severity_type));
        inserter->AddArg(jank_severity_score_id_,
                         Variadic::Real(jank_severity_score));
        inserter->AddArg(prediction_type_id_,
                         Variadic::String(prediction_type));
        inserter->AddArg(jank_tag_id_, Variadic::String(jank_tag));
        inserter->AddArg(jank_tag_experimental_id_,
                         Variadic::String(jank_tag_experimental));
        inserter->AddArg(is_buffer_id_, Variadic::String(is_buffer));
      });

  if (opt_slice_id) {
    display_token_to_surface_slice_.emplace(display_frame_token, *opt_slice_id);
  }
}

void FrameTimelineEventParser::ParseFrameEnd(int64_t timestamp,
                                             ConstBytes blob) {
  FrameEndDecoder event(blob);
  if (!event.has_cookie()) {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
    return;
  }

  int64_t cookie = event.cookie();
  auto* it = cookie_map_.Find(cookie);
  if (!it) {
    context_->storage->IncrementStats(stats::frame_timeline_unpaired_end_event);
    return;
  }
  TrackId track_id;
  switch (it->second) {
    case TrackType::kExpected:
      track_id = context_->track_compressor->InternEnd(
          kExpectedBlueprint, tracks::Dimensions(it->first), cookie);
      break;
    case TrackType::kActual:
      track_id = context_->track_compressor->InternEnd(
          kActualBlueprint, tracks::Dimensions(it->first), cookie);
      break;
  }
  context_->slice_tracker->End(timestamp, track_id);
  cookie_map_.Erase(cookie);
}

void FrameTimelineEventParser::ParseFrameTimelineEvent(int64_t timestamp,
                                                       ConstBytes blob) {
  protos::pbzero::FrameTimelineEvent_Decoder frame_event(blob);

  // Due to platform bugs, negative timestamps can creep into into traces.
  // Ensure that it doesn't make it into the tables.
  // TODO(mayzner): remove the negative check once we have some logic handling
  // this at the sorter level.
  if (timestamp < 0 || IsBadTimestamp(timestamp)) {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
    return;
  }

  if (frame_event.has_expected_display_frame_start()) {
    ParseExpectedDisplayFrameStart(timestamp,
                                   frame_event.expected_display_frame_start());
  } else if (frame_event.has_actual_display_frame_start()) {
    ParseActualDisplayFrameStart(timestamp,
                                 frame_event.actual_display_frame_start());
  } else if (frame_event.has_expected_surface_frame_start()) {
    ParseExpectedSurfaceFrameStart(timestamp,
                                   frame_event.expected_surface_frame_start());
  } else if (frame_event.has_actual_surface_frame_start()) {
    ParseActualSurfaceFrameStart(timestamp,
                                 frame_event.actual_surface_frame_start());
  } else if (frame_event.has_frame_end()) {
    ParseFrameEnd(timestamp, frame_event.frame_end());
  } else {
    context_->storage->IncrementStats(
        stats::frame_timeline_event_parser_errors);
  }
}
}  // namespace perfetto::trace_processor
