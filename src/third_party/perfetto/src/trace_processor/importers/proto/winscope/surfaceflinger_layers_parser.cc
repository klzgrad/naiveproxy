/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_parser.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_extractor.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_rect_computation.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_utils.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_visibility_computation.h"
#include "src/trace_processor/importers/proto/winscope/winscope_context.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/proto_to_args_parser.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto::trace_processor::winscope {

SurfaceFlingerLayersParser::SurfaceFlingerLayersParser(WinscopeContext* context)
    : context_{context},
      args_parser_{*context->trace_processor_context_->descriptor_pool_} {}

void SurfaceFlingerLayersParser::Parse(int64_t timestamp,
                                       protozero::ConstBytes blob,
                                       std::optional<uint32_t> sequence_id) {
  protos::pbzero::LayersSnapshotProto::Decoder snapshot_decoder(blob);

  const auto& snapshot_id = ParseSnapshot(timestamp, blob, sequence_id);

  std::unordered_map<uint32_t, geometry::Rect> displays_by_layer_stack;

  if (snapshot_decoder.has_displays()) {
    int index = 0;
    for (auto it = snapshot_decoder.displays(); it; ++it) {
      DisplayDecoder display_decoder(*it);
      ParseDisplay(display_decoder, snapshot_id, index,
                   displays_by_layer_stack);
      index++;
    }
  }

  protos::pbzero::LayersProto::Decoder layers_decoder(
      snapshot_decoder.layers());

  const std::unordered_map<int32_t, LayerDecoder>& layers_by_id =
      surfaceflinger_layers::ExtractLayersById(layers_decoder);

  const std::vector<LayerDecoder>& layers_top_to_bottom =
      surfaceflinger_layers::ExtractLayersTopToBottom(layers_decoder);

  std::unordered_map<int32_t, surfaceflinger_layers::VisibilityProperties>
      computed_visibility =
          surfaceflinger_layers::VisibilityComputation(
              snapshot_decoder, layers_top_to_bottom, layers_by_id,
              context_->trace_processor_context_->storage
                  ->mutable_string_pool())
              .Compute();

  const auto& computed_rects =
      surfaceflinger_layers::RectComputation(
          snapshot_decoder, layers_top_to_bottom, computed_visibility,
          displays_by_layer_stack, context_->rect_tracker_,
          context_->transform_tracker_)
          .Compute();

  for (auto it = layers_decoder.layers(); it; ++it) {
    LayerDecoder layer(*it);
    std::optional<surfaceflinger_layers::VisibilityProperties> visibility;
    surfaceflinger_layers::SurfaceFlingerRects rects;
    if (layer.has_id()) {
      auto maybe_visibility = computed_visibility.find(layer.id());
      if (maybe_visibility != computed_visibility.end()) {
        visibility = maybe_visibility->second;
      }
      auto maybe_rects = computed_rects.find(layer.id());
      if (maybe_rects != computed_rects.end()) {
        rects = maybe_rects->second;
      }
    }

    ParseLayer(timestamp, *it, snapshot_id, visibility, layers_by_id, rects);
  }
}

const SnapshotId SurfaceFlingerLayersParser::ParseSnapshot(
    int64_t timestamp,
    protozero::ConstBytes blob,
    std::optional<uint32_t> sequence_id) {
  auto* storage = context_->trace_processor_context_->storage.get();
  tables::SurfaceFlingerLayersSnapshotTable::Row snapshot;
  snapshot.ts = timestamp;
  protos::pbzero::LayersSnapshotProto::Decoder snapshot_decoder(blob);
  snapshot.has_invalid_elapsed_ts =
      snapshot_decoder.elapsed_realtime_nanos() == 0;
  snapshot.base64_proto_id = storage->mutable_string_pool()
                                 ->InternString(base::StringView(
                                     base::Base64Encode(blob.data, blob.size)))
                                 .raw_id();
  if (sequence_id) {
    snapshot.sequence_id = *sequence_id;
  }
  const auto snapshot_id =
      storage->mutable_surfaceflinger_layers_snapshot_table()
          ->Insert(snapshot)
          .id;

  ArgsTracker args_tracker(context_->trace_processor_context_);
  auto inserter = args_tracker.AddArgsTo(snapshot_id);
  ArgsParser writer(timestamp, inserter, *storage);
  const auto table_name = tables::SurfaceFlingerLayersSnapshotTable::Name();
  auto allowed_fields =
      util::winscope_proto_mapping::GetAllowedFields(table_name);
  base::Status status = args_parser_.ParseMessage(
      blob, *util::winscope_proto_mapping::GetProtoName(table_name),
      &allowed_fields.value(), writer);
  if (!status.ok()) {
    storage->IncrementStats(stats::winscope_sf_layers_parse_errors);
  }
  return snapshot_id;
}

