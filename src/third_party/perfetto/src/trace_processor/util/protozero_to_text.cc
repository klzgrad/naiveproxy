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

#include "src/trace_processor/util/protozero_to_text.h"

#include <cinttypes>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "protos/perfetto/common/descriptor.pbzero.h"
#include "src/trace_processor/util/descriptors.h"

namespace perfetto::trace_processor::protozero_to_text {

namespace {

using protozero::proto_utils::ProtoWireType;
using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;

// This function matches the implementation of TextFormatEscaper.escapeBytes
// from the Java protobuf library.
std::string QuoteAndEscapeTextProtoString(base::StringView raw) {
  std::string ret;
  for (char c : raw) {
    switch (c) {
      case '\a':
        ret += "\\a";
        break;
      case '\b':
        ret += "\\b";
        break;
      case '\f':
        ret += "\\f";
        break;
      case '\n':
        ret += "\\n";
        break;
      case '\r':
        ret += "\\r";
        break;
      case '\t':
        ret += "\\t";
        break;
      case '\v':
        ret += "\\v";
        break;
      case '\\':
        ret += "\\\\";
        break;
      case '\'':
        ret += "\\\'";
        break;
      case '"':
        ret += "\\\"";
        break;
      default:
        // Only ASCII characters between 0x20 (space) and 0x7e (tilde) are
        // printable; other byte values are escaped with 3-character octal
        // codes.
        if (c >= 0x20 && c <= 0x7e) {
          ret += c;
        } else {
          ret += '\\';

          // Cast to unsigned char to make the right shift unsigned as well.
          auto uc = static_cast<unsigned char>(c);
          ret += ('0' + ((uc >> 6) & 3));
          ret += ('0' + ((uc >> 3) & 7));
          ret += ('0' + (uc & 7));
        }
        break;
    }
  }
  return '"' + ret + '"';
}

// Append |to_add| which is something string like to |out|.
template <typename T>
void StrAppend(std::string* out, const T& to_add) {
  out->append(to_add);
}

template <typename T, typename... strings>
void StrAppend(std::string* out, const T& first, strings... values) {
  StrAppend(out, first);
  StrAppend(out, values...);
}

void IncreaseIndents(std::string* out) {
  StrAppend(out, "  ");
}

void DecreaseIndents(std::string* out) {
  PERFETTO_DCHECK(out->size() >= 2);
  out->erase(out->size() - 2);
}

void PrintUnknownVarIntField(uint32_t id, int64_t value, std::string* out) {
  StrAppend(out, std::to_string(id), ": ", std::to_string(value));
}

void PrintEnumField(const FieldDescriptor& fd,
                    const DescriptorPool& pool,
                    uint32_t id,
                    int32_t enum_value,
                    std::string* out) {
  auto opt_enum_descriptor_idx =
      pool.FindDescriptorIdx(fd.resolved_type_name());
  if (!opt_enum_descriptor_idx) {
    PrintUnknownVarIntField(id, enum_value, out);
    return;
  }
  auto opt_enum_string =
      pool.descriptors()[*opt_enum_descriptor_idx].FindEnumString(enum_value);
  // If the enum value is unknown, treat it like a completely unknown field.
  if (!opt_enum_string) {
    PrintUnknownVarIntField(id, enum_value, out);
    return;
  }
  StrAppend(out, fd.name(), ": ", *opt_enum_string);
}

std::string FormattedFieldDescriptorName(
    const FieldDescriptor& field_descriptor) {
  if (field_descriptor.is_extension()) {
    // Libprotobuf formatter always formats extension field names as fully
    // qualified names.
    // TODO(b/197625974): Assuming for now all our extensions will belong to the
    // perfetto.protos package. Update this if we ever want to support extendees
    // in different package.
    return "[perfetto.protos." + field_descriptor.name() + "]";
  }
  return field_descriptor.name();
}

void PrintVarIntField(const FieldDescriptor* fd,
                      const protozero::Field& field,
                      const DescriptorPool& pool,
                      std::string* out) {
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_INT32:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_int32()));
      return;
    case FieldDescriptorProto::TYPE_SINT32:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_sint32()));
      return;
    case FieldDescriptorProto::TYPE_UINT32:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_uint32()));
      return;
    case FieldDescriptorProto::TYPE_INT64:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_int64()));
      return;
    case FieldDescriptorProto::TYPE_SINT64:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_sint64()));
      return;
    case FieldDescriptorProto::TYPE_UINT64:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_uint64()));
      return;
    case FieldDescriptorProto::TYPE_BOOL:
      StrAppend(out, fd->name(), ": ", field.as_bool() ? "true" : "false");
      return;
    case FieldDescriptorProto::TYPE_ENUM:
      PrintEnumField(*fd, pool, field.id(), field.as_int32(), out);
      return;
    case 0:
    default:
      PrintUnknownVarIntField(field.id(), field.as_int64(), out);
      return;
  }
}

