/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/util/descriptors.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/common/descriptor.pbzero.h"
#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"

namespace perfetto::trace_processor {
namespace {

// Types deleted from the schema that an old trace's embedded descriptor may
// still reference (e.g. https://github.com/google/perfetto/pull/6308). Such
// fields are skipped rather than failing the trace.
// TODO(b/524094370): harden this once traces predating the removals age out.
bool IsIntentionallyRemovedType(const std::string& raw_type_name) {
  static const char* const kRemovedTypes[] = {
      ".perfetto.protos.AndroidCameraFrameEvent",
      ".perfetto.protos.AndroidCameraSessionStats",
  };
  for (const char* removed : kRemovedTypes) {
    if (raw_type_name == removed) {
      return true;
    }
  }
  return false;
}

FieldDescriptor CreateFieldFromDecoder(
    const protos::pbzero::FieldDescriptorProto::Decoder& f_decoder,
    bool is_extension) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  std::string type_name =
      f_decoder.has_type_name()
          ? base::StringView(f_decoder.type_name()).ToStdString()
          : "";
  // TODO(lalitm): add support for enums here.
  uint32_t type =
      f_decoder.has_type()
          ? static_cast<uint32_t>(f_decoder.type())
          : static_cast<uint32_t>(FieldDescriptorProto::TYPE_MESSAGE);
  protos::pbzero::FieldOptions::Decoder opt(f_decoder.options());
  std::optional<std::string> default_value;
  if (f_decoder.has_default_value()) {
    default_value = f_decoder.default_value().ToStdString();
  }
  return {
      base::StringView(f_decoder.name()).ToStdString(),
      static_cast<uint32_t>(f_decoder.number()),
      type,
      std::move(type_name),
      std::vector<uint8_t>(f_decoder.options().data,
                           f_decoder.options().data + f_decoder.options().size),
      default_value,
      f_decoder.label() == FieldDescriptorProto::LABEL_REPEATED,
      opt.packed(),
      is_extension,
  };
}

base::Status CheckExtensionField(
    const ProtoDescriptor& proto_descriptor,
    const FieldDescriptor& field,
    std::vector<ExtensionTypeCheck>* extension_type_checks) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;

  const auto* existing_field = proto_descriptor.FindFieldByTag(field.number());
  if (!existing_field) {
    return base::OkStatus();
  }

  // A re-declared scalar is fine as long as its wire type is unchanged (e.g.
  // bool -> uint32, https://github.com/google/perfetto/pull/6082): the bytes
  // stay decodable. Reject only wire-type changes.
  // TODO(b/524094370): harden this once traces predating the widening age out.
  using protozero::proto_utils::ProtoSchemaToWireType;
  using protozero::proto_utils::ProtoSchemaType;
  if (ProtoSchemaToWireType(static_cast<ProtoSchemaType>(field.type())) !=
      ProtoSchemaToWireType(
          static_cast<ProtoSchemaType>(existing_field->type()))) {
    return base::ErrStatus("Field %s is re-introduced with different type",
                           field.name().c_str());
  }

  const bool is_msg_or_enum =
      field.type() == FieldDescriptorProto::TYPE_MESSAGE ||
      field.type() == FieldDescriptorProto::TYPE_ENUM;
  if (is_msg_or_enum &&
      field.raw_type_name() != existing_field->raw_type_name()) {
    // Same tag, same fundamental type, but the message/enum is named
    // differently (e.g. a package rename during an out-of-tree migration).
    // Defer a structural-compatibility check until after type resolution
    // has populated resolved_type_name() for all fields.
    extension_type_checks->push_back(
        {/*extendee_full_name=*/proto_descriptor.full_name(),
         /*field_name=*/field.name(),
         /*existing_raw_type=*/existing_field->raw_type_name(),
         /*new_raw_type=*/field.raw_type_name()});
  }
  return base::OkStatus();
}

