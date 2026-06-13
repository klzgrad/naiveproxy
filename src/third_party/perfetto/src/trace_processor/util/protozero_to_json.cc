/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/util/protozero_to_json.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/json_serializer.h"

#include "protos/perfetto/common/descriptor.pbzero.h"

namespace perfetto::trace_processor::protozero_to_json {

namespace {

using protos::pbzero::FieldDescriptorProto;
using protozero::PackedRepeatedFieldIterator;
using protozero::proto_utils::ProtoWireType;

// Wrapper around JsonSerializer that adds error tracking for protozero parsing.
class JsonBuilder {
 public:
  explicit JsonBuilder(int flags)
      : serializer_(flags & protozero_to_json::Flags::kPretty
                        ? json::JsonSerializer::kPretty
                        : json::JsonSerializer::kNone),
        flags_(flags) {}

  json::JsonSerializer& serializer() { return serializer_; }
  std::string ToString() const { return serializer_.ToString(); }

  void AddError(const std::string& s) { errors_.push_back(s); }
  bool is_inline_errors() const {
    return flags_ & protozero_to_json::Flags::kInlineErrors;
  }
  const std::vector<std::string>& errors() const { return errors_; }

 private:
  json::JsonSerializer serializer_;
  int flags_;
  std::vector<std::string> errors_;
};

bool HasFieldOptions(const FieldDescriptor& field_desc) {
  return !field_desc.options().empty();
}

std::string FulllyQualifiedFieldName(const ProtoDescriptor& desc,
                                     const FieldDescriptor& field_desc) {
  return desc.package_name().substr(1) + "." + field_desc.name();
}

bool IsTypeMatch(ProtoWireType wire, uint32_t type) {
  switch (wire) {
    case ProtoWireType::kVarInt:
      switch (type) {
        case FieldDescriptorProto::TYPE_INT32:
        case FieldDescriptorProto::TYPE_SINT32:
        case FieldDescriptorProto::TYPE_UINT32:
        case FieldDescriptorProto::TYPE_INT64:
        case FieldDescriptorProto::TYPE_SINT64:
        case FieldDescriptorProto::TYPE_UINT64:
        case FieldDescriptorProto::TYPE_BOOL:
        case FieldDescriptorProto::TYPE_ENUM:
          return true;
        default:
          return false;
      }
    case ProtoWireType::kLengthDelimited:
      switch (type) {
        case FieldDescriptorProto::TYPE_BYTES:
        case FieldDescriptorProto::TYPE_MESSAGE:
        case FieldDescriptorProto::TYPE_STRING:
          // The normal case.
          return true;
        case FieldDescriptorProto::TYPE_INT32:
        case FieldDescriptorProto::TYPE_SINT32:
        case FieldDescriptorProto::TYPE_UINT32:
        case FieldDescriptorProto::TYPE_INT64:
        case FieldDescriptorProto::TYPE_SINT64:
        case FieldDescriptorProto::TYPE_UINT64:
        case FieldDescriptorProto::TYPE_BOOL:
        case FieldDescriptorProto::TYPE_ENUM:
        case FieldDescriptorProto::TYPE_FIXED32:
        case FieldDescriptorProto::TYPE_SFIXED32:
        case FieldDescriptorProto::TYPE_FLOAT:
        case FieldDescriptorProto::TYPE_FIXED64:
        case FieldDescriptorProto::TYPE_SFIXED64:
        case FieldDescriptorProto::TYPE_DOUBLE:
          // Packed repeated fields.
          return true;
        default:
          return false;
      }
    case ProtoWireType::kFixed32:
      switch (type) {
        case FieldDescriptorProto::TYPE_FIXED32:
        case FieldDescriptorProto::TYPE_SFIXED32:
        case FieldDescriptorProto::TYPE_FLOAT:
          return true;
        default:
          return false;
      }
    case ProtoWireType::kFixed64:
      switch (type) {
        case FieldDescriptorProto::TYPE_FIXED64:
        case FieldDescriptorProto::TYPE_SFIXED64:
        case FieldDescriptorProto::TYPE_DOUBLE:
          return true;
        default:
          return false;
      }
  }
  PERFETTO_FATAL("For GCC");
}

bool IsNumericFieldType(uint32_t type) {
  switch (type) {
    case FieldDescriptorProto::TYPE_BYTES:
    case FieldDescriptorProto::TYPE_MESSAGE:
    case FieldDescriptorProto::TYPE_STRING:
      return false;
    case FieldDescriptorProto::TYPE_INT32:
    case FieldDescriptorProto::TYPE_SINT32:
    case FieldDescriptorProto::TYPE_UINT32:
    case FieldDescriptorProto::TYPE_INT64:
    case FieldDescriptorProto::TYPE_SINT64:
    case FieldDescriptorProto::TYPE_UINT64:
    case FieldDescriptorProto::TYPE_BOOL:
    case FieldDescriptorProto::TYPE_ENUM:
    case FieldDescriptorProto::TYPE_FIXED32:
    case FieldDescriptorProto::TYPE_SFIXED32:
    case FieldDescriptorProto::TYPE_FLOAT:
    case FieldDescriptorProto::TYPE_FIXED64:
    case FieldDescriptorProto::TYPE_SFIXED64:
    case FieldDescriptorProto::TYPE_DOUBLE:
    default:
      return true;
  }
}

void MessageField(const DescriptorPool& pool,
                  const std::string& type,
                  protozero::ConstBytes protobytes,
                  bool fully_qualify_extensions,
                  JsonBuilder* out);
void EnumField(const DescriptorPool& pool,
               const FieldDescriptor& fd,
               int32_t value,
               JsonBuilder* out);

template <ProtoWireType W, typename T>
void PackedField(const DescriptorPool& pool,
                 const FieldDescriptor& fd,
                 const protozero::Field& field,
                 JsonBuilder* out) {
  out->serializer().OpenArray();
  bool e = false;
  for (PackedRepeatedFieldIterator<W, T> it(field.data(), field.size(), &e); it;
       it++) {
    T value = *it;
    if (fd.type() == FieldDescriptorProto::TYPE_ENUM) {
      EnumField(pool, fd, static_cast<int32_t>(value), out);
    } else {
      out->serializer().NumberValue<T>(value);
    }
  }
  out->serializer().CloseArray();
  if (e) {
    out->AddError(
        std::string("Decoding failure for field '" + fd.name() + "'"));
  }
}

template <ProtoWireType W>
void PackedBoolField(const DescriptorPool&,
                     const FieldDescriptor& fd,
                     const protozero::Field& field,
                     JsonBuilder* out) {
  out->serializer().OpenArray();
  bool e = false;
  for (PackedRepeatedFieldIterator<W, int32_t> it(field.data(), field.size(),
                                                  &e);
       it; it++) {
    bool value = *it;
    out->serializer().BoolValue(value);
  }
  out->serializer().CloseArray();
  if (e) {
    out->AddError(
        std::string("Decoding failure for field '" + fd.name() + "'"));
  }
}

void LengthField(const DescriptorPool& pool,
                 const FieldDescriptor* fd,
                 const protozero::Field& field,
                 bool fully_qualify_extensions,
                 JsonBuilder* out) {
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_BYTES: {
      auto s = field.as_string();
      out->serializer().StringValue(std::string_view(s.data, s.size));
      return;
    }
    case FieldDescriptorProto::TYPE_STRING: {
      auto s = field.as_string();
      out->serializer().StringValue(std::string_view(s.data, s.size));
      return;
    }
    case FieldDescriptorProto::TYPE_MESSAGE:
      MessageField(pool, fd->resolved_type_name(), field.as_bytes(),
                   fully_qualify_extensions, out);
      return;
    case FieldDescriptorProto::TYPE_DOUBLE:
      PackedField<ProtoWireType::kFixed64, double>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_FLOAT:
      PackedField<ProtoWireType::kFixed32, float>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_FIXED32:
      PackedField<ProtoWireType::kFixed32, uint32_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_SFIXED32:
      PackedField<ProtoWireType::kFixed32, int32_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_INT32:
      PackedField<ProtoWireType::kVarInt, int32_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_SINT32:
      PackedField<ProtoWireType::kVarInt, int32_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_UINT32:
      PackedField<ProtoWireType::kVarInt, uint32_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_FIXED64:
      PackedField<ProtoWireType::kFixed64, uint64_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_SFIXED64:
      PackedField<ProtoWireType::kFixed64, int64_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_INT64:
      PackedField<ProtoWireType::kVarInt, int64_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_SINT64:
      PackedField<ProtoWireType::kVarInt, int64_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_UINT64:
      PackedField<ProtoWireType::kVarInt, uint64_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_ENUM:
      PackedField<ProtoWireType::kVarInt, int32_t>(pool, *fd, field, out);
      return;
    case FieldDescriptorProto::TYPE_BOOL:
      PackedBoolField<ProtoWireType::kVarInt>(pool, *fd, field, out);
      return;
    case 0:
    default: {
      // In the absence of specific information display bytes.
      auto s = field.as_string();
      out->serializer().StringValue(std::string_view(s.data, s.size));
      return;
    }
  }
}

void EnumField(const DescriptorPool& pool,
               const FieldDescriptor& fd,
               int32_t value,
               JsonBuilder* out) {
  auto opt_enum_descriptor_idx =
      pool.FindDescriptorIdx(fd.resolved_type_name());
  if (!opt_enum_descriptor_idx) {
    out->serializer().NumberValue(value);
    return;
  }
  auto opt_enum_string =
      pool.descriptors()[*opt_enum_descriptor_idx].FindEnumString(value);
  // If the enum value is unknown, treat it like a completely unknown field.
  if (!opt_enum_string) {
    out->serializer().NumberValue(value);
    return;
  }

  out->serializer().StringValue(*opt_enum_string);
}

void VarIntField(const DescriptorPool& pool,
                 const FieldDescriptor* fd,
                 const protozero::Field& field,
                 JsonBuilder* out) {
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_INT32:
      out->serializer().NumberValue(field.as_int32());
      return;
    case FieldDescriptorProto::TYPE_SINT32:
      out->serializer().NumberValue(field.as_sint32());
      return;
    case FieldDescriptorProto::TYPE_UINT32:
      out->serializer().NumberValue(field.as_uint32());
      return;
    case FieldDescriptorProto::TYPE_INT64:
      out->serializer().NumberValue(field.as_int64());
      return;
    case FieldDescriptorProto::TYPE_SINT64:
      out->serializer().NumberValue(field.as_sint64());
      return;
    case FieldDescriptorProto::TYPE_UINT64:
      out->serializer().NumberValue(field.as_uint64());
      return;
    case FieldDescriptorProto::TYPE_BOOL:
      out->serializer().BoolValue(field.as_bool());
      return;
    case FieldDescriptorProto::TYPE_ENUM:
      EnumField(pool, *fd, field.as_int32(), out);
      return;
    case 0:
    default:
      out->serializer().NumberValue(field.as_int64());
      return;
  }
}