void PrintFixed32Field(const FieldDescriptor* fd,
                       const protozero::Field& field,
                       std::string* out) {
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_SFIXED32:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_int32()));
      break;
    case FieldDescriptorProto::TYPE_FIXED32:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_uint32()));
      break;
    case FieldDescriptorProto::TYPE_FLOAT:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_float()));
      break;
    case 0:
    default:
      base::StackString<12> padded_hex("0x%08" PRIx32, field.as_uint32());
      StrAppend(out, std::to_string(field.id()), ": ", padded_hex.c_str());
      break;
  }
}

void PrintFixed64Field(const FieldDescriptor* fd,
                       const protozero::Field& field,
                       std::string* out) {
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_SFIXED64:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_int64()));
      break;
    case FieldDescriptorProto::TYPE_FIXED64:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_uint64()));
      break;
    case FieldDescriptorProto::TYPE_DOUBLE:
      StrAppend(out, fd->name(), ": ", std::to_string(field.as_double()));
      break;
    case 0:
    default:
      base::StackString<20> padded_hex("0x%016" PRIx64, field.as_uint64());
      StrAppend(out, std::to_string(field.id()), ": ", padded_hex.c_str());
      break;
  }
}

void ProtozeroToTextInternal(const std::string& type,
                             protozero::ConstBytes protobytes,
                             NewLinesMode new_lines_mode,
                             const DescriptorPool& pool,
                             std::string* indents,
                             std::string* output);

template <protozero::proto_utils::ProtoWireType wire_type, typename T>
void PrintPackedField(const FieldDescriptor& fd,
                      const protozero::Field& field,
                      NewLinesMode new_lines_mode,
                      const std::string& indents,
                      const DescriptorPool& pool,
                      std::string* out) {
  const bool include_new_lines = new_lines_mode == kIncludeNewLines;
  bool err = false;
  bool first_output = true;
  for (protozero::PackedRepeatedFieldIterator<wire_type, T> it(
           field.data(), field.size(), &err);
       it; it++) {
    T value = *it;
    if (!first_output) {
      if (include_new_lines) {
        StrAppend(out, "\n", indents);
      } else {
        StrAppend(out, " ");
      }
    }
    std::string serialized_value;
    if (fd.type() == FieldDescriptorProto::TYPE_ENUM) {
      PrintEnumField(fd, pool, field.id(), static_cast<int32_t>(value), out);
    } else {
      StrAppend(out, fd.name(), ": ", std::to_string(value));
    }
    first_output = false;
  }

  if (err) {
    if (!first_output) {
      if (include_new_lines) {
        StrAppend(out, "\n", indents);
      } else {
        StrAppend(out, " ");
      }
    }
    StrAppend(out, "# Packed decoding failure for field ", fd.name(), "\n");
  }
}

void PrintLengthDelimitedField(const FieldDescriptor* fd,
                               const protozero::Field& field,
                               NewLinesMode new_lines_mode,
                               std::string* indents,
                               const DescriptorPool& pool,
                               std::string* out) {
  const bool include_new_lines = new_lines_mode == kIncludeNewLines;
  uint32_t type = fd ? fd->type() : 0;
  switch (type) {
    case FieldDescriptorProto::TYPE_BYTES:
    case FieldDescriptorProto::TYPE_STRING: {
      std::string value = QuoteAndEscapeTextProtoString(field.as_string());
      StrAppend(out, fd->name(), ": ", value);
      return;
    }
    case FieldDescriptorProto::TYPE_MESSAGE:
      StrAppend(out, FormattedFieldDescriptorName(*fd), " {");
      if (include_new_lines) {
        IncreaseIndents(indents);
      }
      ProtozeroToTextInternal(fd->resolved_type_name(), field.as_bytes(),
                              new_lines_mode, pool, indents, out);
      if (include_new_lines) {
        DecreaseIndents(indents);
        StrAppend(out, "\n", *indents, "}");
      } else {
        StrAppend(out, " }");
      }
      return;
    case FieldDescriptorProto::TYPE_DOUBLE:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kFixed64, double>(
          *fd, field, new_lines_mode, *indents, pool, out);
      return;
    case FieldDescriptorProto::TYPE_FLOAT:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kFixed32, float>(
          *fd, field, new_lines_mode, *indents, pool, out);
      return;
    case FieldDescriptorProto::TYPE_INT64:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kVarInt, int64_t>(
          *fd, field, new_lines_mode, *indents, pool, out);
      return;
    case FieldDescriptorProto::TYPE_UINT64:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kVarInt,
                       uint64_t>(*fd, field, new_lines_mode, *indents, pool,
                                 out);
      return;
    case FieldDescriptorProto::TYPE_INT32:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kVarInt, int32_t>(
          *fd, field, new_lines_mode, *indents, pool, out);
      return;
    case FieldDescriptorProto::TYPE_FIXED64:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kFixed64,
                       uint64_t>(*fd, field, new_lines_mode, *indents, pool,
                                 out);
      return;
    case FieldDescriptorProto::TYPE_FIXED32:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kFixed32,
                       uint32_t>(*fd, field, new_lines_mode, *indents, pool,
                                 out);
      return;
    case FieldDescriptorProto::TYPE_UINT32:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kVarInt,
                       uint32_t>(*fd, field, new_lines_mode, *indents, pool,
                                 out);
      return;
    case FieldDescriptorProto::TYPE_SFIXED32:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kFixed32,
                       int32_t>(*fd, field, new_lines_mode, *indents, pool,
                                out);
      return;
    case FieldDescriptorProto::TYPE_SFIXED64:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kFixed64,
                       int64_t>(*fd, field, new_lines_mode, *indents, pool,
                                out);
      return;
    case FieldDescriptorProto::TYPE_ENUM:
      PrintPackedField<protozero::proto_utils::ProtoWireType::kVarInt, int32_t>(
          *fd, field, new_lines_mode, *indents, pool, out);
      return;
    // Our protoc plugin cannot generate code for packed repeated fields with
    // these types. Output a comment and then fall back to the raw field_id:
    // string representation.
    case FieldDescriptorProto::TYPE_BOOL:
    case FieldDescriptorProto::TYPE_SINT32:
    case FieldDescriptorProto::TYPE_SINT64:
      StrAppend(out, "# Packed type ", std::to_string(type),
                " not supported. Printing raw string.", "\n", *indents);
      break;
    case 0:
    default:
      break;
  }
  std::string value = QuoteAndEscapeTextProtoString(field.as_string());
  StrAppend(out, std::to_string(field.id()), ": ", value);
}

