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

#include "src/trace_processor/util/proto_to_args_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/util/descriptors.h"

#include "protos/perfetto/common/descriptor.pbzero.h"

namespace perfetto::trace_processor::util {

namespace {

template <protozero::proto_utils::ProtoWireType wire_type, typename cpp_type>
using PRFI = protozero::PackedRepeatedFieldIterator<wire_type, cpp_type>;

void AppendProtoType(std::string& target, const std::string& value) {
  if (!target.empty())
    target += '.';
  target += value;
}

bool IsFieldAllowed(const FieldDescriptor& field,
                    const std::vector<uint32_t>* allowed_fields) {
  // If allowlist is not provided, reflect all fields. Otherwise, check if the
  // current field either an extension or is in allowlist.
  return field.is_extension() || !allowed_fields ||
         std::find(allowed_fields->begin(), allowed_fields->end(),
                   field.number()) != allowed_fields->end();
}

}  // namespace

ProtoToArgsParser::Key::Key() = default;
ProtoToArgsParser::Key::Key(const std::string& k) : flat_key(k), key(k) {}
ProtoToArgsParser::Key::Key(const std::string& fk, const std::string& k)
    : flat_key(fk), key(k) {}
ProtoToArgsParser::Key::~Key() = default;

ProtoToArgsParser::ScopedNestedKeyContext::ScopedNestedKeyContext(Key& key)
    : key_(key),
      old_flat_key_length_(key.flat_key.length()),
      old_key_length_(key.key.length()) {}

ProtoToArgsParser::ScopedNestedKeyContext::ScopedNestedKeyContext(
    ProtoToArgsParser::ScopedNestedKeyContext&& other) noexcept
    : key_(other.key_),
      old_flat_key_length_(other.old_flat_key_length_),
      old_key_length_(other.old_key_length_) {
  other.old_flat_key_length_ = std::nullopt;
  other.old_key_length_ = std::nullopt;
}

ProtoToArgsParser::ScopedNestedKeyContext::~ScopedNestedKeyContext() {
  RemoveFieldSuffix();
}

void ProtoToArgsParser::ScopedNestedKeyContext::RemoveFieldSuffix() {
  if (old_flat_key_length_)
    key_.flat_key.resize(old_flat_key_length_.value());
  if (old_key_length_)
    key_.key.resize(old_key_length_.value());
  old_flat_key_length_ = std::nullopt;
  old_key_length_ = std::nullopt;
}

ProtoToArgsParser::Delegate::~Delegate() = default;

ProtoToArgsParser::ProtoToArgsParser(const DescriptorPool& pool) : pool_(pool) {
  constexpr int kDefaultSize = 64;
  key_prefix_.key.reserve(kDefaultSize);
  key_prefix_.flat_key.reserve(kDefaultSize);
}

struct ProtoToArgsParser::WorkItem {
  // The serialized data for the current message.
  protozero::ConstBytes data;

  // The decoder for the current message. Its internal state marks our
  // progress through this message's fields.
  protozero::ProtoDecoder decoder;

  // The descriptor for the current message type.
  const ProtoDescriptor* descriptor;

  // A map to track the index of repeated fields *at this nesting level*.
  std::unordered_map<size_t, int> repeated_field_index;

  // The set of fields seen in this message, for handling defaults.
  std::unordered_set<uint32_t> existing_fields;

  // The RAII context for the current message's key. Its destructor will be
  // called automatically when this WorkItem is popped from the stack,
  // ensuring the key prefix is correctly managed.
  ScopedNestedKeyContext key_context;

