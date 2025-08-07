/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/tracing/internal/track_event_interned_fields.h"

namespace perfetto {
namespace internal {

InternedEventCategory::~InternedEventCategory() = default;

// static
void InternedEventCategory::Add(protos::pbzero::InternedData* interned_data,
                                size_t iid,
                                const char* value,
                                size_t length) {
  auto category = interned_data->add_event_categories();
  category->set_iid(iid);
  category->set_name(value, length);
}

InternedEventName::~InternedEventName() = default;

// static
void InternedEventName::Add(protos::pbzero::InternedData* interned_data,
                            size_t iid,
                            const char* value) {
  auto name = interned_data->add_event_names();
  name->set_iid(iid);
  name->set_name(value);
}

InternedDebugAnnotationName::~InternedDebugAnnotationName() = default;

// static
void InternedDebugAnnotationName::Add(
    protos::pbzero::InternedData* interned_data,
    size_t iid,
    const char* value) {
  auto name = interned_data->add_debug_annotation_names();
  name->set_iid(iid);
  name->set_name(value);
}

InternedDebugAnnotationValueTypeName::~InternedDebugAnnotationValueTypeName() =
    default;

// static
void InternedDebugAnnotationValueTypeName::Add(
    protos::pbzero::InternedData* interned_data,
    size_t iid,
    const char* value) {
  auto name = interned_data->add_debug_annotation_value_type_names();
  name->set_iid(iid);
  name->set_name(value);
}

}  // namespace internal
}  // namespace perfetto