// True if `existing` and `candidate` are interchangeable as far as how
// trace_processor decodes and queries a *scalar* field. Number, name,
// repeated/packed and default must be identical. The scalar type may differ
// as long as the protozero wire type is the same (e.g. int32 -> int64).
bool AreFieldDescriptorsCompatible(const FieldDescriptor& existing,
                                   const FieldDescriptor& candidate) {
  if (existing.number() != candidate.number() ||
      existing.name() != candidate.name() ||
      existing.is_repeated() != candidate.is_repeated() ||
      existing.is_packed() != candidate.is_packed() ||
      existing.default_value() != candidate.default_value()) {
    return false;
  }
  // FieldDescriptor::type() stores the protobuf FieldDescriptorProto::Type
  // numeric value. protozero's ProtoSchemaType deliberately mirrors that
  // numbering, so this static_cast is well-defined.
  using protozero::proto_utils::ProtoSchemaToWireType;
  using protozero::proto_utils::ProtoSchemaType;
  return ProtoSchemaToWireType(static_cast<ProtoSchemaType>(existing.type())) ==
         ProtoSchemaToWireType(static_cast<ProtoSchemaType>(candidate.type()));
}

// True if `existing` and `candidate` enums don't contradict each other:
// every number they both define must map to the same name. Numbers defined
// on only one side are fine -- that just means one enum has values the other
// doesn't, which happens when an enum gains values over time.
bool AreEnumValuesCompatible(const ProtoDescriptor& existing,
                             const ProtoDescriptor& candidate) {
  for (const auto& [number, name] : existing.enum_values_by_number()) {
    auto it = candidate.enum_values_by_number().find(number);
    if (it != candidate.enum_values_by_number().end() && it->second != name) {
      return false;
    }
  }
  return true;
}

// True if the bool option |option_number| is set on |field|.
bool FieldBoolOption(const FieldDescriptor& field, uint32_t option_number) {
  protozero::ProtoDecoder opt(field.options().data(), field.options().size());
  auto f = opt.FindField(option_number);
  return f.valid() && f.as_bool();
}

}  // namespace

std::optional<uint32_t> DescriptorPool::ResolveShortType(
    const std::string& parent_path,
    const std::string& short_type) {
  PERFETTO_DCHECK(!short_type.empty());

  std::string search_path = short_type[0] == '.'
                                ? parent_path + short_type
                                : parent_path + '.' + short_type;
  auto opt_idx = FindDescriptorIdx(search_path);
  if (opt_idx)
    return opt_idx;

  if (parent_path.empty())
    return std::nullopt;

  auto parent_dot_idx = parent_path.rfind('.');
  auto parent_substr = parent_dot_idx == std::string::npos
                           ? ""
                           : parent_path.substr(0, parent_dot_idx);
  return ResolveShortType(parent_substr, short_type);
}

bool DescriptorPool::DescriptorsStructurallyEqual(
    uint32_t root_existing_idx,
    uint32_t root_candidate_idx,
    std::set<CanonicalDescriptorPair>& comparisons_in_progress) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;

  struct DescriptorComparison {
    uint32_t existing_idx;
    uint32_t candidate_idx;
  };
  std::vector<DescriptorComparison> worklist;

  worklist.push_back({root_existing_idx, root_candidate_idx});
  while (!worklist.empty()) {
    const auto [existing_idx, candidate_idx] = worklist.back();
    worklist.pop_back();

    // Same descriptor: trivially equal, nothing to check.
    if (existing_idx == candidate_idx) {
      continue;
    }

    // If we are already comparing this exact pair of descriptors (it is
    // somewhere else in the worklist or has already been started), treat it
    // as equal. This is what lets self-referential and mutually-recursive
    // messages terminate instead of looping forever.
    if (!comparisons_in_progress
             .insert(CanonicalDescriptorPair(existing_idx, candidate_idx))
             .second) {
      continue;
    }

    const ProtoDescriptor& existing_desc = descriptors_[existing_idx];
    const ProtoDescriptor& candidate_desc = descriptors_[candidate_idx];
    if (existing_desc.type() != candidate_desc.type()) {
      return false;
    }

    // If both sides are enums, they match when every value number they both
    // define maps to the same name; a number on only one side is allowed
    // (one enum gained values the other doesn't have). Enums have no
    // sub-types to queue.
    if (existing_desc.type() == ProtoDescriptor::Type::kEnum) {
      if (!AreEnumValuesCompatible(existing_desc, candidate_desc)) {
        return false;
      }
      continue;
    }

    // Check every field they both define matches exactly. A field number on
    // only one side is fine -- that just means one type has a field the
    // other doesn't, which happens when a type gains fields over time.
    for (const auto& entry : existing_desc.fields()) {
      const FieldDescriptor& existing_field = entry.second;
      const FieldDescriptor* candidate_field =
          candidate_desc.FindFieldByTag(existing_field.number());

      if (candidate_field == nullptr) {
        continue;
      }

      if (!AreFieldDescriptorsCompatible(existing_field, *candidate_field)) {
        return false;
      }

      const bool field_is_message_or_enum =
          existing_field.type() == FieldDescriptorProto::TYPE_MESSAGE ||
          existing_field.type() == FieldDescriptorProto::TYPE_ENUM;
      if (!field_is_message_or_enum) {
        continue;
      }

      std::optional<uint32_t> existing_sub_idx =
          FindDescriptorIdx(existing_field.resolved_type_name());
      std::optional<uint32_t> candidate_sub_idx =
          FindDescriptorIdx(candidate_field->resolved_type_name());
      if (!existing_sub_idx.has_value() || !candidate_sub_idx.has_value()) {
        return false;
      }
      worklist.push_back({existing_sub_idx.value(), candidate_sub_idx.value()});
    }
  }
  return true;
}

