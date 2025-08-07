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

#include "src/tools/proto_merger/proto_file.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/text_format.h>

#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace proto_merger {
namespace {

const char* const
    kTypeToName[google::protobuf::FieldDescriptor::Type::MAX_TYPE + 1] = {
        "ERROR",  // 0 is reserved for errors

        "double",    // TYPE_DOUBLE
        "float",     // TYPE_FLOAT
        "int64",     // TYPE_INT64
        "uint64",    // TYPE_UINT64
        "int32",     // TYPE_INT32
        "fixed64",   // TYPE_FIXED64
        "fixed32",   // TYPE_FIXED32
        "bool",      // TYPE_BOOL
        "string",    // TYPE_STRING
        "group",     // TYPE_GROUP
        "message",   // TYPE_MESSAGE
        "bytes",     // TYPE_BYTES
        "uint32",    // TYPE_UINT32
        "enum",      // TYPE_ENUM
        "sfixed32",  // TYPE_SFIXED32
        "sfixed64",  // TYPE_SFIXED64
        "sint32",    // TYPE_SINT32
        "sint64",    // TYPE_SINT64
};

std::optional<std::string> MinimizeType(const std::string& a,
                                        const std::string& b) {
  auto a_pieces = base::SplitString(a, ".");
  auto b_pieces = base::SplitString(b, ".");

  size_t skip = 0;
  for (size_t i = 0; i < std::min(a_pieces.size(), b_pieces.size()); ++i) {
    if (a_pieces[i] != b_pieces[i])
      return a.substr(skip);
    skip += a_pieces[i].size() + 1;
  }
  return std::nullopt;
}

std::string SimpleFieldTypeFromDescriptor(
    const google::protobuf::Descriptor& parent,
    const google::protobuf::FieldDescriptor& desc,
    bool packageless_type) {
  switch (desc.type()) {
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (packageless_type) {
        return base::StripPrefix(
            std::string(desc.message_type()->full_name()),
            std::string(desc.message_type()->file()->package()) + ".");
      } else {
        return MinimizeType(std::string(desc.message_type()->full_name()),
                            std::string(parent.full_name()))
            .value_or(std::string(desc.message_type()->name()));
      }
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      if (packageless_type) {
        return base::StripPrefix(
            std::string(desc.enum_type()->full_name()),
            std::string(desc.enum_type()->file()->package()) + ".");
      } else {
        return MinimizeType(std::string(desc.enum_type()->full_name()),
                            std::string(parent.full_name()))
            .value_or(std::string(desc.enum_type()->name()));
      }
    default:
      return kTypeToName[desc.type()];
  }
}

std::string FieldTypeFromDescriptor(
    const google::protobuf::Descriptor& parent,
    const google::protobuf::FieldDescriptor& desc,
    bool packageless_type) {
  if (!desc.is_map())
    return SimpleFieldTypeFromDescriptor(parent, desc, packageless_type);

  std::string field_type;
  field_type += "map<";
  field_type += FieldTypeFromDescriptor(parent, *desc.message_type()->field(0),
                                        packageless_type);
  field_type += ",";
  field_type += FieldTypeFromDescriptor(parent, *desc.message_type()->field(1),
                                        packageless_type);
  field_type += ">";
  return field_type;
}

std::unique_ptr<google::protobuf::Message> NormalizeOptionsMessage(
    const google::protobuf::DescriptorPool& pool,
    google::protobuf::DynamicMessageFactory* factory,
    const google::protobuf::Message& message) {
  const auto* option_descriptor =
      pool.FindMessageTypeByName(message.GetDescriptor()->full_name());
  if (!option_descriptor)
    return nullptr;

  std::unique_ptr<google::protobuf::Message> dynamic_options(
      factory->GetPrototype(option_descriptor)->New());
  PERFETTO_CHECK(dynamic_options->ParseFromString(message.SerializeAsString()));
  return dynamic_options;
}

std::vector<ProtoFile::Option> OptionsFromMessage(
    const google::protobuf::DescriptorPool& pool,
    const google::protobuf::Message& raw_message) {
  google::protobuf::DynamicMessageFactory factory;

  auto normalized = NormalizeOptionsMessage(pool, &factory, raw_message);
  const auto* message = normalized ? normalized.get() : &raw_message;
  const auto* reflection = message->GetReflection();

  std::vector<const google::protobuf::FieldDescriptor*> fields;
  reflection->ListFields(*message, &fields);

  std::vector<ProtoFile::Option> options;
  for (size_t i = 0; i < fields.size(); i++) {
    int count = 1;
    bool repeated = false;
    if (fields[i]->is_repeated()) {
      count = reflection->FieldSize(*message, fields[i]);
      repeated = true;
    }
    for (int j = 0; j < count; j++) {
      std::string name;
      if (fields[i]->is_extension()) {
        name = "(" + std::string(fields[i]->full_name()) + ")";
      } else {
        name = fields[i]->name();
      }

      std::string fieldval;
      if (fields[i]->cpp_type() ==
          google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        std::string tmp;
        google::protobuf::TextFormat::Printer printer;
        printer.PrintFieldValueToString(*message, fields[i], repeated ? j : -1,
                                        &tmp);
        fieldval.append("{\n");
        fieldval.append(tmp);
        fieldval.append("}");
      } else {
        google::protobuf::TextFormat::PrintFieldValueToString(
            *message, fields[i], repeated ? j : -1, &fieldval);
      }
      options.push_back(ProtoFile::Option{name, fieldval});
    }
  }
  return options;
}

template <typename Output, typename Descriptor>
Output InitFromDescriptor(const Descriptor& desc) {
  google::protobuf::SourceLocation source_loc;
  if (!desc.GetSourceLocation(&source_loc))
    return {};

  Output out;
  out.leading_comments = base::SplitString(source_loc.leading_comments, "\n");
  out.trailing_comments = base::SplitString(source_loc.trailing_comments, "\n");
  return out;
}

ProtoFile::Field FieldFromDescriptor(
    const google::protobuf::Descriptor& parent,
    const google::protobuf::FieldDescriptor& desc) {
  auto field = InitFromDescriptor<ProtoFile::Field>(desc);
  field.is_repeated = desc.is_repeated();
  field.packageless_type = FieldTypeFromDescriptor(parent, desc, true);
  field.type = FieldTypeFromDescriptor(parent, desc, false);
  field.name = desc.name();
  field.number = desc.number();
  field.options = OptionsFromMessage(*desc.file()->pool(), desc.options());

  // Protobuf editions: packed fields are no longer an option, but have the same
  // syntax as far as writing the merged .proto file is concerned.
  if (desc.is_packed()) {
    field.options.push_back(
        ProtoFile::Option{"features.repeated_field_encoding", "PACKED"});
  }

  return field;
}

ProtoFile::Enum::Value EnumValueFromDescriptor(
    const google::protobuf::EnumValueDescriptor& desc) {
  auto value = InitFromDescriptor<ProtoFile::Enum::Value>(desc);
  value.name = desc.name();
  value.number = desc.number();
  value.options = OptionsFromMessage(*desc.file()->pool(), desc.options());
  return value;
}

ProtoFile::Enum EnumFromDescriptor(
    const google::protobuf::EnumDescriptor& desc) {
  auto en = InitFromDescriptor<ProtoFile::Enum>(desc);
  en.name = desc.name();
  for (int i = 0; i < desc.value_count(); ++i) {
    en.values.emplace_back(EnumValueFromDescriptor(*desc.value(i)));
  }
  return en;
}

ProtoFile::Oneof OneOfFromDescriptor(
    const google::protobuf::Descriptor& parent,
    const google::protobuf::OneofDescriptor& desc) {
  auto oneof = InitFromDescriptor<ProtoFile::Oneof>(desc);
  oneof.name = desc.name();
  for (int i = 0; i < desc.field_count(); ++i) {
    oneof.fields.emplace_back(FieldFromDescriptor(parent, *desc.field(i)));
  }
  return oneof;
}

ProtoFile::Message MessageFromDescriptor(
    const google::protobuf::Descriptor& desc) {
  auto message = InitFromDescriptor<ProtoFile::Message>(desc);
  message.name = desc.name();
  for (int i = 0; i < desc.enum_type_count(); ++i) {
    message.enums.emplace_back(EnumFromDescriptor(*desc.enum_type(i)));
  }
  for (int i = 0; i < desc.nested_type_count(); ++i) {
    message.nested_messages.emplace_back(
        MessageFromDescriptor(*desc.nested_type(i)));
  }
  for (int i = 0; i < desc.oneof_decl_count(); ++i) {
    message.oneofs.emplace_back(OneOfFromDescriptor(desc, *desc.oneof_decl(i)));
  }
  for (int i = 0; i < desc.field_count(); ++i) {
    auto* field = desc.field(i);
    if (field->containing_oneof())
      continue;
    message.fields.emplace_back(FieldFromDescriptor(desc, *field));
  }
  return message;
}

}  // namespace

ProtoFile ProtoFileFromDescriptor(
    std::string preamble,
    const google::protobuf::FileDescriptor& desc) {
  ProtoFile file;
  file.preamble = std::move(preamble);
  for (int i = 0; i < desc.enum_type_count(); ++i) {
    file.enums.push_back(EnumFromDescriptor(*desc.enum_type(i)));
  }
  for (int i = 0; i < desc.message_type_count(); ++i) {
    file.messages.push_back(MessageFromDescriptor(*desc.message_type(i)));
  }
  return file;
}

}  // namespace proto_merger
}  // namespace perfetto
