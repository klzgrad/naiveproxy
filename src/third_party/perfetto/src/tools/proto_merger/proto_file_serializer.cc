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

#include "src/tools/proto_merger/proto_file_serializer.h"

#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace proto_merger {
namespace {

std::string DeletedComment(const std::string& prefix) {
  std::string output;
  output += "\n";
  output += prefix + "  //\n";
  output += prefix;
  output +=
      "  // The following enums/messages/fields are not present upstream\n";
  output += prefix + "  //\n";
  return output;
}

std::string SerializeComments(const std::string& prefix,
                              const std::vector<std::string>& lines) {
  std::string output;
  for (const auto& line : lines) {
    output.append(prefix);
    output.append("//");
    output.append(line);
    output.append("\n");
  }
  return output;
}

std::string SerializeLeadingComments(const std::string& prefix,
                                     const ProtoFile::Member& member,
                                     bool prefix_newline_if_comment = true) {
  if (member.leading_comments.empty())
    return "";

  std::string output;
  if (prefix_newline_if_comment) {
    output += "\n";
  }
  output += SerializeComments(prefix, member.leading_comments);
  return output;
}

std::string SerializeTrailingComments(const std::string& prefix,
                                      const ProtoFile::Member& member) {
  return SerializeComments(prefix, member.trailing_comments);
}

std::string SerializeOptions(const std::vector<ProtoFile::Option>& options) {
  if (options.empty())
    return "";

  std::string output;
  output += " [";
  size_t n = options.size();
  for (size_t i = 0; i < n; i++) {
    output += options[i].key + " = " + options[i].value;
    if (i != n - 1)
      output += ", ";
  }
  output += "]";
  return output;
}

std::string SerializeEnumValue(size_t indent,
                               const ProtoFile::Enum::Value& value) {
  std::string prefix(indent * 2, ' ');

  std::string output;
  output += SerializeLeadingComments(prefix, value, false);

  output += prefix + value.name + " = " + std::to_string(value.number);
  output += SerializeOptions(value.options);
  output += ";\n";

  output += SerializeTrailingComments(prefix, value);
  return output;
}

std::string SerializeEnum(size_t indent, const ProtoFile::Enum& en) {
  std::string prefix(indent * 2, ' ');
  ++indent;

  std::string output;
  output += SerializeLeadingComments(prefix, en);

  output += prefix + "enum " + en.name + " {\n";
  for (const auto& value : en.values) {
    output += SerializeEnumValue(indent, value);
  }
  output += prefix + "}\n";

  output += SerializeTrailingComments(prefix, en);
  return output;
}

std::string SerializeField(size_t indent,
                           const ProtoFile::Field& field,
                           bool write_label) {
  std::string prefix(indent * 2, ' ');

  std::string output;
  output += SerializeLeadingComments(prefix, field);

  output += prefix;
  if (write_label && field.is_repeated)
    output += "repeated ";
  output +=
      field.type + " " + field.name + " = " + std::to_string(field.number);

  output += SerializeOptions(field.options);
  output += ";\n";

  output += SerializeTrailingComments(prefix, field);
  return output;
}

std::string SerializeOneof(size_t indent, const ProtoFile::Oneof& oneof) {
  std::string prefix(indent * 2, ' ');
  ++indent;

  std::string output;
  output += SerializeLeadingComments(prefix, oneof);

  output += prefix + "oneof " + oneof.name + " {\n";
  for (const auto& field : oneof.fields) {
    output += SerializeField(indent, field, false);
  }
  output += prefix + "}\n";

  output += SerializeTrailingComments(prefix, oneof);
  return output;
}

std::string SerializeMessage(size_t indent, const ProtoFile::Message& message) {
  std::string prefix(indent * 2, ' ');
  ++indent;

  std::string output;
  output += SerializeLeadingComments(prefix, message);

  output += prefix + "message " + message.name + " {\n";
  for (const auto& en : message.enums) {
    output += SerializeEnum(indent, en);
  }
  for (const auto& nested : message.nested_messages) {
    output += SerializeMessage(indent, nested);
  }
  for (const auto& oneof : message.oneofs) {
    output += SerializeOneof(indent, oneof);
  }
  for (const auto& field : message.fields) {
    output += SerializeField(indent, field, true);
  }

  if (!message.deleted_enums.empty() || !message.deleted_fields.empty() ||
      !message.deleted_nested_messages.empty() ||
      !message.deleted_oneofs.empty()) {
    output += DeletedComment(prefix);
    for (const auto& en : message.deleted_enums) {
      output += SerializeEnum(indent, en);
    }
    for (const auto& nested : message.deleted_nested_messages) {
      output += SerializeMessage(indent, nested);
    }
    for (const auto& oneof : message.deleted_oneofs) {
      output += SerializeOneof(indent, oneof);
    }
    for (const auto& field : message.deleted_fields) {
      output += SerializeField(indent, field, true);
    }
  }

  output += prefix + "}\n";

  output += SerializeTrailingComments(prefix, message);
  return output;
}

}  // namespace

std::string ProtoFileToDotProto(const ProtoFile& proto_file) {
  std::string output;
  output += proto_file.preamble;

  for (const auto& en : proto_file.enums) {
    output += SerializeEnum(0, en);
  }
  for (const auto& message : proto_file.messages) {
    output += SerializeMessage(0, message);
  }

  if (!proto_file.deleted_enums.empty() ||
      !proto_file.deleted_messages.empty()) {
    output += DeletedComment("");

    for (const auto& en : proto_file.deleted_enums) {
      output += SerializeEnum(0, en);
    }
    for (const auto& nested : proto_file.deleted_messages) {
      output += SerializeMessage(0, nested);
    }
  }
  return output;
}

}  // namespace proto_merger
}  // namespace perfetto