void Fixed32Field(const FieldDescriptor* fd,
                  const protozero::Field& field,
                  JsonBuilder* out) {
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_SFIXED32:
      out->serializer().NumberValue(field.as_int32());
      break;
    case FieldDescriptorProto::TYPE_FIXED32:
      out->serializer().NumberValue(field.as_uint32());
      break;
    case FieldDescriptorProto::TYPE_FLOAT:
      out->serializer().FloatValue(field.as_float());
      break;
    case 0:
    default:
      out->serializer().NumberValue(field.as_uint32());
      break;
  }
}

void Fixed64Field(const FieldDescriptor* fd,
                  const protozero::Field& field,
                  JsonBuilder* out) {
  uint64_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_SFIXED64:
      out->serializer().NumberValue(field.as_int64());
      break;
    case FieldDescriptorProto::TYPE_FIXED64:
      out->serializer().NumberValue(field.as_uint64());
      break;
    case FieldDescriptorProto::TYPE_DOUBLE:
      out->serializer().DoubleValue(field.as_double());
      break;
    case 0:
    default:
      out->serializer().NumberValue(field.as_uint64());
      break;
  }
}

void RepeatedVarInt(const DescriptorPool& pool,
                    protozero::ConstBytes protobytes,
                    const FieldDescriptor* fd,
                    uint32_t id,
                    JsonBuilder* out) {
  out->serializer().OpenArray();
  protozero::ProtoDecoder decoder(protobytes.data, protobytes.size);
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == id) {
      VarIntField(pool, fd, field, out);
    }
  }
  out->serializer().CloseArray();
}