// Recursive case function, Will parse |protobytes| assuming it is a proto of
// |type| and will use |pool| to look up the |type|. All output will be placed
// in |output|, using |new_lines_mode| to separate fields. When called for
// |indents| will be increased by 2 spaces to improve readability.
void ProtozeroToTextInternal(const std::string& type,
                             protozero::ConstBytes protobytes,
                             NewLinesMode new_lines_mode,
                             const DescriptorPool& pool,
                             std::string* indents,
                             std::string* output) {
  std::optional<uint32_t> opt_proto_desc_idx = pool.FindDescriptorIdx(type);
  const ProtoDescriptor* opt_proto_descriptor =
      opt_proto_desc_idx ? &pool.descriptors()[*opt_proto_desc_idx] : nullptr;
  const bool include_new_lines = new_lines_mode == kIncludeNewLines;

  protozero::ProtoDecoder decoder(protobytes.data, protobytes.size);
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (!output->empty()) {
      if (include_new_lines) {
        StrAppend(output, "\n", *indents);
      } else {
        StrAppend(output, " ", *indents);
      }
    } else {
      StrAppend(output, *indents);
    }
    const auto* opt_field_descriptor =
        opt_proto_descriptor ? opt_proto_descriptor->FindFieldByTag(field.id())
                             : nullptr;
    switch (field.type()) {
      case ProtoWireType::kVarInt:
        PrintVarIntField(opt_field_descriptor, field, pool, output);
        break;
      case ProtoWireType::kLengthDelimited:
        PrintLengthDelimitedField(opt_field_descriptor, field, new_lines_mode,
                                  indents, pool, output);
        break;
      case ProtoWireType::kFixed32:
        PrintFixed32Field(opt_field_descriptor, field, output);
        break;
      case ProtoWireType::kFixed64:
        PrintFixed64Field(opt_field_descriptor, field, output);
        break;
    }
  }
  if (decoder.bytes_left() != 0) {
    if (!output->empty()) {
      if (include_new_lines) {
        StrAppend(output, "\n", *indents);
      } else {
        StrAppend(output, " ", *indents);
      }
    }
    StrAppend(
        output, "# Extra bytes: ",
        QuoteAndEscapeTextProtoString(base::StringView(
            reinterpret_cast<const char*>(decoder.end() - decoder.bytes_left()),
            decoder.bytes_left())),
        "\n");
  }
}

}  // namespace

std::string ProtozeroToText(const DescriptorPool& pool,
                            const std::string& type,
                            protozero::ConstBytes protobytes,
                            NewLinesMode new_lines_mode,
                            uint32_t initial_indent_depth) {
  std::string indent = std::string(2lu * initial_indent_depth, ' ');
  std::string final_result;
  ProtozeroToTextInternal(type, protobytes, new_lines_mode, pool, &indent,
                          &final_result);
  return final_result;
}

std::string ProtozeroToText(const DescriptorPool& pool,
                            const std::string& type,
                            const std::vector<uint8_t>& protobytes,
                            NewLinesMode new_lines_mode) {
  return ProtozeroToText(
      pool, type, protozero::ConstBytes{protobytes.data(), protobytes.size()},
      new_lines_mode);
}

}  // namespace perfetto::trace_processor::protozero_to_text
