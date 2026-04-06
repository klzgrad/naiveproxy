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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_PARSER_H_

#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_rect_computation.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_visibility_computation.h"
#include "src/trace_processor/importers/proto/winscope/winscope_context.h"
#include "src/trace_processor/importers/proto/winscope/winscope_geometry.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor::winscope {

namespace {
using SnapshotId = tables::SurfaceFlingerLayersSnapshotTable::Id;
using LayerDecoder = protos::pbzero::LayerProto::Decoder;
using DisplayDecoder = protos::pbzero::DisplayProto::Decoder;
}  // namespace

class SurfaceFlingerLayersParser {
 public:
  explicit SurfaceFlingerLayersParser(WinscopeContext*);
  void Parse(int64_t timestamp,
             protozero::ConstBytes decoder,
             std::optional<uint32_t> sequence_id);

 private:
  const SnapshotId ParseSnapshot(int64_t timestamp,
                                 protozero::ConstBytes blob,
                                 std::optional<uint32_t> sequence_id);

  void ParseLayer(
      int64_t timestamp,
      protozero::ConstBytes blob,
      const SnapshotId& snapshot_id,
      const std::optional<surfaceflinger_layers::VisibilityProperties>&
          visibility,
      const std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
      const surfaceflinger_layers::SurfaceFlingerRects& rects);

  tables::SurfaceFlingerLayerTable::Id InsertLayerRow(
      protozero::ConstBytes blob,
      const SnapshotId& snapshot_id,
      const std::optional<surfaceflinger_layers::VisibilityProperties>&
          visibility,
      const std::unordered_map<int32_t, LayerDecoder>& layers_by_id,
      const surfaceflinger_layers::SurfaceFlingerRects& rects);

  void TryAddBlockingLayerArgs(const std::vector<int32_t>& blocking_layers,
                               const std::string key_prefix,
                               ArgsParser& writer);

  void ParseDisplay(
      const DisplayDecoder& display_decoder,
      const SnapshotId& snapshot_id,
      int index,
      std::unordered_map<uint32_t, geometry::Rect>& displays_by_layer_stack);

  const tables::WinscopeRectTable::Id& InsertDisplayRectRow(
      const DisplayDecoder& display_decoder,
      std::unordered_map<uint32_t, geometry::Rect>& displays_by_layer_stack);

  tables::WinscopeTraceRectTable::Id InsertDisplayTraceRectRow(
      const DisplayDecoder& display_decoder,
      const tables::WinscopeRectTable::Id& rect_id,
      int index);

  WinscopeContext* const context_;
  util::ProtoToArgsParser args_parser_;

  const uint32_t INVALID_LAYER_STACK = 4294967295;
};
}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_PARSER_H_