void RepeatedLengthField(const DescriptorPool& pool,
                         protozero::ConstBytes protobytes,
                         const FieldDescriptor* fd,
                         uint32_t id,
                         bool fully_qualify_extensions,
                         JsonBuilder* out) {
  out->serializer().OpenArray();
  protozero::ProtoDecoder decoder(protobytes.data, protobytes.size);
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == id) {
      LengthField(pool, fd, field, fully_qualify_extensions, out);
    }
  }
  out->serializer().CloseArray();
}

void RepeatedFixed64(protozero::ConstBytes protobytes,
                     const FieldDescriptor* fd,
                     uint32_t id,
                     JsonBuilder* out) {
  out->serializer().OpenArray();
  protozero::ProtoDecoder decoder(protobytes.data, protobytes.size);
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == id) {
      Fixed64Field(fd, field, out);
    }
  }
  out->serializer().CloseArray();
}

void RepeatedFixed32(protozero::ConstBytes protobytes,
                     const FieldDescriptor* fd,
                     uint32_t id,
                     JsonBuilder* out) {
  out->serializer().OpenArray();
  protozero::ProtoDecoder decoder(protobytes.data, protobytes.size);
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == id) {
      Fixed32Field(fd, field, out);
    }
  }
  out->serializer().CloseArray();
}

