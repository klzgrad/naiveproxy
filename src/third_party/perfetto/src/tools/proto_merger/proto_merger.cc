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

#include "src/tools/proto_merger/proto_merger.h"

#include <algorithm>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/tools/proto_merger/proto_file_serializer.h"

namespace perfetto {
namespace proto_merger {
namespace {

void StripDeletedElementComment(std::vector<std::string>& leading_comments) {
  std::vector<std::string> kept;
  kept.reserve(leading_comments.size());
  for (size_t i = 0; i < leading_comments.size(); i++) {
    if (base::TrimWhitespace(leading_comments[i]) != kDeletedCommentWarning) {
      kept.push_back(std::move(leading_comments[i]));
      continue;
    }
    // |i| is the warning line: don't keep it, and also drop one blank
    // comment line before it (already in |kept|) and one after it.
    if (!kept.empty() && base::TrimWhitespace(kept.back()).empty())
      kept.pop_back();
    if (i + 1 < leading_comments.size() &&
        base::TrimWhitespace(leading_comments[i + 1]).empty())
      i++;  // Skips the blank line: the loop increment moves past it.
  }
  leading_comments = std::move(kept);
}

bool IsAllowlistedOption(const std::string& key,
                         const std::set<std::string>& allowlisted_options) {
  return allowlisted_options.count(key);
}

void MergeAllowlistedOptions(const std::vector<ProtoFile::Option>& upstream,
                             std::vector<ProtoFile::Option>& out,
                             const std::set<std::string>& allowlisted_options) {
  if (allowlisted_options.empty() || upstream.empty())
    return;

  for (const auto& upstream_opt : upstream) {
    if (IsAllowlistedOption(upstream_opt.key, allowlisted_options)) {
      auto it = std::find_if(out.begin(), out.end(),
                             [&](const ProtoFile::Option& opt) {
                               return opt.key == upstream_opt.key;
                             });
      if (it != out.end()) {
        it->value = upstream_opt.value;
      } else {
        out.push_back(upstream_opt);
      }
    }
  }
}

void MarkFieldAsDeprecated(ProtoFile::Field& field) {
  auto it = std::find_if(
      field.options.begin(), field.options.end(),
      [](const ProtoFile::Option& opt) { return opt.key == "deprecated"; });
  if (it != field.options.end()) {
    it->value = "true";
  } else {
    field.options.push_back(ProtoFile::Option{"deprecated", "true"});
  }
}

template <typename Key, typename Value>
std::optional<Value> FindInMap(const std::map<Key, Value>& map,
                               const Key& key) {
  auto it = map.find(key);
  return it == map.end() ? std::nullopt : std::make_optional(it->second);
}

// Finds the given 'name' in the vector by comparing against
// the field named 'name' for each item in the vector.
// T is ProtoFile::Enum, ProtoFile::Oneof or ProtoFile::Message.
template <typename T>
const T* FindByName(const std::vector<T>& items, const std::string& name) {
  for (const auto& item : items) {
    if (item.name == name)
      return &item;
  }
  return nullptr;
}

template <typename T>
T CleanDeletedItem(const T& input_item) {
  T item = input_item;
  StripDeletedElementComment(item.leading_comments);
  return item;
}

// Compute the items present in the |input| vector but deleted in
// the |upstream| vector by looking at the field |name|.
// T is ProtoFile::Enum, ProtoFile::Oneof or ProtoFile::Message.
template <typename T>
std::vector<T> ComputeDeletedByName(const std::vector<T>& input,
                                    const std::vector<T>& upstream) {
  std::vector<T> deleted;
  std::set<std::string> seen;
  for (const auto& upstream_item : upstream) {
    auto* input_item = FindByName(input, upstream_item.name);
    if (!input_item)
      continue;
    seen.insert(input_item->name);
  }

  for (const auto& input_item : input) {
    if (seen.count(input_item.name))
      continue;
    deleted.emplace_back(CleanDeletedItem(input_item));
  }
  return deleted;
}

// Finds the given 'number' in the vector by comparing against
// the field named 'number for each item in the vector.
// T is ProtoFile::EnumValue or ProtoFile::Field.
template <typename T>
const T* FindByNumber(const std::vector<T>& items, int number) {
  for (const auto& item : items) {
    if (item.number == number)
      return &item;
  }
  return nullptr;
}

// Compute the items present in the |input| vector but deleted in
// the |upstream| vector by looking at the field |number|.
// T is ProtoFile::EnumValue or ProtoFile::Field.
template <typename T>
std::vector<T> ComputeDeletedByNumber(const std::vector<T>& input,
                                      const std::vector<T>& upstream) {
  std::vector<T> deleted;
  std::set<int> seen;
  for (const auto& upstream_item : upstream) {
    auto* input_item = FindByNumber(input, upstream_item.number);
    if (!input_item)
      continue;
    seen.insert(input_item->number);
  }

  for (const auto& input_item : input) {
    if (seen.count(input_item.number))
      continue;
    deleted.emplace_back(CleanDeletedItem(input_item));
  }
  return deleted;
}

ProtoFile::Enum::Value MergeEnumValue(
    const ProtoFile::Enum::Value& input,
    const ProtoFile::Enum::Value& upstream,
    const std::set<std::string>& allowlisted_options) {
  PERFETTO_CHECK(input.number == upstream.number);

  ProtoFile::Enum::Value out;
  out.name = upstream.name;

  // Get the comments from the source of truth.
  out.leading_comments = upstream.leading_comments;
  out.trailing_comments = upstream.trailing_comments;

  // Get everything else from the input.
  out.number = input.number;
  out.options = input.options;

  MergeAllowlistedOptions(upstream.options, out.options, allowlisted_options);

  return out;
}

ProtoFile::Enum MergeEnum(const ProtoFile::Enum& input,
                          const ProtoFile::Enum& upstream,
                          const std::set<std::string>& allowlisted_options) {
  PERFETTO_CHECK(input.name == upstream.name);

  ProtoFile::Enum out;
  out.name = upstream.name;

  // Get the comments from the source of truth.
  out.leading_comments = upstream.leading_comments;
  out.trailing_comments = upstream.trailing_comments;

  for (const auto& upstream_value : upstream.values) {
    // If an enum is allowlisted, we implicitly assume that all its
    // values are also allowed. Therefore, if the value doesn't exist
    // in the input, just take it from the source of truth.
    auto* input_value = FindByNumber(input.values, upstream_value.number);
    auto out_value = input_value ? MergeEnumValue(*input_value, upstream_value,
                                                  allowlisted_options)
                                 : upstream_value;
    out.values.emplace_back(std::move(out_value));
  }

  // Compute all the values present in the input but deleted in the
  // source of truth.
  out.deleted_values = ComputeDeletedByNumber(input.values, upstream.values);
  return out;
}

std::vector<ProtoFile::Enum> MergeEnums(
    const std::vector<ProtoFile::Enum>& input,
    const std::vector<ProtoFile::Enum>& upstream,
    const std::set<std::string>& allowlist,
    const std::set<std::string>& allowlisted_options) {
  std::vector<ProtoFile::Enum> out;
  for (const auto& upstream_enum : upstream) {
    auto* input_enum = FindByName(input, upstream_enum.name);
    if (!input_enum) {
      // If the enum is missing from the input but is present
      // in the allowlist, take the whole enum from the
      // source of truth.
      if (allowlist.count(upstream_enum.name))
        out.emplace_back(upstream_enum);
      continue;
    }

    // Otherwise, merge the enums from the input and source of truth.
    out.emplace_back(
        MergeEnum(*input_enum, upstream_enum, allowlisted_options));
  }
  return out;
}

void CollectEnums(const std::vector<ProtoFile::Enum>& enums,
                  const std::vector<ProtoFile::Message>& messages,
                  const std::string& prefix,
                  std::set<std::string>& out) {
  for (const auto& en : enums) {
    out.insert(prefix + en.name);
  }
  for (const auto& msg : messages) {
    CollectEnums(msg.enums, msg.nested_messages, prefix + msg.name + ".", out);
  }
}
// Protocol Buffers binary/wire-compatible type transitions.
// Transitions are allowed between types within the same category as they share
// the same wire format.
bool IsAllowedTypeTransition(const std::string& from,
                             const std::string& to,
                             const std::set<std::string>& known_enums) {
  auto is_primitive = [](const std::string& type) {
    return type == "double" || type == "float" || type == "int64" ||
           type == "uint64" || type == "int32" || type == "fixed64" ||
           type == "fixed32" || type == "bool" || type == "string" ||
           type == "bytes" || type == "uint32" || type == "sfixed32" ||
           type == "sfixed64" || type == "sint32" || type == "sint64";
  };
  auto is_varint = [&](const std::string& type) {
    return type == "int32" || type == "uint32" || type == "int64" ||
           type == "uint64" || type == "bool" || known_enums.count(type);
  };
  auto is_message = [&](const std::string& type) {
    return !is_primitive(type) && !known_enums.count(type);
  };

  if (is_varint(from) && is_varint(to)) {
    return true;
  }
  if ((from == "sint32" || from == "sint64") &&
      (to == "sint32" || to == "sint64")) {
    return true;
  }
  if ((from == "fixed32" || from == "sfixed32") &&
      (to == "fixed32" || to == "sfixed32")) {
    return true;
  }
  if ((from == "fixed64" || from == "sfixed64") &&
      (to == "fixed64" || to == "sfixed64")) {
    return true;
  }
  if ((from == "string" || from == "bytes") &&
      (to == "string" || to == "bytes")) {
    return true;
  }
  if ((from == "bytes" && is_message(to)) ||
      (is_message(from) && to == "bytes")) {
    return true;
  }
  return false;
}

base::Status MergeField(const ProtoFile::Field& input,
                        const ProtoFile::Field& upstream,
                        const std::set<std::string>& known_enums,
                        const std::set<std::string>& allowlisted_options,
                        ProtoFile::Field& out) {
  PERFETTO_CHECK(input.number == upstream.number);

  if (input.packageless_type != upstream.packageless_type) {
    if (!IsAllowedTypeTransition(input.packageless_type,
                                 upstream.packageless_type, known_enums)) {
      return base::ErrStatus(
          "The type of field with id %d and name %s (source of truth name: %s) "
          "changed from %s to %s. Please resolve conflict manually before "
          "rerunning.",
          input.number, input.name.c_str(), upstream.name.c_str(),
          input.packageless_type.c_str(), upstream.packageless_type.c_str());
    }
  }

  // If the packageless type name is the same but the type is different
  // mostly we should error however sometimes it is useful to allow downstream
  // to 'alias' an upstream type. For example 'Foo' to an existing internal
  // type in another package 'my.private.Foo'.
  if (input.packageless_type == upstream.packageless_type &&
      input.type != upstream.type) {
    if (!base::EndsWith(upstream.type, "Atom")) {
      return base::ErrStatus(
          "Upstream field with id %d and name '%s' "
          "(source of truth name: '%s') uses the type '%s' but we have the "
          "existing downstream type '%s'. Resolve this manually either by "
          "allowing this explicitly in proto_merger or editing the proto.",
          input.number, input.name.c_str(), upstream.name.c_str(),
          upstream.type.c_str(), input.type.c_str());
    }
  }

  // Get the comments, label and the name from the source of truth.
  out.leading_comments = upstream.leading_comments;
  out.trailing_comments = upstream.trailing_comments;
  out.is_repeated = upstream.is_repeated;
  out.name = upstream.name;

  // Get everything else from the input.
  out.number = input.number;
  out.options = input.options;

  MergeAllowlistedOptions(upstream.options, out.options, allowlisted_options);

  if (input.packageless_type != upstream.packageless_type) {
    out.packageless_type = upstream.packageless_type;
    out.type = upstream.type;
  } else {
    out.packageless_type = input.packageless_type;
    out.type = input.type;
  }

  return base::OkStatus();
}

base::Status MergeFields(const std::vector<ProtoFile::Field>& input,
                         const std::vector<ProtoFile::Field>& upstream,
                         const std::set<int>& allowlist,
                         const std::unordered_set<int>& reserved_numbers,
                         const std::set<std::string>& known_enums,
                         const std::set<std::string>& allowlisted_options,
                         std::vector<ProtoFile::Field>& out) {
  for (const auto& upstream_field : upstream) {
    auto* input_field = FindByNumber(input, upstream_field.number);
    if (!input_field) {
      // If the field is missing from the input but is present
      // in the allowlist, take the whole field from the
      // source of truth.
      if (allowlist.count(upstream_field.number))
        out.emplace_back(upstream_field);
      continue;
    }

    // Otherwise, merge the fields from the input and source of truth.
    ProtoFile::Field out_field;
    base::Status status = MergeField(*input_field, upstream_field, known_enums,
                                     allowlisted_options, out_field);
    if (!status.ok())
      return status;
    out.emplace_back(std::move(out_field));
  }

  // Append reserved fields from input as deprecated fields.
  for (const auto& input_field : input) {
    if (reserved_numbers.count(input_field.number)) {
      ProtoFile::Field deprecated_field = input_field;
      MarkFieldAsDeprecated(deprecated_field);
      out.emplace_back(std::move(deprecated_field));
    }
  }

  return base::OkStatus();
}

// We call both of these just "Merge" so that |MergeRecursive| below can
// reference them with the same name.
base::Status Merge(const ProtoFile::Oneof& input,
                   const ProtoFile::Oneof& upstream,
                   const Allowlist::Oneof& allowlist,
                   const std::set<std::string>& known_enums,
                   const std::set<std::string>& allowlisted_options,
                   ProtoFile::Oneof& out);

base::Status Merge(const ProtoFile::Message& input,
                   const ProtoFile::Message& upstream,
                   const Allowlist::Message& allowlist,
                   const std::set<std::string>& known_enums,
                   const std::set<std::string>& allowlisted_options,
                   ProtoFile::Message& out);

template <typename T, typename AllowlistType>
base::Status MergeRecursive(
    const std::vector<T>& input,
    const std::vector<T>& upstream,
    const std::map<std::string, AllowlistType>& allowlist_map,
    const std::set<std::string>& known_enums,
    const std::set<std::string>& allowlisted_options,
    std::vector<T>& out) {
  for (const auto& upstream_item : upstream) {
    auto opt_allowlist = FindInMap(allowlist_map, upstream_item.name);
    auto* input_item = FindByName(input, upstream_item.name);

    // If the value is not present in the input and the allowlist doesn't
    // exist either, this field is not approved so should not be included
    // in the output.
    if (!input_item && !opt_allowlist)
      continue;

    // If the input value doesn't exist, create a fake "input" that we can pass
    // to the merge function. This basically has the effect that the upstream
    // item is taken but *not* recursively; i.e. any fields which are inside the
    // message/oneof are checked against the allowlist individually. If we just
    // took the whole upstream here, we could add fields which were not
    // allowlisted.
    T input_or_fake;
    if (input_item) {
      input_or_fake = *input_item;
    } else {
      input_or_fake.name = upstream_item.name;
    }

    auto allowlist = opt_allowlist.value_or(AllowlistType{});
    T out_item;
    auto status = Merge(input_or_fake, upstream_item, allowlist, known_enums,
                        allowlisted_options, out_item);
    if (!status.ok())
      return status;
    out.emplace_back(std::move(out_item));
  }
  return base::OkStatus();
}

base::Status Merge(const ProtoFile::Oneof& input,
                   const ProtoFile::Oneof& upstream,
                   const Allowlist::Oneof& allowlist,
                   const std::set<std::string>& known_enums,
                   const std::set<std::string>& allowlisted_options,
                   ProtoFile::Oneof& out) {
  PERFETTO_CHECK(input.name == upstream.name);
  out.name = input.name;

  // Get the comments from the source of truth.
  out.leading_comments = upstream.leading_comments;
  out.trailing_comments = upstream.trailing_comments;

  // Compute all the fields present in the input but deleted in the
  // source of truth.
  out.deleted_fields = ComputeDeletedByNumber(input.fields, upstream.fields);

  // Finish by merging the list of fields.
  return MergeFields(input.fields, upstream.fields, allowlist, {}, known_enums,
                     allowlisted_options, out.fields);
}

base::Status Merge(const ProtoFile::Message& input,
                   const ProtoFile::Message& upstream,
                   const Allowlist::Message& allowlist,
                   const std::set<std::string>& known_enums,
                   const std::set<std::string>& allowlisted_options,
                   ProtoFile::Message& out) {
  PERFETTO_CHECK(input.name == upstream.name);
  out.name = input.name;

  // Get the comments from the source of truth.
  out.leading_comments = upstream.leading_comments;
  out.trailing_comments = upstream.trailing_comments;

  // Compute all the values present in the input but deleted in the
  // source of truth.
  out.deleted_enums = ComputeDeletedByName(input.enums, upstream.enums);
  out.deleted_nested_messages =
      ComputeDeletedByName(input.nested_messages, upstream.nested_messages);
  out.deleted_oneofs = ComputeDeletedByName(input.oneofs, upstream.oneofs);

  for (auto& field : ComputeDeletedByNumber(input.fields, upstream.fields)) {
    if (!upstream.reserved_numbers.count(field.number)) {
      out.deleted_fields.emplace_back(std::move(field));
    }
  }

  // Merge any nested enum types.
  out.enums = MergeEnums(input.enums, upstream.enums, allowlist.enums,
                         allowlisted_options);

  // Merge any nested message types.
  auto status = MergeRecursive(input.nested_messages, upstream.nested_messages,
                               allowlist.nested_messages, known_enums,
                               allowlisted_options, out.nested_messages);
  if (!status.ok())
    return status;

  // Merge any oneofs.
  status = MergeRecursive(input.oneofs, upstream.oneofs, allowlist.oneofs,
                          known_enums, allowlisted_options, out.oneofs);
  if (!status.ok())
    return status;

  // Finish by merging the list of fields.
  return MergeFields(input.fields, upstream.fields, allowlist.fields,
                     upstream.reserved_numbers, known_enums,
                     allowlisted_options, out.fields);
}

}  // namespace

base::Status MergeProtoFiles(const ProtoFile& input,
                             const ProtoFile& upstream,
                             const Allowlist& allowlist,
                             ProtoFile& out,
                             const std::set<std::string>& allowlisted_options) {
  // The preamble is taken directly from upstream. This allows private stuff
  // to be in the preamble without being present in upstream.
  out.preamble = input.preamble;

  std::set<std::string> known_enums;
  for (const auto& en : upstream.enums) {
    known_enums.insert(en.name);
  }
  for (const auto& msg : upstream.messages) {
    CollectEnums(msg.enums, msg.nested_messages, msg.name + ".", known_enums);
  }
  for (const auto& en : input.enums) {
    known_enums.insert(en.name);
  }
  for (const auto& msg : input.messages) {
    CollectEnums(msg.enums, msg.nested_messages, msg.name + ".", known_enums);
  }

  // Compute all the enums and messages present in the input but deleted in the
  // source of truth.
  out.deleted_enums = ComputeDeletedByName(input.enums, upstream.enums);
  out.deleted_messages =
      ComputeDeletedByName(input.messages, upstream.messages);

  // Merge the top-level enums.
  out.enums = MergeEnums(input.enums, upstream.enums, allowlist.enums,
                         allowlisted_options);

  // Finish by merging the top-level messages.
  return MergeRecursive(input.messages, upstream.messages, allowlist.messages,
                        known_enums, allowlisted_options, out.messages);
}

}  // namespace proto_merger
}  // namespace perfetto