void SurfaceFlingerLayersParser::ParseLayer(
    int64_t timestamp,
    protozero::ConstBytes blob,
    const SnapshotId& snapshot_id,
    const std::optional<surfaceflinger_layers::VisibilityProperties>&
        visibility,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
    const surfaceflinger_layers::SurfaceFlingerRects& rects) {
  auto* storage = context_->trace_processor_context_->storage.get();
  ArgsTracker tracker(context_->trace_processor_context_);
  auto row_id =
      InsertLayerRow(blob, snapshot_id, visibility, layers_by_id, rects);
  auto inserter = tracker.AddArgsTo(row_id);
  ArgsParser writer(timestamp, inserter, *storage);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::SurfaceFlingerLayerTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    storage->IncrementStats(stats::winscope_sf_layers_parse_errors);
  }

  if (!visibility.has_value()) {
    return;
  }
  if (visibility->visibility_reasons.size() > 0) {
    auto i = 0;
    auto pool = storage->mutable_string_pool();
    for (const auto& reason : visibility->visibility_reasons) {
      util::ProtoToArgsParser::Key key;
      key.key = "visibility_reason[" + std::to_string(i) + ']';
      key.flat_key = "visibility_reason";
      writer.AddString(key, pool->Get(reason).c_str());
      i++;
    }
  }
  TryAddBlockingLayerArgs(visibility->occluding_layers, "occluded_by", writer);
  TryAddBlockingLayerArgs(visibility->partially_occluding_layers,
                          "partially_occluded_by", writer);
  TryAddBlockingLayerArgs(visibility->covering_layers, "covered_by", writer);
}

tables::SurfaceFlingerLayerTable::Id SurfaceFlingerLayersParser::InsertLayerRow(
    protozero::ConstBytes blob,
    const SnapshotId& snapshot_id,
    const std::optional<surfaceflinger_layers::VisibilityProperties>&
        visibility,
    const std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
    const surfaceflinger_layers::SurfaceFlingerRects& rects) {
  auto* string_pool =
      context_->trace_processor_context_->storage->mutable_string_pool();

  tables::SurfaceFlingerLayerTable::Row layer;
  layer.snapshot_id = snapshot_id;
  layer.base64_proto_id = string_pool
                              ->InternString(base::StringView(
                                  base::Base64Encode(blob.data, blob.size)))
                              .raw_id();
  LayerDecoder layer_decoder(blob);
  if (layer_decoder.has_id()) {
    layer.layer_id = layer_decoder.id();
  }

  if (layer_decoder.has_name()) {
    layer.layer_name =
        string_pool->InternString(base::StringView(layer_decoder.name()));
  }
  if (layer_decoder.has_parent()) {
    layer.parent = layer_decoder.parent();
  }

  auto corner_radii =
      surfaceflinger_layers::layer::GetCornerRadii(layer_decoder);
  layer.corner_radius_tl = corner_radii.tl;
  layer.corner_radius_tr = corner_radii.tr;
  layer.corner_radius_bl = corner_radii.bl;
  layer.corner_radius_br = corner_radii.br;

  if (layer_decoder.has_hwc_composition_type()) {
    layer.hwc_composition_type = layer_decoder.hwc_composition_type();
  }
  if (layer_decoder.has_z_order_relative_of()) {
    layer.z_order_relative_of = layer_decoder.z_order_relative_of();
    if (layer_decoder.z_order_relative_of() > 0 &&
        layers_by_id.find(layer_decoder.z_order_relative_of()) ==
            layers_by_id.end()) {
      layer.is_missing_z_parent = true;
    }
  }
  layer.is_hidden_by_policy =
      surfaceflinger_layers::layer::IsHiddenByPolicy(layer_decoder);
  layer.is_visible = visibility.has_value() ? visibility->is_visible : false;
  layer.layer_rect_id = rects.layer_rect;
  layer.input_rect_id = rects.input_rect;
  return context_->trace_processor_context_->storage
      ->mutable_surfaceflinger_layer_table()
      ->Insert(layer)
      .id;
}

