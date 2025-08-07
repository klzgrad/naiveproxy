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

#include "perfetto/ext/base/base64.h"
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto {
namespace trace_processor {

SurfaceFlingerLayersParser::SurfaceFlingerLayersParser(
    TraceProcessorContext* context)
    : context_{context}, args_parser_{*context->descriptor_pool_} {}

void SurfaceFlingerLayersParser::Parse(int64_t timestamp,
                                       protozero::ConstBytes blob) {
  protos::pbzero::LayersSnapshotProto::Decoder snapshot_decoder(blob);
  tables::SurfaceFlingerLayersSnapshotTable::Row snapshot;
  snapshot.ts = timestamp;
  snapshot.base64_proto_id = context_->storage->mutable_string_pool()
                                 ->InternString(base::StringView(
                                     base::Base64Encode(blob.data, blob.size)))
                                 .raw_id();
  auto snapshot_id =
      context_->storage->mutable_surfaceflinger_layers_snapshot_table()
          ->Insert(snapshot)
          .id;

  auto inserter = context_->args_tracker->AddArgsTo(snapshot_id);
  ArgsParser writer(timestamp, inserter, *context_->storage);
  const auto table_name = tables::SurfaceFlingerLayersSnapshotTable::Name();
  auto allowed_fields =
      util::winscope_proto_mapping::GetAllowedFields(table_name);
  base::Status status = args_parser_.ParseMessage(
      blob, *util::winscope_proto_mapping::GetProtoName(table_name),
      &allowed_fields.value(), writer);
  if (!status.ok()) {
    context_->storage->IncrementStats(stats::winscope_sf_layers_parse_errors);
  }

  protos::pbzero::LayersProto::Decoder layers_decoder(
      snapshot_decoder.layers());
  for (auto it = layers_decoder.layers(); it; ++it) {
    ParseLayer(timestamp, *it, snapshot_id);
  }
}

void SurfaceFlingerLayersParser::ParseLayer(
    int64_t timestamp,
    protozero::ConstBytes blob,
    tables::SurfaceFlingerLayersSnapshotTable::Id snapshot_id) {
  tables::SurfaceFlingerLayerTable::Row layer;
  layer.snapshot_id = snapshot_id;
  layer.base64_proto_id = context_->storage->mutable_string_pool()
                              ->InternString(base::StringView(
                                  base::Base64Encode(blob.data, blob.size)))
                              .raw_id();
  auto layerId =
      context_->storage->mutable_surfaceflinger_layer_table()->Insert(layer).id;

  ArgsTracker tracker(context_);
  auto inserter = tracker.AddArgsTo(layerId);
  ArgsParser writer(timestamp, inserter, *context_->storage);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::SurfaceFlingerLayerTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    context_->storage->IncrementStats(stats::winscope_sf_layers_parse_errors);
  }
}

}  // namespace trace_processor
}  // namespace perfetto
