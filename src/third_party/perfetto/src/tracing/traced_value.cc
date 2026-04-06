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

#include "perfetto/tracing/traced_value.h"

#include "perfetto/base/logging.h"
#include "perfetto/tracing/debug_annotation.h"
#include "perfetto/tracing/internal/track_event_interned_fields.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"

namespace perfetto {

namespace internal {

TracedValue CreateTracedValueFromProto(
    protos::pbzero::DebugAnnotation* annotation,
    EventContext* event_context) {
  return TracedValue::CreateFromProto(annotation, event_context);
}

}  // namespace internal

// static
TracedValue TracedValue::CreateFromProto(
    protos::pbzero::DebugAnnotation* annotation,
    EventContext* event_context) {
  return TracedValue(annotation, event_context, nullptr);
}

TracedValue::TracedValue(TracedValue&&) = default;
TracedValue::~TracedValue() = default;

void TracedValue::WriteInt64(int64_t value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_int_value(value);
}

void TracedValue::WriteUInt64(uint64_t value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_uint_value(value);
}

void TracedValue::WriteDouble(double value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_double_value(value);
}

void TracedValue::WriteBoolean(bool value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_bool_value(value);
}

void TracedValue::WriteString(const char* value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_string_value(value);
}

void TracedValue::WriteString(const char* value, size_t len) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_string_value(value, len);
}

void TracedValue::WriteString(const std::string& value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_string_value(value);
}

void TracedValue::WriteString(std::string_view value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_string_value(value.data(), value.size());
}

void TracedValue::WritePointer(const void* value) && {
  PERFETTO_DCHECK(checked_scope_.is_active());
  annotation_->set_pointer_value(reinterpret_cast<uint64_t>(value));
}

TracedDictionary TracedValue::WriteDictionary() && {
  // Note: this passes |checked_scope_.is_active_| bit to the parent to be
  // picked up later by the new TracedDictionary.
  PERFETTO_DCHECK(checked_scope_.is_active());
  checked_scope_.Reset();

  PERFETTO_DCHECK(!annotation_->is_finalized());
  return TracedDictionary(annotation_,
                          protos::pbzero::DebugAnnotation::kDictEntries,
                          event_context_, checked_scope_.parent_scope());
}

TracedArray TracedValue::WriteArray() && {
  // Note: this passes |checked_scope_.is_active_| bit to the parent to be
  // picked up later by the new TracedDictionary.
  PERFETTO_DCHECK(checked_scope_.is_active());
  checked_scope_.Reset();

  PERFETTO_DCHECK(!annotation_->is_finalized());
  return TracedArray(annotation_, event_context_,
                     checked_scope_.parent_scope());
}

protozero::Message* TracedValue::WriteProtoInternal(const char* name) {
  if (event_context_) {
    annotation_->set_proto_type_name_iid(
        internal::InternedDebugAnnotationValueTypeName::Get(event_context_,
                                                            name));
  } else {
    annotation_->set_proto_type_name(name);
  }
  return annotation_->template BeginNestedMessage<protozero::Message>(
      protos::pbzero::DebugAnnotation::kProtoValueFieldNumber);
}

TracedArray::TracedArray(TracedValue annotation)
    : TracedArray(std::move(annotation).WriteArray()) {}

TracedValue TracedArray::AppendItem() {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return TracedValue(annotation_->add_array_values(), event_context_,
                     &checked_scope_);
}

TracedDictionary TracedArray::AppendDictionary() {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return AppendItem().WriteDictionary();
}

TracedArray TracedArray::AppendArray() {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return AppendItem().WriteArray();
}

TracedDictionary::TracedDictionary(TracedValue annotation)
    : TracedDictionary(std::move(annotation).WriteDictionary()) {}

TracedValue TracedDictionary::AddItem(StaticString key) {
  PERFETTO_DCHECK(checked_scope_.is_active());
  protos::pbzero::DebugAnnotation* item =
      message_->BeginNestedMessage<protos::pbzero::DebugAnnotation>(field_id_);
  item->set_name(key.value);
  return TracedValue(item, event_context_, &checked_scope_);
}

TracedValue TracedDictionary::AddItem(DynamicString key) {
  PERFETTO_DCHECK(checked_scope_.is_active());
  protos::pbzero::DebugAnnotation* item =
      message_->BeginNestedMessage<protos::pbzero::DebugAnnotation>(field_id_);
  item->set_name(key.value);
  return TracedValue(item, event_context_, &checked_scope_);
}

TracedDictionary TracedDictionary::AddDictionary(StaticString key) {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return AddItem(key).WriteDictionary();
}

TracedDictionary TracedDictionary::AddDictionary(DynamicString key) {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return AddItem(key).WriteDictionary();
}

TracedArray TracedDictionary::AddArray(StaticString key) {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return AddItem(key).WriteArray();
}

TracedArray TracedDictionary::AddArray(DynamicString key) {
  PERFETTO_DCHECK(checked_scope_.is_active());
  return AddItem(key).WriteArray();
}

}  // namespace perfetto