  // Set to false as soon as any field is parsed for this message.
  bool empty_message = true;
};

base::Status ProtoToArgsParser::ParseMessage(
    const protozero::ConstBytes& cb,
    const std::string& type,
    const std::vector<uint32_t>* allowed_fields,
    Delegate& delegate,
    int* unknown_extensions,
    bool add_defaults) {
  std::vector<WorkItem> work_stack;
  {
    auto idx = pool_.FindDescriptorIdx(type);
    if (!idx) {
      return base::Status("Failed to find proto descriptor for " + type);
    }
    work_stack.emplace_back(WorkItem{cb,
                                     protozero::ProtoDecoder(cb),
                                     &pool_.descriptors()[*idx],
                                     {},
                                     {},
                                     ScopedNestedKeyContext(key_prefix_),
                                     true});
  }

  while (!work_stack.empty()) {
    WorkItem& item = work_stack.back();
    if (auto override_result =
            MaybeApplyOverrideForType(item.descriptor->full_name(),
                                      item.key_context, item.data, delegate)) {
      work_stack.pop_back();
      RETURN_IF_ERROR(override_result.value());
      continue;
    }

    protozero::Field field = item.decoder.ReadField();
    if (field.valid()) {
      item.empty_message = false;
      const auto* field_descriptor =
          item.descriptor->FindFieldByTag(field.id());
      if (!field_descriptor) {
        if (unknown_extensions != nullptr) {
          (*unknown_extensions)++;
        }
        // Unknown field, possibly an unknown extension.
        continue;
      }

      if (add_defaults) {
        item.existing_fields.insert(field_descriptor->number());
      }

      // The allowlist only applies to the top-level message.
      if (work_stack.size() == 1 &&
          !IsFieldAllowed(*field_descriptor, allowed_fields)) {
        // Field is neither an extension, nor is allowed to be
        // reflected.
        continue;
      }

      // Detect packed fields based on the serialized wire type instead of the
      // descriptor flag to tolerate proto/descriptor mismatches.
      using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
      using PWT = protozero::proto_utils::ProtoWireType;
      const auto descriptor_type = field_descriptor->type();
      const bool is_length_delimited = field.type() == PWT::kLengthDelimited;
      const bool looks_packed =
          field_descriptor->is_repeated() && is_length_delimited &&
          descriptor_type != FieldDescriptorProto::TYPE_MESSAGE &&
          descriptor_type != FieldDescriptorProto::TYPE_STRING &&
          descriptor_type != FieldDescriptorProto::TYPE_BYTES;
      if (looks_packed) {
        RETURN_IF_ERROR(ParsePackedField(
            *field_descriptor, item.repeated_field_index, field, delegate));
        continue;
      }

      ScopedNestedKeyContext field_key_context(key_prefix_);
      AppendProtoType(key_prefix_.flat_key, field_descriptor->name());
      if (field_descriptor->is_repeated()) {
        std::string prefix_part = field_descriptor->name();
        int& index = item.repeated_field_index[field.id()];
        std::string number = std::to_string(index);
        prefix_part.reserve(prefix_part.length() + number.length() + 2);
        prefix_part.append("[");
        prefix_part.append(number);
        prefix_part.append("]");
        index++;
        AppendProtoType(key_prefix_.key, prefix_part);
      } else {
        AppendProtoType(key_prefix_.key, field_descriptor->name());
      }

      if (std::optional<base::Status> status =
              MaybeApplyOverrideForField(field, delegate)) {
        RETURN_IF_ERROR(*status);
        continue;
      }

      if (field_descriptor->type() ==
          protos::pbzero::FieldDescriptorProto::TYPE_MESSAGE) {
        auto desc_idx =
            pool_.FindDescriptorIdx(field_descriptor->resolved_type_name());
        if (!desc_idx) {
          return base::ErrStatus(
              "Failed to find proto descriptor for %s",
              field_descriptor->resolved_type_name().c_str());
        }
        work_stack.emplace_back(
            WorkItem{field.as_bytes(),
                     protozero::ProtoDecoder(field.as_bytes()),
                     &pool_.descriptors()[*desc_idx],
                     {},
                     {},
                     std::move(field_key_context),
                     true});
      } else {
        RETURN_IF_ERROR(ParseSimpleField(*field_descriptor, field, delegate));
      }
      continue;
    }

    if (add_defaults) {
      for (const auto& [id, field_desc] : item.descriptor->fields()) {
        if (work_stack.size() == 1 &&
            !IsFieldAllowed(field_desc, allowed_fields)) {
          continue;
        }
        bool field_exists = item.existing_fields.find(field_desc.number()) !=
                            item.existing_fields.cend();
        if (field_exists) {
          continue;
        }
        const std::string& field_name = field_desc.name();
        ScopedNestedKeyContext key_context_default(key_prefix_);
        AppendProtoType(key_prefix_.flat_key, field_name);
        AppendProtoType(key_prefix_.key, field_name);
        RETURN_IF_ERROR(AddDefault(field_desc, delegate));
        item.empty_message = false;
      }
    }
    if (item.empty_message) {
      delegate.AddNull(item.key_context.key());
    }
    work_stack.pop_back();
  }

  return base::OkStatus();
}

base::Status ProtoToArgsParser::ParsePackedField(
    const FieldDescriptor& field_descriptor,
    std::unordered_map<size_t, int>& repeated_field_index,
    protozero::Field field,
    Delegate& delegate) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  using PWT = protozero::proto_utils::ProtoWireType;

  if (!field_descriptor.is_repeated()) {
    return base::ErrStatus("Packed field %s must be repeated",
                           field_descriptor.name().c_str());
  }
  if (field.type() != PWT::kLengthDelimited) {
    return base::ErrStatus(
        "Packed field %s must have a length delimited wire type",
        field_descriptor.name().c_str());
  }

