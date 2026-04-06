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

#include "src/trace_processor/util/proto_profiler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/util/descriptors.h"

#include "protos/perfetto/common/descriptor.pbzero.h"

namespace perfetto::trace_processor::util {

namespace {
using ::perfetto::protos::pbzero::FieldDescriptorProto;
using ::protozero::proto_utils::ProtoWireType;

// Takes a type full name, and returns only the final part.
// For example, .perfetto.protos.TracePacket -> TracePacket
std::string GetFieldTypeName(const std::string& full_type_name) {
  auto pos = full_type_name.rfind('.');
  if (pos == std::string::npos) {
    return full_type_name;
  }
  return full_type_name.substr(pos + 1);
}

std::string GetLeafTypeName(uint32_t type_id) {
  std::string raw_name = FieldDescriptorProto::Type_Name(
      static_cast<FieldDescriptorProto::Type>(type_id));
  return base::StripPrefix(base::ToLower(raw_name), "type_");
}

}  // namespace

SizeProfileComputer::Field::Field(uint32_t field_idx_in,
                                  const FieldDescriptor* field_descriptor_in,
                                  uint32_t type_in,
                                  const ProtoDescriptor* proto_descriptor_in)
    : field_idx(field_idx_in),
      type(type_in),
      field_descriptor(field_descriptor_in),
      proto_descriptor(proto_descriptor_in) {}

std::string SizeProfileComputer::Field::field_name() const {
  if (field_descriptor)
    return "#" + field_descriptor->name();
  return "#unknown";
}

std::string SizeProfileComputer::Field::type_name() const {
  if (proto_descriptor)
    return GetFieldTypeName(proto_descriptor->full_name());
  return GetLeafTypeName(type);
}

SizeProfileComputer::SizeProfileComputer(DescriptorPool* pool,
                                         const std::string& message_type)
    : pool_(pool) {
  auto message_idx = pool_->FindDescriptorIdx(message_type);
  if (!message_idx) {
    PERFETTO_ELOG("Cannot find descriptor for type %s", message_type.c_str());
    return;
  }
  root_message_idx_ = *message_idx;
}

void SizeProfileComputer::Reset(const uint8_t* ptr, size_t size) {
  state_stack_.clear();
  field_path_.fields.clear();
  protozero::ProtoDecoder decoder(ptr, size);
  const ProtoDescriptor* descriptor = &pool_->descriptors()[root_message_idx_];
  state_stack_.push_back(State{descriptor, decoder, size, 0});
  field_path_.fields.emplace_back(0, nullptr, root_message_idx_, descriptor);
}

std::optional<size_t> SizeProfileComputer::GetNext() {
  std::optional<size_t> result;
  if (state_stack_.empty())
    return result;

  if (field_path_.fields.size() > state_stack_.size()) {
    // The leaf path needs to be popped to continue iterating on the current
    // proto.
    field_path_.fields.pop_back();
    PERFETTO_DCHECK(field_path_.fields.size() == state_stack_.size());
  }
  State& state = state_stack_.back();

  for (;;) {
    if (state.decoder.bytes_left() == 0) {
      break;
    }

    protozero::Field field = state.decoder.ReadField();
    if (!field.valid()) {
      PERFETTO_ELOG("Field not valid (can mean field id >1000)");
      break;
    }

    ProtoWireType type = field.type();
    size_t field_size = GetFieldSize(field);

    state.overhead -= field_size;
    const FieldDescriptor* field_descriptor =
        state.descriptor->FindFieldByTag(field.id());
    if (!field_descriptor) {
      state.unknown += field_size;
      continue;
    }

    bool is_message_type =
        field_descriptor->type() == FieldDescriptorProto::TYPE_MESSAGE;
    if (type == ProtoWireType::kLengthDelimited && is_message_type) {
      auto message_idx =
          pool_->FindDescriptorIdx(field_descriptor->resolved_type_name());

      if (!message_idx) {
        PERFETTO_ELOG("Cannot find descriptor for type %s",
                      field_descriptor->resolved_type_name().c_str());
        return result;
      }

      protozero::ProtoDecoder decoder(field.data(), field.size());
      const ProtoDescriptor* descriptor = &pool_->descriptors()[*message_idx];
      field_path_.fields.emplace_back(field.id(), field_descriptor,
                                      *message_idx, descriptor);
      state_stack_.push_back(State{descriptor, decoder, field.size(), 0U});
      return GetNext();
    }
    field_path_.fields.emplace_back(field.id(), field_descriptor,
                                    field_descriptor->type(), nullptr);
    result.emplace(field_size);
    return result;
  }
  if (state.unknown) {
    field_path_.fields.emplace_back(uint32_t(-1), nullptr, 0U, nullptr);
    result.emplace(state.unknown);
    state.unknown = 0;
    return result;
  }

  result.emplace(state.overhead);
  state_stack_.pop_back();
  return result;
}

size_t SizeProfileComputer::GetFieldSize(const protozero::Field& f) {
  uint8_t buf[10];
  switch (f.type()) {
    case protozero::proto_utils::ProtoWireType::kVarInt:
      return static_cast<size_t>(
          protozero::proto_utils::WriteVarInt(f.as_uint64(), buf) - buf);
    case protozero::proto_utils::ProtoWireType::kLengthDelimited:
      return f.size();
    case protozero::proto_utils::ProtoWireType::kFixed32:
      return 4;
    case protozero::proto_utils::ProtoWireType::kFixed64:
      return 8;
  }
  PERFETTO_FATAL("unexpected field type");  // for gcc
}

}  // namespace perfetto::trace_processor::util