base::Status DescriptorPool::AddExtensionField(
    const ExtensionInfo& extension,
    std::vector<ExtensionTypeCheck>* extension_type_checks) {
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  FieldDescriptorProto::Decoder f_decoder(extension.field_desc_proto);
  auto field = CreateFieldFromDecoder(f_decoder, true);

  std::string_view scope = extension.parent_full_name.empty()
                               ? extension.package_name
                               : extension.parent_full_name;
  PERFETTO_DCHECK(!scope.empty() && scope[0] == '.');
  scope.remove_prefix(1);
  std::string extension_full_name(scope);
  if (!scope.empty())
    extension_full_name.push_back('.');
  extension_full_name.append(field.name());
  field.set_extension_full_name(extension_full_name);

  std::string extendee_name = f_decoder.extendee().ToStdString();
  if (extendee_name.empty()) {
    return base::ErrStatus("Extendee name is empty");
  }

  if (extendee_name[0] != '.') {
    // Only prepend if the extendee is not fully qualified
    extendee_name = extension.package_name + "." + extendee_name;
  }
  std::optional<uint32_t> extendee = FindDescriptorIdx(extendee_name);
  if (!extendee.has_value()) {
    return base::ErrStatus("Extendee does not exist %s", extendee_name.c_str());
  }
  ProtoDescriptor& extendee_desc = descriptors_[extendee.value()];
  RETURN_IF_ERROR(
      CheckExtensionField(extendee_desc, field, extension_type_checks));
  extendee_desc.AddField(field);
  return base::OkStatus();
}

base::Status DescriptorPool::AddNestedProtoDescriptors(
    const std::string& file_name,
    const std::string& package_name,
    std::optional<uint32_t> parent_idx,
    protozero::ConstBytes descriptor_proto,
    std::vector<ExtensionInfo>* extensions,
    std::vector<ExtensionTypeCheck>* extension_type_checks,
    bool merge_existing_messages) {
  protos::pbzero::DescriptorProto::Decoder decoder(descriptor_proto);

  auto parent_name =
      parent_idx ? descriptors_[*parent_idx].full_name() : package_name;
  auto full_name =
      parent_name + "." + base::StringView(decoder.name()).ToStdString();

  auto idx = FindDescriptorIdx(full_name);
  if (idx.has_value() && !merge_existing_messages) {
    const auto& existing_descriptor = descriptors_[*idx];
    return base::ErrStatus("%s: %s was already defined in file %s",
                           file_name.c_str(), full_name.c_str(),
                           existing_descriptor.file_name().c_str());
  }
  if (!idx.has_value()) {
    ProtoDescriptor proto_descriptor(file_name, package_name, full_name,
                                     ProtoDescriptor::Type::kMessage,
                                     parent_idx);
    idx = AddProtoDescriptor(std::move(proto_descriptor));
  }
  ProtoDescriptor& proto_descriptor = descriptors_[*idx];
  if (proto_descriptor.type() != ProtoDescriptor::Type::kMessage) {
    return base::ErrStatus("%s was enum, redefined as message",
                           full_name.c_str());
  }

  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  for (auto it = decoder.field(); it; ++it) {
    FieldDescriptorProto::Decoder f_decoder(*it);
    auto field = CreateFieldFromDecoder(f_decoder, /*is_extension=*/false);
    RETURN_IF_ERROR(
        CheckExtensionField(proto_descriptor, field, extension_type_checks));
    proto_descriptor.AddField(std::move(field));
  }

  for (auto it = decoder.enum_type(); it; ++it) {
    RETURN_IF_ERROR(AddEnumProtoDescriptors(file_name, package_name, idx, *it,
                                            merge_existing_messages));
  }
  for (auto it = decoder.nested_type(); it; ++it) {
    RETURN_IF_ERROR(AddNestedProtoDescriptors(file_name, package_name, idx, *it,
                                              extensions, extension_type_checks,
                                              merge_existing_messages));
  }
  for (auto ext_it = decoder.extension(); ext_it; ++ext_it) {
    extensions->push_back(
        {package_name, proto_descriptor.full_name(), *ext_it});
  }
  return base::OkStatus();
}

