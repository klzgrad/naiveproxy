/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/content_analyzer.h"

#include <cstdint>
#include <optional>
#include <string>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/trace_proto_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/proto_profiler.h"

namespace perfetto::trace_processor {

ProtoContentAnalyzer::ProtoContentAnalyzer(TraceProcessorContext* context)
    : context_(context),
      computer_(context_->descriptor_pool_.get(),
                ".perfetto.protos.TracePacket") {}

ProtoContentAnalyzer::~ProtoContentAnalyzer() = default;

void ProtoContentAnalyzer::ProcessPacket(
    const TraceBlobView& packet,
    const SampleAnnotation& packet_annotations) {
  auto& map = aggregated_samples_[packet_annotations];
  computer_.Reset(packet.data(), packet.length());
  for (auto sample = computer_.GetNext(); sample.has_value();
       sample = computer_.GetNext()) {
    auto* value = map.Find(computer_.GetPath());
    if (value) {
      value->size += *sample;
      ++value->count;
    } else {
      map.Insert(computer_.GetPath(), Sample{*sample, 1});
    }
  }
}

void ProtoContentAnalyzer::NotifyEndOfFile() {
  // TODO(kraskevich): consider generating a flamegraph-compatable table once
  // Perfetto UI supports custom flamegraphs (b/227644078).
  for (auto annotated_map = aggregated_samples_.GetIterator(); annotated_map;
       ++annotated_map) {
    base::FlatHashMap<util::SizeProfileComputer::FieldPath,
                      tables::ExperimentalProtoPathTable::Id,
                      util::SizeProfileComputer::FieldPathHasher>
        path_ids;
    for (auto sample = annotated_map.value().GetIterator(); sample; ++sample) {
      std::string path_string;
      std::optional<tables::ExperimentalProtoPathTable::Id> previous_path_id;
      util::SizeProfileComputer::FieldPath path;
      for (const auto& field : sample.key()) {
        if (field.has_field_name()) {
          if (!path_string.empty()) {
            path_string += '.';
          }
          path_string.append(field.field_name());
        }
        if (!path_string.empty()) {
          path_string += '.';
        }
        path_string.append(field.type_name());

        path.push_back(field);
        // Reuses existing path from |path_ids| if possible.
        {
          auto* path_id = path_ids.Find(path);
          if (path_id) {
            previous_path_id = *path_id;
            continue;
          }
        }
        // Create a new row in experimental_proto_path.
        tables::ExperimentalProtoPathTable::Row path_row;
        if (field.has_field_name()) {
          path_row.field_name = context_->storage->InternString(
              base::StringView(field.field_name()));
        }
        path_row.field_type = context_->storage->InternString(
            base::StringView(field.type_name()));
        if (previous_path_id.has_value())
          path_row.parent_id = *previous_path_id;

        auto path_id =
            context_->storage->mutable_experimental_proto_path_table()
                ->Insert(path_row)
                .id;
        if (!previous_path_id.has_value()) {
          // Add annotations to the current row as an args set.
          auto inserter = context_->args_tracker->AddArgsTo(path_id);
          for (auto& annotation : annotated_map.key()) {
            inserter.AddArg(annotation.first,
                            Variadic::String(annotation.second));
          }
        }
        previous_path_id = path_id;
        path_ids[path] = path_id;
      }

      // Add a content row referring to |previous_path_id|.
      tables::ExperimentalProtoContentTable::Row content_row;
      content_row.path =
          context_->storage->InternString(base::StringView(path_string));
      content_row.path_id = *previous_path_id;
      content_row.total_size = static_cast<int64_t>(sample.value().size);
      content_row.size = static_cast<int64_t>(sample.value().size);
      content_row.count = static_cast<int64_t>(sample.value().count);
      context_->storage->mutable_experimental_proto_content_table()->Insert(
          content_row);
    }
  }
  aggregated_samples_.Clear();
}

}  // namespace perfetto::trace_processor