  auto parse = [&](uint64_t new_value, PWT wire_type) {
    protozero::Field f;
    f.initialize(field.id(), static_cast<uint8_t>(wire_type), new_value, 0);

    std::string prefix_part = field_descriptor.name();
    int& index = repeated_field_index[field.id()];
    std::string number = std::to_string(index);
    prefix_part.reserve(prefix_part.length() + number.length() + 2);
    prefix_part.append("[");
    prefix_part.append(number);
    prefix_part.append("]");
    index++;

    ScopedNestedKeyContext key_context(key_prefix_);
    AppendProtoType(key_prefix_.flat_key, field_descriptor.name());
    AppendProtoType(key_prefix_.key, prefix_part);

    if (std::optional<base::Status> status =
            MaybeApplyOverrideForField(f, delegate)) {
      return *status;
    }
    return ParseSimpleField(field_descriptor, f, delegate);
  };

  const uint8_t* data = field.as_bytes().data;
  size_t size = field.as_bytes().size;
  bool perr = false;
  switch (field_descriptor.type()) {
    case FieldDescriptorProto::TYPE_INT32:
    case FieldDescriptorProto::TYPE_INT64:
    case FieldDescriptorProto::TYPE_UINT32:
    case FieldDescriptorProto::TYPE_UINT64:
    case FieldDescriptorProto::TYPE_ENUM:
      for (PRFI<PWT::kVarInt, uint64_t> it(data, size, &perr); it; ++it) {
        parse(*it, PWT::kVarInt);
      }
      break;
    case FieldDescriptorProto::TYPE_FIXED32:
    case FieldDescriptorProto::TYPE_SFIXED32:
    case FieldDescriptorProto::TYPE_FLOAT:
      for (PRFI<PWT::kFixed32, uint32_t> it(data, size, &perr); it; ++it) {
        parse(*it, PWT::kFixed32);
      }
      break;
    case FieldDescriptorProto::TYPE_FIXED64:
    case FieldDescriptorProto::TYPE_SFIXED64:
    case FieldDescriptorProto::TYPE_DOUBLE:
      for (PRFI<PWT::kFixed64, uint64_t> it(data, size, &perr); it; ++it) {
        parse(*it, PWT::kFixed64);
      }
      break;
    default:
      return base::ErrStatus("Unsupported packed repeated field");
  }
  return base::OkStatus();
}

void ProtoToArgsParser::AddParsingOverrideForField(
    const std::string& field,
    ParsingOverrideForField func) {
  field_overrides_[field] = std::move(func);
}

void ProtoToArgsParser::AddParsingOverrideForType(const std::string& type,
                                                  ParsingOverrideForType func) {
  type_overrides_[type] = std::move(func);
}

std::optional<base::Status> ProtoToArgsParser::MaybeApplyOverrideForField(
    const protozero::Field& field,
    Delegate& delegate) {
  auto it = field_overrides_.find(key_prefix_.flat_key);
  if (it == field_overrides_.end())
    return std::nullopt;
  return it->second(field, delegate);
}

std::optional<base::Status> ProtoToArgsParser::MaybeApplyOverrideForType(
    const std::string& message_type,
    ScopedNestedKeyContext& key,
    const protozero::ConstBytes& data,
    Delegate& delegate) {
  auto it = type_overrides_.find(message_type);
  if (it == type_overrides_.end())
    return std::nullopt;
  return it->second(key, data, delegate);
}

base::Status ProtoToArgsParser::ParseSimpleField(
    const FieldDescriptor& descriptor,
    const protozero::Field& field,
    Delegate& delegate) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  switch (descriptor.type()) {
    case FieldDescriptorProto::TYPE_INT32:
    case FieldDescriptorProto::TYPE_SFIXED32:
      delegate.AddInteger(key_prefix_, field.as_int32());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_SINT32:
      delegate.AddInteger(key_prefix_, field.as_sint32());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_INT64:
    case FieldDescriptorProto::TYPE_SFIXED64:
      delegate.AddInteger(key_prefix_, field.as_int64());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_SINT64:
      delegate.AddInteger(key_prefix_, field.as_sint64());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_UINT32:
    case FieldDescriptorProto::TYPE_FIXED32:
      delegate.AddUnsignedInteger(key_prefix_, field.as_uint32());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_UINT64:
    case FieldDescriptorProto::TYPE_FIXED64:
      delegate.AddUnsignedInteger(key_prefix_, field.as_uint64());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_BOOL:
      delegate.AddBoolean(key_prefix_, field.as_bool());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_DOUBLE:
      delegate.AddDouble(key_prefix_, field.as_double());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_FLOAT:
      delegate.AddDouble(key_prefix_, static_cast<double>(field.as_float()));
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_BYTES:
      delegate.AddBytes(key_prefix_, field.as_bytes());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_STRING:
      delegate.AddString(key_prefix_, field.as_string());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_ENUM:
      return AddEnum(descriptor, field.as_int32(), delegate);
    default:
      return base::ErrStatus(
          "Tried to write value of type field %s (in proto type "
          "%s) which has type enum %u",
          descriptor.name().c_str(), descriptor.resolved_type_name().c_str(),
          descriptor.type());
  }
}