base::Status DescriptorPool::AddEnumProtoDescriptors(
    const std::string& file_name,
    const std::string& package_name,
    std::optional<uint32_t> parent_idx,
    protozero::ConstBytes descriptor_proto,
    bool merge_existing_messages) {
  protos::pbzero::EnumDescriptorProto::Decoder decoder(descriptor_proto);

  auto parent_name =
      parent_idx ? descriptors_[*parent_idx].full_name() : package_name;
  auto full_name =
      parent_name + "." + base::StringView(decoder.name()).ToStdString();

  auto prev_idx = FindDescriptorIdx(full_name);
  if (prev_idx.has_value() && !merge_existing_messages) {
    const auto& existing_descriptor = descriptors_[*prev_idx];
    return base::ErrStatus("%s: %s was already defined in file %s",
                           file_name.c_str(), full_name.c_str(),
                           existing_descriptor.file_name().c_str());
  }
  if (!prev_idx.has_value()) {
    ProtoDescriptor proto_descriptor(file_name, package_name, full_name,
                                     ProtoDescriptor::Type::kEnum,
                                     std::nullopt);
    prev_idx = AddProtoDescriptor(std::move(proto_descriptor));
  }
  ProtoDescriptor& proto_descriptor = descriptors_[*prev_idx];
  if (proto_descriptor.type() != ProtoDescriptor::Type::kEnum) {
    return base::ErrStatus("%s was message, redefined as enum",
                           full_name.c_str());
  }

  for (auto it = decoder.value(); it; ++it) {
    protos::pbzero::EnumValueDescriptorProto::Decoder enum_value(it->data(),
                                                                 it->size());
    proto_descriptor.AddEnumValue(enum_value.number(),
                                  enum_value.name().ToStdString());
  }

  return base::OkStatus();
}

