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

#include "src/tools/proto_merger/allowlist.h"

#include <google/protobuf/descriptor.pb.h>

#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace proto_merger {
namespace {

std::vector<std::string> SplitFieldPath(const std::string& name) {
  if (name.empty())
    return {};

  if (name[0] == '.')
    return base::SplitString(name.substr(1), ".");

  return base::SplitString(name, ".");
}

Allowlist::Message& ResolveMessageForDescriptor(
    const google::protobuf::Descriptor& desc,
    Allowlist& allowlist) {
  std::string name(desc.name());
  if (!desc.containing_type())
    return allowlist.messages[name];

  Allowlist::Message& parent =
      ResolveMessageForDescriptor(*desc.containing_type(), allowlist);
  return parent.nested_messages[name];
}

void AllowlistEnum(const google::protobuf::EnumDescriptor& desc,
                   Allowlist& allowlist) {
  std::string name(desc.name());
  if (!desc.containing_type()) {
    allowlist.enums.emplace(name);
    return;
  }

  auto& containing =
      ResolveMessageForDescriptor(*desc.containing_type(), allowlist);
  containing.enums.emplace(name);
}

void AllowlistField(const google::protobuf::FieldDescriptor& desc,
                    Allowlist& allowlist) {
  auto& containing =
      ResolveMessageForDescriptor(*desc.containing_type(), allowlist);

  // Check if this field is already allowed and return if so; otherwise add it.
  // We need to do slightly different things based on whether or not this field
  // is in a oneof.
  if (desc.containing_oneof()) {
    auto& oneof =
        containing.oneofs[std::string(desc.containing_oneof()->name())];
    if (!oneof.emplace(desc.number()).second) {
      return;
    }
  } else {
    if (!containing.fields.emplace(desc.number()).second)
      return;
  }

  switch (desc.type()) {
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      // For message types, we recursively allow all fields under it including
      // any types those fields may depend on.
      for (int i = 0; i < desc.message_type()->field_count(); ++i) {
        AllowlistField(*desc.message_type()->field(i), allowlist);
      }
      break;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      // For enums, we allow the enum type.
      AllowlistEnum(*desc.enum_type(), allowlist);
      break;
    default:
      // We don't need to do anything for primitive types.
      break;
  }
}

}  // namespace

base::Status AllowlistFromFieldList(
    const google::protobuf::Descriptor& desc,
    const std::vector<std::string>& allowed_fields,
    Allowlist& allowlist) {
  for (const auto& field_path : allowed_fields) {
    std::vector<std::string> pieces = SplitFieldPath(field_path);
    const auto* current = &desc;
    for (size_t i = 0; i < pieces.size(); ++i) {
      const auto* field = current->FindFieldByName(pieces[i]);
      if (!field) {
        return base::ErrStatus("Field %s in message %s not found.",
                               pieces[i].c_str(),
                               std::string(current->name()).c_str());
      }
      if (i == pieces.size() - 1) {
        // For the last field, allow the field and any messages it depends on
        // recursively.
        AllowlistField(*field, allowlist);
        break;
      }

      // All fields before the last should lead to a message type.
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        return base::ErrStatus("Field %s in message %s has a non-message type",
                               std::string(field->name()).c_str(),
                               std::string(desc.name()).c_str());
      }
      current = field->message_type();
    }
  }
  return base::OkStatus();
}

}  // namespace proto_merger
}  // namespace perfetto
