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

#include "src/trace_processor/importers/ftrace/generic_ftrace_tracker.h"

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_utils.h"
#include "protos/perfetto/common/descriptor.pbzero.h"

#include "src/trace_processor/storage/stats.h"

namespace perfetto::trace_processor {

using protozero::proto_utils::ProtoSchemaType;

// We do not expect tracepoints with over 32 fields. It's more likely that the
// trace is corrupted. See also |kMaxFtraceEventFields| in ftrace_descriptors.h.
static constexpr uint32_t kMaxAllowedFields = 32;

GenericFtraceTracker::GenericFtraceTracker(TraceProcessorContext* context)
    : context_(context) {}

GenericFtraceTracker::~GenericFtraceTracker() = default;

void GenericFtraceTracker::AddDescriptor(uint32_t pb_field_id,
                                         protozero::ConstBytes pb_descriptor) {
  if (events_.Find(pb_field_id))
    return;  // already added

  protos::pbzero::DescriptorProto::Decoder decoder(pb_descriptor);

  GenericEvent event;
  event.name = context_->storage->InternString(decoder.name());
  for (auto it = decoder.field(); it; ++it) {
    protos::pbzero::FieldDescriptorProto::Decoder field_decoder(it->data(),
                                                                it->size());

    uint32_t field_id = static_cast<uint32_t>(field_decoder.number());
    if (field_id >= kMaxAllowedFields) {
      PERFETTO_DLOG("Skipping generic descriptor with >32 fields.");
      context_->storage->IncrementStats(
          stats::ftrace_generic_descriptor_errors);
      return;
    }
    if (field_decoder.type() > static_cast<int32_t>(ProtoSchemaType::kSint64)) {
      PERFETTO_DLOG("Skipping generic descriptor with invalid field type.");
      context_->storage->IncrementStats(
          stats::ftrace_generic_descriptor_errors);
      return;
    }

    // Ensure field vector is big enough.
    if (field_id >= event.fields.size()) {
      event.fields.resize(field_id + 1);
    }
    GenericField& field = event.fields[field_id];

    field.name = context_->storage->InternString(field_decoder.name());
    field.type = static_cast<ProtoSchemaType>(field_decoder.type());
  }
  events_.Insert(pb_field_id, std::move(event));
}

GenericFtraceTracker::GenericEvent* GenericFtraceTracker::GetEvent(
    uint32_t pb_field_id) {
  return events_.Find(pb_field_id);
}

}  // namespace perfetto::trace_processor