base::Status DescriptorPool::AddFromFileDescriptorSet(
    const uint8_t* file_descriptor_set_proto,
    size_t size,
    const std::vector<std::string>& skip_prefixes,
    bool merge_existing_messages) {
  protos::pbzero::FileDescriptorSet::Decoder proto(file_descriptor_set_proto,
                                                   size);
  std::vector<ExtensionInfo> extensions;
  std::vector<ExtensionTypeCheck> extension_type_checks;
  for (auto it = proto.file(); it; ++it) {
    protos::pbzero::FileDescriptorProto::Decoder file(*it);
    const std::string file_name = file.name().ToStdString();
    if (base::StartsWithAny(file_name, skip_prefixes))
      continue;
    if (!merge_existing_messages &&
        processed_files_.find(file_name) != processed_files_.end()) {
      // This file has been loaded once already. Skip.
      continue;
    }
    processed_files_.insert(file_name);
    std::string package = "." + base::StringView(file.package()).ToStdString();
    for (auto message_it = file.message_type(); message_it; ++message_it) {
      RETURN_IF_ERROR(AddNestedProtoDescriptors(
          file_name, package, std::nullopt, *message_it, &extensions,
          &extension_type_checks, merge_existing_messages));
    }
    for (auto enum_it = file.enum_type(); enum_it; ++enum_it) {
      RETURN_IF_ERROR(AddEnumProtoDescriptors(
          file_name, package, std::nullopt, *enum_it, merge_existing_messages));
    }
    for (auto ext_it = file.extension(); ext_it; ++ext_it) {
      extensions.push_back({package, /*parent_full_name=*/"", *ext_it});
    }
  }

  // Second pass: Add extension fields to the real protos.
  for (const auto& extension : extensions) {
    RETURN_IF_ERROR(AddExtensionField(extension, &extension_type_checks));
  }

  // Third pass: resolve the types of all the fields.
  using FieldDescriptorProto = protos::pbzero::FieldDescriptorProto;
  for (ProtoDescriptor& descriptor : descriptors_) {
    for (auto& entry : *descriptor.mutable_fields()) {
      FieldDescriptor& field = entry.second;
      bool needs_resolution =
          field.resolved_type_name().empty() &&
          (field.type() == FieldDescriptorProto::TYPE_MESSAGE ||
           field.type() == FieldDescriptorProto::TYPE_ENUM);
      if (needs_resolution) {
        auto opt_desc =
            ResolveShortType(descriptor.full_name(), field.raw_type_name());
        if (!opt_desc.has_value()) {
          if (IsIntentionallyRemovedType(field.raw_type_name())) {
            continue;
          }
          return base::ErrStatus(
              "Unable to find short type %s in field inside message %s",
              field.raw_type_name().c_str(), descriptor.full_name().c_str());
        }
        field.set_resolved_type_name(
            descriptors_[opt_desc.value()].full_name());
      }
    }
  }

  // Fourth pass: verify deferred type checks are structurally compatible
  // now that all field types have been resolved.
  for (const auto& check : extension_type_checks) {
    std::optional<uint32_t> opt_existing_idx =
        ResolveShortType(check.extendee_full_name, check.existing_raw_type);
    std::optional<uint32_t> opt_new_idx =
        ResolveShortType(check.extendee_full_name, check.new_raw_type);
    // Both types must resolve before we can compare; either can be absent.
    //  - existing: TP's pool may reference a type whose definition wasn't
    //  loaded.
    //  - new: trace's descriptor may name a type without including its
    //  definition.
    if (!opt_existing_idx.has_value() || !opt_new_idx.has_value()) {
      // A type isn't in the pool: normal for a trace recorded before an
      // out-of-tree migration renamed it. Can't compare structurally, but the
      // tag and wire type already matched and the existing definition is kept,
      // so the field still decodes. Tolerate rather than reject the trace.
      // TODO(b/524094370): harden this once OOT migrations stabilize.
      PERFETTO_DLOG(
          "Field %s re-introduced as %s (was %s): unresolved type, "
          "skipping compatibility check",
          check.field_name.c_str(), check.new_raw_type.c_str(),
          check.existing_raw_type.c_str());
      continue;
    }
    std::set<CanonicalDescriptorPair> comparisons_in_progress;
    if (!DescriptorsStructurallyEqual(opt_existing_idx.value(),
                                      opt_new_idx.value(),
                                      comparisons_in_progress)) {
      return base::ErrStatus(
          "Field %s re-introduced as %s (was %s) and the two messages are "
          "not structurally identical",
          check.field_name.c_str(), check.new_raw_type.c_str(),
          check.existing_raw_type.c_str());
    }
  }

  // Fifth pass: interpret options and resolve Perfetto's custom field options.
  const CustomOptionNumbers option_numbers = FindCustomOptionNumbers();
  for (ProtoDescriptor& descriptor : descriptors_) {
    for (auto& entry : *descriptor.mutable_fields()) {
      FieldDescriptor& field = entry.second;
      if (field.options().empty()) {
        continue;
      }
      ResolveUninterpretedOption(descriptor, field, *field.mutable_options());
      ResolveCustomFieldOptions(descriptor, option_numbers, &field);
    }
  }
  return base::OkStatus();
}