void InnerMessageField(const DescriptorPool& pool,
                       const std::string& type,
                       protozero::ConstBytes protobytes,
                       bool fully_qualify_extensions,
                       JsonBuilder* out) {
  std::optional<uint32_t> opt_proto_desc_idx = pool.FindDescriptorIdx(type);
  const ProtoDescriptor* opt_proto_descriptor =
      opt_proto_desc_idx ? &pool.descriptors()[*opt_proto_desc_idx] : nullptr;

  protozero::ProtoDecoder decoder(protobytes.data, protobytes.size);
  std::unordered_set<uint32_t> fields_seen;

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    const auto* opt_field_descriptor =
        opt_proto_descriptor ? opt_proto_descriptor->FindFieldByTag(field.id())
                             : nullptr;
    bool is_repeated = false;
    if (opt_field_descriptor &&
        IsTypeMatch(field.type(), opt_field_descriptor->type())) {
      is_repeated = opt_field_descriptor->is_repeated();
      // The first time we see a repeated field we consume them all:
      if (fields_seen.count(field.id())) {
        continue;
      }
      if (opt_field_descriptor->is_extension() && fully_qualify_extensions) {
        out->serializer().Key(FulllyQualifiedFieldName(*opt_proto_descriptor,
                                                       *opt_field_descriptor));
      } else {
        out->serializer().Key(opt_field_descriptor->name());
      }
    } else {
      out->serializer().Key(std::to_string(field.id()));
    }
    if (is_repeated) {
      fields_seen.insert(field.id());

      switch (field.type()) {
        case ProtoWireType::kVarInt:
          RepeatedVarInt(pool, protobytes, opt_field_descriptor, field.id(),
                         out);
          break;
        case ProtoWireType::kLengthDelimited:
          if (opt_field_descriptor &&
              IsNumericFieldType(opt_field_descriptor->type())) {
            // wire_type = length + field_type in
            // {u,s,}int{32,64}, float, double etc means this is the
            // packed case:
            LengthField(pool, opt_field_descriptor, field,
                        fully_qualify_extensions, out);
          } else {
            RepeatedLengthField(pool, protobytes, opt_field_descriptor,
                                field.id(), fully_qualify_extensions, out);
          }
          break;
        case ProtoWireType::kFixed32:
          RepeatedFixed32(protobytes, opt_field_descriptor, field.id(), out);
          break;
        case ProtoWireType::kFixed64:
          RepeatedFixed64(protobytes, opt_field_descriptor, field.id(), out);
          break;
      }
    } else {
      switch (field.type()) {
        case ProtoWireType::kVarInt:
          VarIntField(pool, opt_field_descriptor, field, out);
          break;
        case ProtoWireType::kLengthDelimited:
          LengthField(pool, opt_field_descriptor, field,
                      fully_qualify_extensions, out);
          break;
        case ProtoWireType::kFixed32:
          Fixed32Field(opt_field_descriptor, field, out);
          break;
        case ProtoWireType::kFixed64:
          Fixed64Field(opt_field_descriptor, field, out);
          break;
      }
    }
  }

  if (decoder.bytes_left() != 0) {
    out->AddError(std::to_string(decoder.bytes_left()) + " extra bytes");
  }
}

void MessageField(const DescriptorPool& pool,
                  const std::string& type,
                  protozero::ConstBytes protobytes,
                  bool fully_qualify_extensions,
                  JsonBuilder* out) {
  out->serializer().OpenObject();
  InnerMessageField(pool, type, protobytes, fully_qualify_extensions, out);
  out->serializer().CloseObject();
}

