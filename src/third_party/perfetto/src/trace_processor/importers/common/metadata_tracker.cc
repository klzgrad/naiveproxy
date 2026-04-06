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

#include "src/trace_processor/importers/common/metadata_tracker.h"

#include <optional>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/importers/common/global_metadata_tracker.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

MetadataTracker::MetadataTracker(TraceProcessorContext* context)
    : context_(context) {}

MetadataId MetadataTracker::SetMetadata(metadata::KeyId key, Variadic value) {
  return context_->global_metadata_tracker->SetMetadata(
      context_->machine_id(), context_->trace_id(), key, value);
}

std::optional<SqlValue> MetadataTracker::GetMetadata(metadata::KeyId key) {
  return context_->global_metadata_tracker->GetMetadata(
      context_->machine_id(), context_->trace_id(), key);
}

MetadataId MetadataTracker::AppendMetadata(metadata::KeyId key,
                                           Variadic value) {
  return context_->global_metadata_tracker->AppendMetadata(
      context_->machine_id(), context_->trace_id(), key, value);
}

MetadataId MetadataTracker::SetDynamicMetadata(StringId key, Variadic value) {
  return context_->global_metadata_tracker->SetDynamicMetadata(
      context_->machine_id(), context_->trace_id(), key, value);
}

}  // namespace perfetto::trace_processor