base::Status DescriptorPool::ResolveUninterpretedOption(
    const ProtoDescriptor& proto_desc,
    const FieldDescriptor& field_desc,
    std::vector<uint8_t>& options) {
  auto opt_idx = FindDescriptorIdx(".google.protobuf.FieldOptions");
  if (!opt_idx) {
    return base::ErrStatus("Unable to find field options for field %s in %s",
                           field_desc.name().c_str(),
                           proto_desc.full_name().c_str());
  }
  ProtoDescriptor& field_options_desc = descriptors_[*opt_idx];

  protozero::ProtoDecoder decoder(field_desc.options().data(),
                                  field_desc.options().size());
  protozero::HeapBuffered<protozero::Message> field_options;
  for (;;) {
    const uint8_t* start = decoder.begin() + decoder.read_offset();
    auto field = decoder.ReadField();
    if (!field.valid()) {
      break;
    }
    const uint8_t* end = decoder.begin() + decoder.read_offset();

    if (field.id() !=
        protos::pbzero::FieldOptions::kUninterpretedOptionFieldNumber) {
      field_options->AppendRawProtoBytes(start,
                                         static_cast<size_t>(end - start));
      continue;
    }

    protos::pbzero::UninterpretedOption::Decoder unint(field.as_bytes());
    auto it = unint.name();
    if (!it) {
      return base::ErrStatus(
          "Option for field %s in message %s does not have a name",
          field_desc.name().c_str(), proto_desc.full_name().c_str());
    }
    protos::pbzero::UninterpretedOption::NamePart::Decoder name_part(*it);
    const auto* option_field_desc =
        field_options_desc.FindFieldByName(name_part.name_part().ToStdString());

    // It's not immediately clear how options with multiple names should
    // be parsed. This likely requires digging into protobuf compiler
    // source; given we don't have any examples of this in the codebase
    // today, defer handling of this to when we may need it.
    if (++it) {
      return base::ErrStatus(
          "Option for field %s in message %s has multiple name segments",
          field_desc.name().c_str(), proto_desc.full_name().c_str());
    }
    if (unint.has_identifier_value()) {
      field_options->AppendString(option_field_desc->number(),
                                  unint.identifier_value().ToStdString());
    } else if (unint.has_positive_int_value()) {
      field_options->AppendVarInt(option_field_desc->number(),
                                  unint.positive_int_value());
    } else if (unint.has_negative_int_value()) {
      field_options->AppendVarInt(option_field_desc->number(),
                                  unint.negative_int_value());
    } else if (unint.has_double_value()) {
      field_options->AppendFixed(option_field_desc->number(),
                                 unint.double_value());
    } else if (unint.has_string_value()) {
      field_options->AppendString(option_field_desc->number(),
                                  unint.string_value().ToStdString());
    } else if (unint.has_aggregate_value()) {
      field_options->AppendString(option_field_desc->number(),
                                  unint.aggregate_value().ToStdString());
    } else {
      return base::ErrStatus(
          "Unknown field set in UninterpretedOption %s for field %s in message "
          "%s",
          option_field_desc->name().c_str(), field_desc.name().c_str(),
          proto_desc.full_name().c_str());
    }
  }
  if (decoder.bytes_left() > 0) {
    return base::ErrStatus("Unexpected extra bytes when parsing option %zu",
                           decoder.bytes_left());
  }
  options = field_options.SerializeAsArray();
  return base::OkStatus();
}

void DescriptorPool::ResolveFlagsEnumOption(const ProtoDescriptor& descriptor,
                                            uint32_t option_number,
                                            FieldDescriptor* field) {
  protozero::ProtoDecoder opt(field->options().data(), field->options().size());
  auto f = opt.FindField(option_number);
  if (!f.valid()) {
    return;
  }
  if (auto enum_idx =
          ResolveShortType(descriptor.full_name(), f.as_std_string())) {
    field->set_flags_enum_descriptor_idx(*enum_idx);
  }
}

DescriptorPool::CustomOptionNumbers DescriptorPool::FindCustomOptionNumbers()
    const {
  CustomOptionNumbers numbers;
  auto idx = FindDescriptorIdx(".google.protobuf.FieldOptions");
  if (!idx) {
    return numbers;
  }
  const ProtoDescriptor& field_options = descriptors_[*idx];
  if (const auto* opt = field_options.FindFieldByName("flags_enum")) {
    numbers.flags_enum = opt->number();
  }
  if (const auto* opt = field_options.FindFieldByName("is_pid")) {
    numbers.pid = opt->number();
  }
  if (const auto* opt = field_options.FindFieldByName("is_tid")) {
    numbers.tid = opt->number();
  }
  return numbers;
}

void DescriptorPool::ResolveCustomFieldOptions(
    const ProtoDescriptor& descriptor,
    const CustomOptionNumbers& numbers,
    FieldDescriptor* field) {
  if (numbers.flags_enum) {
    ResolveFlagsEnumOption(descriptor, *numbers.flags_enum, field);
  }
  if (numbers.pid && FieldBoolOption(*field, *numbers.pid)) {
    field->set_is_pid(true);
  }
  if (numbers.tid && FieldBoolOption(*field, *numbers.tid)) {
    field->set_is_tid(true);
  }
}