void SurfaceFlingerLayersParser::TryAddBlockingLayerArgs(
    const std::vector<int32_t>& blocking_layers,
    const std::string key_prefix,
    ArgsParser& writer) {
  if (blocking_layers.size() == 0) {
    return;
  }
  auto i = 0;
  for (auto blocking_layer : blocking_layers) {
    util::ProtoToArgsParser::Key key;
    key.key = key_prefix + "[" + std::to_string(i) + ']';
    key.flat_key = key_prefix;
    writer.AddInteger(key, blocking_layer);
    i++;
  }
}

void SurfaceFlingerLayersParser::ParseDisplay(
    const DisplayDecoder& display_decoder,
    const SnapshotId& snapshot_id,
    int index,
    std::unordered_map<uint32_t, geometry::Rect>& displays_by_layer_stack) {
  tables::SurfaceFlingerDisplayTable::Row display;
  display.snapshot_id = snapshot_id;
  display.is_virtual =
      display_decoder.has_is_virtual() ? display_decoder.is_virtual() : false;

  if (display_decoder.has_name()) {
    display.display_name =
        context_->trace_processor_context_->storage->mutable_string_pool()
            ->InternString(display_decoder.name());
  }

  if (display_decoder.has_layer_stack()) {
    display.is_on = display_decoder.layer_stack() != INVALID_LAYER_STACK;
  } else {
    display.is_on = false;
  }
  display.display_id = static_cast<int64_t>(display_decoder.id());

  const auto& rect_id =
      InsertDisplayRectRow(display_decoder, displays_by_layer_stack);

  display.trace_rect_id =
      InsertDisplayTraceRectRow(display_decoder, rect_id, index);

  context_->trace_processor_context_->storage
      ->mutable_surfaceflinger_display_table()
      ->Insert(display);
}

const tables::WinscopeRectTable::Id&
SurfaceFlingerLayersParser::InsertDisplayRectRow(
    const DisplayDecoder& display_decoder,
    std::unordered_map<uint32_t, geometry::Rect>& displays_by_layer_stack) {
  geometry::Rect rect =
      surfaceflinger_layers::display::MakeLayerStackSpaceRect(display_decoder);

  if (display_decoder.has_layer_stack()) {
    displays_by_layer_stack[display_decoder.layer_stack()] = rect;
  }

  if (rect.IsEmpty()) {
    const auto& size =
        surfaceflinger_layers::display::GetDisplaySize(display_decoder);
    rect = geometry::Rect(0, 0, size.w, size.h);
  }

  return context_->rect_tracker_.GetOrInsertRow(rect);
}

tables::WinscopeTraceRectTable::Id
SurfaceFlingerLayersParser::InsertDisplayTraceRectRow(
    const DisplayDecoder& display_decoder,
    const tables::WinscopeRectTable::Id& rect_id,
    int index) {
  tables::WinscopeTraceRectTable::Row row;
  row.rect_id = rect_id;
  row.group_id = display_decoder.layer_stack();
  row.depth = static_cast<uint32_t>(index);
  row.is_spy = false;
  return context_->trace_processor_context_->storage
      ->mutable_winscope_trace_rect_table()
      ->Insert(row)
      .id;
}

}  // namespace perfetto::trace_processor::winscope