// Prints all field options for non-empty fields of a message. Example:
// --- Message definitions ---
// FooMessage {
//   repeated int64 foo = 1 [op1 = val1, op2 = val2];
//   optional BarMessage bar = 2 [op3 = val3];
// }
//
// BarMessage {
//   optional int64 baz = 1 [op4 = val4];
// }
// --- MessageInstance ---
// foo_msg = {  // (As JSON)
//   foo: [23, 24, 25],
//   bar: {
//     baz: 42
//   }
// }
// --- Output of MessageFieldOptionsToJson(foo_msg) ---
//   foo: {
//     __field_options: {
//       op1: val1,
//       op2: val2,
//     },
//     __repeated: true
//   }
//   bar: {
//     __field_options: {
//       op3 = val3,
//     },
//     baz: {
//       __field_options: {
//         op4 = val4
//       },
//     }
//   }
void MessageFieldOptionsToJson(
    const DescriptorPool& pool,
    const std::string& type,
    const std::string& field_prefix,
    const std::unordered_set<std::string>& allowed_fields,
    JsonBuilder* out) {
  std::optional<uint32_t> opt_proto_desc_idx = pool.FindDescriptorIdx(type);
  if (!opt_proto_desc_idx) {
    return;
  }
  const ProtoDescriptor& desc = pool.descriptors()[*opt_proto_desc_idx];
  for (const auto& id_and_field : desc.fields()) {
    const FieldDescriptor& field_desc = id_and_field.second;
    std::string full_field_name = field_prefix + field_desc.name();
    if (allowed_fields.find(full_field_name) == allowed_fields.end()) {
      continue;
    }
    if (field_desc.is_extension()) {
      out->serializer().Key(FulllyQualifiedFieldName(desc, field_desc));
    } else {
      out->serializer().Key(field_desc.name());
    }
    out->serializer().OpenObject();
    if (HasFieldOptions(field_desc)) {
      out->serializer().Key("__field_options");
      MessageField(pool, ".google.protobuf.FieldOptions",
                   protozero::ConstBytes{field_desc.options().data(),
                                         field_desc.options().size()},
                   false, out);
    }
    if (field_desc.type() == FieldDescriptorProto::Type::TYPE_MESSAGE) {
      MessageFieldOptionsToJson(pool, field_desc.resolved_type_name(),
                                full_field_name + ".", allowed_fields, out);
    }
    if (field_desc.is_repeated()) {
      out->serializer().Key("__repeated");
      out->serializer().BoolValue(true);
    }
    out->serializer().CloseObject();
  }
}

bool PopulateAllowedFieldOptionsSet(
    const DescriptorPool& pool,
    const std::string& type,
    const std::string& field_prefix,
    protozero::ConstBytes protobytes,
    std::unordered_set<std::string>& allowed_fields) {
  std::optional<uint32_t> opt_proto_desc_idx = pool.FindDescriptorIdx(type);
  if (!opt_proto_desc_idx) {
    return false;
  }
  const ProtoDescriptor& desc = pool.descriptors()[*opt_proto_desc_idx];
  protozero::ProtoDecoder decoder(protobytes);
  bool allowed = false;
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    const auto* opt_field_descriptor = desc.FindFieldByTag(field.id());
    if (!opt_field_descriptor) {
      continue;
    }
    std::string full_field_name = field_prefix + opt_field_descriptor->name();
    bool nested = false;
    if (opt_field_descriptor->type() ==
        protos::pbzero::FieldDescriptorProto::TYPE_MESSAGE) {
      nested = PopulateAllowedFieldOptionsSet(
          pool, opt_field_descriptor->resolved_type_name(),
          full_field_name + ".", field.as_bytes(), allowed_fields);
    }
    if (nested || HasFieldOptions(*opt_field_descriptor)) {
      allowed_fields.emplace(full_field_name);
      allowed = true;
    }
  }
  return allowed;
}

}  // namespace

std::string ProtozeroToJson(const DescriptorPool& pool,
                            const std::string& type,
                            protozero::ConstBytes protobytes,
                            int flags) {
  JsonBuilder builder(flags);
  builder.serializer().OpenObject();
  InnerMessageField(pool, type, protobytes, true, &builder);
  if (builder.is_inline_errors() && !builder.errors().empty()) {
    builder.serializer().Key("__error");
    builder.serializer().StringValue(base::Join(builder.errors(), "\n"));
  }
  if (flags & kInlineAnnotations) {
    std::unordered_set<std::string> allowed_fields;
    PopulateAllowedFieldOptionsSet(pool, type, "", protobytes, allowed_fields);
    if (!allowed_fields.empty()) {
      builder.serializer().Key("__annotations");
      builder.serializer().OpenObject();
      MessageFieldOptionsToJson(pool, type, "", allowed_fields, &builder);
      builder.serializer().CloseObject();
    }
  }
  builder.serializer().CloseObject();
  return builder.ToString();
}

std::string ProtozeroToJson(const DescriptorPool& pool,
                            const std::string& type,
                            const std::vector<uint8_t>& protobytes,
                            int flags) {
  return ProtozeroToJson(
      pool, type, protozero::ConstBytes{protobytes.data(), protobytes.size()},
      flags);
}

}  // namespace perfetto::trace_processor::protozero_to_json
