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
#include <string_view>
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

base::Status ProtoToArgsParser::ParseMessage(
    const protozero::ConstBytes& cb,
    const std::string& type,
    const std::vector<uint32_t>* allowed_fields,
    Delegate& delegate,
    int* unknown_extensions,
    bool add_defaults) {
  ScopedNestedKeyContext key_context(key_prefix_);
  return ParseMessageInternal(key_context, cb, type, allowed_fields, delegate,
                              unknown_extensions, add_defaults);
}

base::Status ProtoToArgsParser::ParseMessageInternal(
    ScopedNestedKeyContext& key_context,
    const protozero::ConstBytes& cb,
    const std::string& type,
    const std::vector<uint32_t>* allowed_fields,
    Delegate& delegate,
    int* unknown_extensions,
    bool add_defaults) {
  if (auto override_result =
          MaybeApplyOverrideForType(type, key_context, cb, delegate)) {
    return override_result.value();
  }

  auto idx = pool_.FindDescriptorIdx(type);
  if (!idx) {
    return base::Status("Failed to find proto descriptor");
  }

  const auto& descriptor = pool_.descriptors()[*idx];

  std::unordered_map<size_t, int> repeated_field_index;
  bool empty_message = true;
  protozero::ProtoDecoder decoder(cb);
  std::unordered_set<std::string_view> existing_fields;
  for (protozero::Field f = decoder.ReadField(); f.valid();
       f = decoder.ReadField()) {
    empty_message = false;
    const auto* field = descriptor.FindFieldByTag(f.id());
    if (!field) {
      if (unknown_extensions != nullptr) {
        (*unknown_extensions)++;
      }
      // Unknown field, possibly an unknown extension.
      continue;
    }

    if (add_defaults) {
      existing_fields.insert(field->name());
    }

    if (!IsFieldAllowed(*field, allowed_fields)) {
      // Field is neither an extension, nor is allowed to be
      // reflected.
      continue;
    }

    // Packed fields need to be handled specially because
    if (field->is_packed()) {
      RETURN_IF_ERROR(ParsePackedField(*field, repeated_field_index, f,
                                       delegate, unknown_extensions,
                                       add_defaults));
      continue;
    }

    RETURN_IF_ERROR(ParseField(*field, repeated_field_index[f.id()], f,
                               delegate, unknown_extensions, add_defaults));
    if (field->is_repeated()) {
      repeated_field_index[f.id()]++;
    }
  }

  if (empty_message) {
    delegate.AddNull(key_prefix_);
  } else if (add_defaults) {
    for (const auto& [id, field] : descriptor.fields()) {
      if (!IsFieldAllowed(field, allowed_fields)) {
        continue;
      }
      const std::string& field_name = field.name();
      bool field_exists =
          existing_fields.find(field_name) != existing_fields.cend();
      if (field_exists) {
        continue;
      }
      ScopedNestedKeyContext key_context_default(key_prefix_);
      AppendProtoType(key_prefix_.flat_key, field_name);
      AppendProtoType(key_prefix_.key, field_name);
      RETURN_IF_ERROR(AddDefault(field, delegate));
    }
  }

  return base::OkStatus();
}

base::Status ProtoToArgsParser::ParseField(
    const FieldDescriptor& field_descriptor,
    int repeated_field_number,
    protozero::Field field,
    Delegate& delegate,
    int* unknown_extensions,
    bool add_defaults) {
  std::string prefix_part = field_descriptor.name();
  if (field_descriptor.is_repeated()) {
    std::string number = std::to_string(repeated_field_number);
    prefix_part.reserve(prefix_part.length() + number.length() + 2);
    prefix_part.append("[");
    prefix_part.append(number);
    prefix_part.append("]");
  }

  // In the args table we build up message1.message2.field1 as the column
  // name. This will append the ".field1" suffix to |key_prefix| and then
  // remove it when it goes out of scope.
  ScopedNestedKeyContext key_context(key_prefix_);
  AppendProtoType(key_prefix_.flat_key, field_descriptor.name());
  AppendProtoType(key_prefix_.key, prefix_part);

  // If we have an override parser then use that instead and move onto the
  // next loop.
  if (std::optional<base::Status> status =
          MaybeApplyOverrideForField(field, delegate)) {
    return *status;
  }

  // If this is not a message we can just immediately add the column name and
  // get the value out of |field|. However if it is a message we need to
  // recurse into it.
  if (field_descriptor.type() ==
      protos::pbzero::FieldDescriptorProto::TYPE_MESSAGE) {
    return ParseMessageInternal(key_context, field.as_bytes(),
                                field_descriptor.resolved_type_name(), nullptr,
                                delegate, unknown_extensions, add_defaults);
  }
  return ParseSimpleField(field_descriptor, field, delegate);
}

base::Status ProtoToArgsParser::ParsePackedField(
    const FieldDescriptor& field_descriptor,
    std::unordered_map<size_t, int>& repeated_field_index,
    protozero::Field field,
    Delegate& delegate,
    int* unknown_extensions,
    bool add_defaults) {
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
    return ParseField(field_descriptor, repeated_field_index[field.id()]++, f,
                      delegate, unknown_extensions, add_defaults);
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