std::optional<uint32_t> DescriptorPool::FindDescriptorIdx(
    const std::string& full_name) const {
  auto it = full_name_to_descriptor_index_.find(full_name);
  if (it == full_name_to_descriptor_index_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> DescriptorPool::FindEnumString(
    CachedDescriptor& cache,
    std::string_view enum_name,
    int32_t value) const {
  if (!cache.descriptor_idx_) {
    cache.descriptor_idx_ = FindDescriptorIdx(std::string(enum_name));
  }
  if (!cache.descriptor_idx_) {
    return std::nullopt;
  }
  return descriptors_[*cache.descriptor_idx_].FindEnumString(value);
}

int64_t DescriptorPool::FlagSetToViews(
    uint32_t enum_descriptor_idx,
    int64_t mask,
    std::vector<std::string_view>* out) const {
  const ProtoDescriptor& desc = descriptors_[enum_descriptor_idx];
  if (desc.type() != ProtoDescriptor::Type::kEnum) {
    return mask;
  }
  const auto& names_by_value = desc.enum_values_by_number();
  uint64_t unmatched = 0;
  for (auto bits = static_cast<uint64_t>(mask); bits != 0; bits &= bits - 1) {
    uint64_t flag = bits & ~(bits - 1);  // lowest set bit
    // int32 enum values: only bits 0..31 can match (bit 31 = INT32_MIN).
    auto it = flag < (uint64_t{1} << 32)
                  ? names_by_value.find(static_cast<int32_t>(flag))
                  : names_by_value.end();
    if (it != names_by_value.end()) {
      out->push_back(it->second);
    } else {
      unmatched |= flag;
    }
  }
  return static_cast<int64_t>(unmatched);
}

std::vector<uint8_t> DescriptorPool::SerializeAsDescriptorSet() const {
  protozero::HeapBuffered<protos::pbzero::DescriptorSet> descs;
  for (const auto& desc : descriptors()) {
    protos::pbzero::DescriptorProto* proto_descriptor =
        descs->add_descriptors();
    proto_descriptor->set_name(desc.full_name());
    for (const auto& entry : desc.fields()) {
      const auto& field = entry.second;
      protos::pbzero::FieldDescriptorProto* field_descriptor =
          proto_descriptor->add_field();
      field_descriptor->set_name(field.name());
      field_descriptor->set_number(static_cast<int32_t>(field.number()));
      // We do not support required fields. They will show up as
      // optional after serialization.
      field_descriptor->set_label(
          field.is_repeated()
              ? protos::pbzero::FieldDescriptorProto::LABEL_REPEATED
              : protos::pbzero::FieldDescriptorProto::LABEL_OPTIONAL);
      field_descriptor->set_type_name(field.resolved_type_name());
      field_descriptor->set_type(
          static_cast<protos::pbzero::FieldDescriptorProto_Type>(field.type()));
    }
  }
  return descs.SerializeAsArray();
}

uint32_t DescriptorPool::AddProtoDescriptor(ProtoDescriptor descriptor) {
  uint32_t idx = static_cast<uint32_t>(descriptors_.size());
  full_name_to_descriptor_index_[descriptor.full_name()] = idx;
  descriptors_.emplace_back(std::move(descriptor));
  return idx;
}

ProtoDescriptor::ProtoDescriptor(std::string file_name,
                                 std::string package_name,
                                 std::string full_name,
                                 Type type,
                                 std::optional<uint32_t> parent_id)
    : file_name_(std::move(file_name)),
      package_name_(std::move(package_name)),
      full_name_(std::move(full_name)),
      type_(type),
      parent_id_(parent_id) {}

FieldDescriptor::FieldDescriptor(std::string name,
                                 uint32_t number,
                                 uint32_t type,
                                 std::string raw_type_name,
                                 std::vector<uint8_t> options,
                                 std::optional<std::string> default_value,
                                 bool is_repeated,
                                 bool is_packed,
                                 bool is_extension)
    : name_(std::move(name)),
      number_(number),
      type_(type),
      raw_type_name_(std::move(raw_type_name)),
      options_(std::move(options)),
      default_value_(std::move(default_value)),
      is_repeated_(is_repeated),
      is_packed_(is_packed),
      is_extension_(is_extension) {}

}  // namespace perfetto::trace_processor