ProtoToArgsParser::ScopedNestedKeyContext ProtoToArgsParser::EnterArray(
    size_t index) {
  ScopedNestedKeyContext context(key_prefix_);
  key_prefix_.key += "[" + std::to_string(index) + "]";
  return context;
}

ProtoToArgsParser::ScopedNestedKeyContext ProtoToArgsParser::EnterDictionary(
    const std::string& name) {
  ScopedNestedKeyContext context(key_prefix_);
  AppendProtoType(key_prefix_.key, name);
  AppendProtoType(key_prefix_.flat_key, name);
  return context;
}

base::Status ProtoToArgsParser::AddDefault(const FieldDescriptor& descriptor,
                                           Delegate& delegate) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  if (!delegate.ShouldAddDefaultArg(key_prefix_)) {
    return base::OkStatus();
  }
  if (descriptor.is_repeated()) {
    delegate.AddNull(key_prefix_);
    return base::OkStatus();
  }
  const auto& default_value = descriptor.default_value();
  const auto& default_value_if_number =
      default_value ? default_value.value() : "0";
  switch (descriptor.type()) {
    case FieldDescriptorProto::TYPE_INT32:
    case FieldDescriptorProto::TYPE_SFIXED32:
      delegate.AddInteger(key_prefix_,
                          base::StringToInt32(default_value_if_number).value());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_SINT32:
      delegate.AddInteger(
          key_prefix_,
          protozero::proto_utils::ZigZagDecode(
              base::StringToInt64(default_value_if_number).value()));
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_INT64:
    case FieldDescriptorProto::TYPE_SFIXED64:
      delegate.AddInteger(key_prefix_,
                          base::StringToInt64(default_value_if_number).value());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_SINT64:
      delegate.AddInteger(
          key_prefix_,
          protozero::proto_utils::ZigZagDecode(
              base::StringToInt64(default_value_if_number).value()));
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_UINT32:
    case FieldDescriptorProto::TYPE_FIXED32:
      delegate.AddUnsignedInteger(
          key_prefix_, base::StringToUInt32(default_value_if_number).value());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_UINT64:
    case FieldDescriptorProto::TYPE_FIXED64:
      delegate.AddUnsignedInteger(
          key_prefix_, base::StringToUInt64(default_value_if_number).value());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_BOOL:
      delegate.AddBoolean(key_prefix_, default_value == "true");
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_DOUBLE:
    case FieldDescriptorProto::TYPE_FLOAT:
      delegate.AddDouble(key_prefix_,
                         base::StringToDouble(default_value_if_number).value());
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_BYTES:
      delegate.AddBytes(key_prefix_, protozero::ConstBytes{});
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_STRING:
      if (default_value) {
        delegate.AddString(key_prefix_, default_value.value());
      } else {
        delegate.AddNull(key_prefix_);
      }
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_MESSAGE:
      delegate.AddNull(key_prefix_);
      return base::OkStatus();
    case FieldDescriptorProto::TYPE_ENUM:
      return AddEnum(descriptor,
                     base::StringToInt32(default_value_if_number).value(),
                     delegate);
    default:
      return base::ErrStatus(
          "Tried to write default value of type field %s (in proto type "
          "%s) which has type enum %u",
          descriptor.name().c_str(), descriptor.resolved_type_name().c_str(),
          descriptor.type());
  }
}

base::Status ProtoToArgsParser::AddEnum(const FieldDescriptor& descriptor,
                                        int32_t value,
                                        Delegate& delegate) {
  auto opt_enum_descriptor_idx =
      pool_.FindDescriptorIdx(descriptor.resolved_type_name());
  if (!opt_enum_descriptor_idx) {
    // Fall back to the integer representation of the field.
    // We add the string representation of the int value here in order that
    // EXTRACT_ARG() should return consistent types under error conditions and
    // that CREATE PERFETTO TABLE AS EXTRACT_ARG(...) should be generally safe
    // to use.
    delegate.AddString(key_prefix_, std::to_string(value));
    return base::OkStatus();
  }
  auto opt_enum_string =
      pool_.descriptors()[*opt_enum_descriptor_idx].FindEnumString(value);
  if (!opt_enum_string) {
    // Fall back to the integer representation of the field. See above for
    // motivation.
    delegate.AddString(key_prefix_, std::to_string(value));
    return base::OkStatus();
  }
  delegate.AddString(
      key_prefix_,
      protozero::ConstChars{opt_enum_string->data(), opt_enum_string->size()});
  return base::OkStatus();
}
}  // namespace perfetto::trace_processor::util
