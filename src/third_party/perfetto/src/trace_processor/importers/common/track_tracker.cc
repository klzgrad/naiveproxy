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

#include "src/trace_processor/importers/common/track_tracker.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/tracks_internal.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

TrackTracker::TrackTracker(TraceProcessorContext* context)
    : context_(context),
      args_tracker_(context),
      description_key_id_(context->storage->InternString("description")) {}

TrackId TrackTracker::AddTrack(const tracks::BlueprintBase& blueprint,
                               StringId name,
                               StringId counter_unit,
                               StringId description,
                               GlobalArgsTracker::CompactArg* d_args,
                               uint32_t d_size,
                               const SetArgsCallback& args) {
  tables::TrackTable::Row row(name);
  const auto* dims = blueprint.dimension_blueprints.data();
  for (uint32_t i = 0; i < d_size; ++i) {
    base::StringView str(dims[i].name.data(), dims[i].name.size());
    if (str == "cpu" && d_args[i].value.type == Variadic::kInt) {
      context_->cpu_tracker->MarkCpuValid(
          static_cast<uint32_t>(d_args[i].value.int_value));
    } else if (str == "utid" && d_args[i].value.type == Variadic::kInt) {
      row.utid = static_cast<uint32_t>(d_args[i].value.int_value);
    } else if (str == "upid" && d_args[i].value.type == Variadic::kInt) {
      row.upid = static_cast<uint32_t>(d_args[i].value.int_value);
    }
    StringId key = context_->storage->InternString(str);
    d_args[i].key = key;
    d_args[i].flat_key = key;
  }

  row.machine_id = context_->machine_id();
  row.type = context_->storage->InternString(
      base::StringView(blueprint.type.data(), blueprint.type.size()));
  if (d_size > 0) {
    row.dimension_arg_set_id =
        context_->global_args_tracker->AddArgSet(d_args, 0, d_size);
  }
  row.event_type = context_->storage->InternString(blueprint.event_type);
  row.counter_unit = counter_unit;
  TrackId id = context_->storage->mutable_track_table()->Insert(row).id;
  if (description != kNullStringId || args) {
    auto inserter = args_tracker_.AddArgsTo(id);
    if (description != kNullStringId) {
      inserter.AddArg(description_key_id_, Variadic::String(description));
    }
    if (args) {
      args(inserter);
    }
    args_tracker_.Flush();
  }
  return id;
}

}  // namespace perfetto::trace_processor
