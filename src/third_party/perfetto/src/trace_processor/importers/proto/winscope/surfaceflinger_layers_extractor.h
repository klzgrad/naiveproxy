/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_EXTRACTOR_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_EXTRACTOR_H_

#include <unordered_map>
#include <vector>
#include "protos/perfetto/trace/android/surfaceflinger_layers.pbzero.h"

namespace perfetto::trace_processor::winscope::surfaceflinger_layers {

namespace {
using LayersDecoder = protos::pbzero::LayersProto::Decoder;
using LayerDecoder = protos::pbzero::LayerProto::Decoder;
}  // namespace

std::unordered_map<int32_t, LayerDecoder> ExtractLayersById(
    const LayersDecoder& layers_decoder);

std::vector<LayerDecoder> ExtractLayersTopToBottom(
    const LayersDecoder& layers_decoder);

}  // namespace perfetto::trace_processor::winscope::surfaceflinger_layers

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_SURFACEFLINGER_LAYERS_EXTRACTOR_H_
