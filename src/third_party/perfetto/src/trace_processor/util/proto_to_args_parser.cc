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
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/trace_processor/util/descriptors.h"

#include "protos/perfetto/common/descriptor.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"

namespace perfetto::trace_processor::util {

namespace {

template <protozero::proto_utils::ProtoWireType wire_type, typename cpp_type>
using PRFI = protozero::PackedRepeatedFieldIterator<wire_type, cpp_type>;

constexpr char kDebugAnnotationTypeName[] = ".perfetto.protos.DebugAnnotation";

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

  // RAII context for the current message's key. Restores the key prefix on
  // destruction (i.e. when this WorkItem is popped from the stack).
  ScopedNestedKeyContext key_context;

  // Set to false as soon as any field is parsed for this message.
  bool empty_message = true;
};

struct ProtoToArgsParser::DebugAnnotationWorkItem {
  // The decoder for the current annotation node.
  protos::pbzero::DebugAnnotation::Decoder decoder;
  // The key context for the current annotation node.
  ScopedNestedKeyContext key;
  // The index of the current array entry.
  std::optional<size_t> array_index = {};
  // [first, first + count) range into da_nested_storage_ holding this node's
  // children while iterating dict_entries / array_values.
  uint32_t nested_count = 0;
  uint32_t nested_current = 0;
  // Whether processing of this node added any entry to the delegate.
  bool added_entry = false;
  // Whether the dict/array enumeration has been done once already.
  bool first_pass_done = false;
  // True once a child (proto_value or nested_value subtree) has been pushed.
  // Prevents the proto_value/nested_value branches from being re-entered when
  // resumption returns to this item after the child completes.
  bool subtree_pushed = false;
};

struct ProtoToArgsParser::NestedValueWorkItem {
  protos::pbzero::DebugAnnotation::NestedValue::Decoder decoder;
  // The root NestedValue (pushed by a DebugAnnotation nested_value field)
  // borrows its key from the parent DebugAnnotation immediately below it on
  // the work stack, so this is empty for that item only. Child NestedValues
  // always populate their own key.
  std::optional<ScopedNestedKeyContext> key = std::nullopt;

  std::optional<size_t> array_index = std::nullopt;
  uint32_t nested_count = 0;
  uint32_t nested_current = 0;
  bool added_entry = false;
  bool first_pass_done = false;
};

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

ProtoToArgsParser::ScopedNestedKeyContext&
ProtoToArgsParser::ScopedNestedKeyContext::operator=(
    ScopedNestedKeyContext&& other) noexcept {
  PERFETTO_DCHECK(&key_ == &other.key_);
  RemoveFieldSuffix();
  old_flat_key_length_ = other.old_flat_key_length_;
  old_key_length_ = other.old_key_length_;
  other.old_flat_key_length_ = std::nullopt;
  other.old_key_length_ = std::nullopt;
  return *this;
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

// Out-of-line and placed after the WorkItem struct definitions so the
// std::vector<std::variant<...>> members' constructors and destructors (which
// need all variant alternatives to be complete types) are instantiated in
// this translation unit.
ProtoToArgsParser::ProtoToArgsParser(const DescriptorPool& pool) : pool_(pool) {
  constexpr int kDefaultSize = 64;
  key_prefix_.key.reserve(kDefaultSize);
  key_prefix_.flat_key.reserve(kDefaultSize);
}

ProtoToArgsParser::~ProtoToArgsParser() = default;

base::Status ProtoToArgsParser::ParseMessage(
    const protozero::ConstBytes& cb,
    const std::string& type,
    const std::vector<uint32_t>* allowed_fields,
    Delegate& delegate,
    int* unknown_extensions,
    bool add_defaults) {
  // If the caller has opted into DebugAnnotation handling, top-level
  // DebugAnnotation parses go through the dedicated entry point which uses
  // DebugAnnotation parsing semantics rather than generic proto reflection.
  if (debug_annotation_enabled_ && type == kDebugAnnotationTypeName) {
    return ParseDebugAnnotation(cb, delegate);
  }
  PERFETTO_DCHECK(work_stack_.empty());
  auto idx = pool_.FindDescriptorIdx(type);
  if (!idx) {
    return base::Status("Failed to find proto descriptor for " + type);
  }
  allowed_fields_ = allowed_fields;
  unknown_extensions_ = unknown_extensions;
  add_defaults_ = add_defaults;
  work_stack_.emplace_back(WorkItem{cb,
                                    protozero::ProtoDecoder(cb),
                                    &pool_.descriptors()[*idx],
                                    {},
                                    {},
                                    ScopedNestedKeyContext(key_prefix_),
                                    true});
  return RunWorkLoop(delegate);
}

base::Status ProtoToArgsParser::RunWorkLoop(Delegate& delegate) {
  // Drain scratch state on every exit path: work items hold raw pointers into
  // descriptors and packet bytes that don't outlive this call. Pop in LIFO
  // order so ScopedNestedKeyContext restores key_prefix_ correctly.
  auto cleanup = base::OnScopeExit([this] {
    while (!work_stack_.empty()) {
      work_stack_.pop_back();
    }
    da_nested_storage_.clear();
    nv_nested_storage_.clear();
  });
  while (!work_stack_.empty()) {
    bool done = false;
    auto& top = work_stack_.back();
    if (auto* item = std::get_if<WorkItem>(&top)) {
      RETURN_IF_ERROR(StepProtoMessage(*item, delegate, done));
    } else if (auto* da = std::get_if<DebugAnnotationWorkItem>(&top)) {
      RETURN_IF_ERROR(StepDebugAnnotation(*da, delegate, done));
    } else {
      auto& nv = std::get<NestedValueWorkItem>(top);
      RETURN_IF_ERROR(
          StepNestedValue(nv, work_stack_.size() - 1, delegate, done));
    }
    if (done) {
      work_stack_.pop_back();
    }
  }
  return base::OkStatus();
}

base::Status ProtoToArgsParser::StepProtoMessage(WorkItem& item,
                                                 Delegate& delegate,
                                                 bool& done) {
  if (auto override_result =
          MaybeApplyOverrideForType(item.descriptor->full_name(),
                                    item.key_context, item.data, delegate)) {
    done = true;
    return override_result.value();
  }

  protozero::Field field = item.decoder.ReadField();
  if (field.valid()) {
    item.empty_message = false;
    const auto* field_descriptor = item.descriptor->FindFieldByTag(field.id());
    if (!field_descriptor) {
      if (unknown_extensions_ != nullptr) {
        (*unknown_extensions_)++;
      }
      // Unknown field, possibly an unknown extension.
      return base::OkStatus();
    }

    if (add_defaults_) {
      item.existing_fields.insert(field_descriptor->number());
    }

    // The allowlist only applies to the top-level message.
    if (work_stack_.size() == 1 &&
        !IsFieldAllowed(*field_descriptor, allowed_fields_)) {
      // Field is neither an extension, nor is allowed to be reflected.
      return base::OkStatus();
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
      return ParsePackedField(*field_descriptor, item.repeated_field_index,
                              field, delegate);
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
      return *status;
    }

    if (field_descriptor->type() == FieldDescriptorProto::TYPE_MESSAGE) {
      const std::string& resolved = field_descriptor->resolved_type_name();
      if (debug_annotation_enabled_ && resolved == kDebugAnnotationTypeName) {
        // Hand off to the DebugAnnotation path on the same work stack so
        // DebugAnnotation -> proto_value -> DebugAnnotation cycles do not
        // grow the C++ stack. DebugAnnotation entries use their own naming
        // via the "name" field, so the field name we just appended is
        // dropped before the DA item enters the dictionary.
        field_key_context.RemoveFieldSuffix();
        return PushDebugAnnotation(field.as_bytes(), delegate);
      }
      auto desc_idx = pool_.FindDescriptorIdx(resolved);
      if (!desc_idx) {
        return base::ErrStatus("Failed to find proto descriptor for %s",
                               resolved.c_str());
      }
      work_stack_.emplace_back(
          WorkItem{field.as_bytes(),
                   protozero::ProtoDecoder(field.as_bytes()),
                   &pool_.descriptors()[*desc_idx],
                   {},
                   {},
                   std::move(field_key_context),
                   true});
      return base::OkStatus();
    }
    return ParseSimpleField(*field_descriptor, field, delegate);
  }

  if (add_defaults_) {
    for (const auto& [id, field_desc] : item.descriptor->fields()) {
      if (work_stack_.size() == 1 &&
          !IsFieldAllowed(field_desc, allowed_fields_)) {
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
  done = true;
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

// ===========================================================================
// DebugAnnotation parsing.
//
// All work items below ride the same ProtoToArgsParser::work_stack_ as
// ProtoMessage WorkItems, so DebugAnnotation -> proto_value ->
// DebugAnnotation cycles (and chains via arbitrary intermediate protos) are
// processed iteratively without growing the C++ stack.
// ===========================================================================

namespace {

std::string SanitizeDebugAnnotationName(std::string_view raw_name) {
  std::string result(raw_name);
  std::replace(result.begin(), result.end(), '.', '_');
  std::replace(result.begin(), result.end(), '[', '_');
  std::replace(result.begin(), result.end(), ']', '_');
  return result;
}

base::Status ParseDebugAnnotationName(
    protos::pbzero::DebugAnnotation::Decoder& annotation,
    ProtoToArgsParser::Delegate& delegate,
    std::string& result) {
  uint64_t name_iid = annotation.name_iid();
  if (PERFETTO_LIKELY(name_iid)) {
    auto* decoder = delegate.GetInternedMessage(
        protos::pbzero::InternedData::kDebugAnnotationNames, name_iid);
    if (!decoder)
      return base::ErrStatus("Debug annotation with invalid name_iid");

    result = SanitizeDebugAnnotationName(decoder->name().ToStdStringView());
  } else if (annotation.has_name()) {
    result = SanitizeDebugAnnotationName(annotation.name().ToStdStringView());
  } else {
    return base::ErrStatus("Debug annotation without name");
  }
  return base::OkStatus();
}

}  // namespace

base::Status ProtoToArgsParser::ParseDebugAnnotation(protozero::ConstBytes data,
                                                     Delegate& delegate) {
  PERFETTO_DCHECK(work_stack_.empty());
  RETURN_IF_ERROR(PushDebugAnnotation(data, delegate));
  return RunWorkLoop(delegate);
}

base::Status ProtoToArgsParser::PushDebugAnnotation(protozero::ConstBytes data,
                                                    Delegate& delegate) {
  protos::pbzero::DebugAnnotation::Decoder decoder(data);
  std::string name;
  RETURN_IF_ERROR(ParseDebugAnnotationName(decoder, delegate, name));
  work_stack_.emplace_back(
      DebugAnnotationWorkItem{std::move(decoder), EnterDictionary(name)});
  return base::OkStatus();
}

base::Status ProtoToArgsParser::StepDebugAnnotation(
    DebugAnnotationWorkItem& item,
    Delegate& delegate,
    bool& done) {
  if (item.decoder.has_dict_entries()) {
    if (!item.first_pass_done) {
      item.nested_current = static_cast<uint32_t>(da_nested_storage_.size());
      uint32_t count = 0;
      for (auto res = item.decoder.dict_entries(); res; ++res, ++count) {
        da_nested_storage_.emplace_back(*res);
      }
      item.nested_count = count;
      item.first_pass_done = true;
    }
    if (item.nested_current < da_nested_storage_.size()) {
      protos::pbzero::DebugAnnotation::Decoder key_value(
          da_nested_storage_[item.nested_current++]);
      std::string key_name;
      RETURN_IF_ERROR(ParseDebugAnnotationName(key_value, delegate, key_name));
      work_stack_.emplace_back(DebugAnnotationWorkItem{
          std::move(key_value), EnterDictionary(key_name)});
      return base::OkStatus();
    }
    for (uint32_t i = 0; i < item.nested_count; ++i) {
      da_nested_storage_.pop_back();
    }
    item.added_entry = true;
  } else if (item.decoder.has_array_values()) {
    if (!item.first_pass_done) {
      item.nested_current = static_cast<uint32_t>(da_nested_storage_.size());
      uint32_t count = 0;
      for (auto res = item.decoder.array_values(); res; ++res, ++count) {
        da_nested_storage_.emplace_back(*res);
      }
      item.nested_count = count;
      item.array_index = delegate.GetArrayEntryIndex(item.key.key().key);
      item.first_pass_done = true;
    }
    if (item.nested_current < da_nested_storage_.size()) {
      work_stack_.emplace_back(DebugAnnotationWorkItem{
          protos::pbzero::DebugAnnotation::Decoder(
              da_nested_storage_[item.nested_current++]),
          EnterArray(*item.array_index)});
      return base::OkStatus();
    }
    for (uint32_t i = 0; i < item.nested_count; ++i) {
      da_nested_storage_.pop_back();
    }
  } else if (item.decoder.has_bool_value()) {
    delegate.AddBoolean(item.key.key(), item.decoder.bool_value());
    item.added_entry = true;
  } else if (item.decoder.has_uint_value()) {
    delegate.AddUnsignedInteger(item.key.key(), item.decoder.uint_value());
    item.added_entry = true;
  } else if (item.decoder.has_int_value()) {
    delegate.AddInteger(item.key.key(), item.decoder.int_value());
    item.added_entry = true;
  } else if (item.decoder.has_double_value()) {
    delegate.AddDouble(item.key.key(), item.decoder.double_value());
    item.added_entry = true;
  } else if (item.decoder.has_string_value()) {
    delegate.AddString(item.key.key(), item.decoder.string_value());
    item.added_entry = true;
  } else if (item.decoder.has_string_value_iid()) {
    auto* str_decoder = delegate.GetInternedMessage(
        protos::pbzero::InternedData::kDebugAnnotationStringValues,
        item.decoder.string_value_iid());
    if (!str_decoder) {
      return base::ErrStatus("Debug annotation with invalid string_value_iid");
    }
    delegate.AddString(item.key.key(), str_decoder->str().ToStdString());
    item.added_entry = true;
  } else if (item.decoder.has_pointer_value()) {
    delegate.AddPointer(item.key.key(), item.decoder.pointer_value());
    item.added_entry = true;
  } else if (item.decoder.has_legacy_json_value()) {
    delegate.AddJson(item.key.key(), item.decoder.legacy_json_value());
    item.added_entry = true;
  } else if (item.decoder.has_proto_value() && !item.subtree_pushed) {
    std::string type_name;
    if (item.decoder.has_proto_type_name()) {
      type_name = item.decoder.proto_type_name().ToStdString();
    } else if (item.decoder.has_proto_type_name_iid()) {
      auto* interned_name = delegate.GetInternedMessage(
          protos::pbzero::InternedData::kDebugAnnotationValueTypeNames,
          item.decoder.proto_type_name_iid());
      if (!interned_name) {
        return base::ErrStatus("Interned proto type name not found");
      }
      type_name = interned_name->name().ToStdString();
    } else {
      return base::ErrStatus(
          "DebugAnnotation has proto_value, but doesn't have proto type name");
    }
    item.added_entry = true;
    item.subtree_pushed = true;

    // Push the proto sub-message onto the same work stack; the loop will
    // pick it up next iteration. A DebugAnnotation type_name pushes a
    // DebugAnnotationWorkItem; any other type pushes a ProtoMessage WorkItem.
    protozero::ConstBytes proto_bytes = item.decoder.proto_value();
    if (type_name == kDebugAnnotationTypeName) {
      return PushDebugAnnotation(proto_bytes, delegate);
    }
    auto desc_idx = pool_.FindDescriptorIdx(type_name);
    if (!desc_idx) {
      return base::Status("Failed to find proto descriptor for " + type_name);
    }
    work_stack_.emplace_back(WorkItem{proto_bytes,
                                      protozero::ProtoDecoder(proto_bytes),
                                      &pool_.descriptors()[*desc_idx],
                                      {},
                                      {},
                                      ScopedNestedKeyContext(key_prefix_),
                                      true});
    return base::OkStatus();
  } else if (item.decoder.has_nested_value() && !item.subtree_pushed) {
    item.subtree_pushed = true;
    // The pushed NestedValueWorkItem has no own key: it borrows the parent
    // DebugAnnotation's key by looking it up at `self_index - 1` in the work
    // stack during StepNestedValue. Storing a pointer to `item.key` here
    // would dangle once `emplace_back` reallocates `work_stack_`.
    work_stack_.emplace_back(NestedValueWorkItem{
        protos::pbzero::DebugAnnotation::NestedValue::Decoder(
            item.decoder.nested_value())});
    return base::OkStatus();
  }

  // Restore the key prefix to the parent's state before reading the parent
  // key for IncrementArrayEntryIndex; otherwise the parent key would still
  // carry this item's suffix.
  bool just_added_entry = item.added_entry;
  item.key.RemoveFieldSuffix();
  done = true;
  if (work_stack_.size() >= 2) {
    if (auto* parent_da = std::get_if<DebugAnnotationWorkItem>(
            &work_stack_[work_stack_.size() - 2])) {
      if (just_added_entry && parent_da->array_index) {
        parent_da->array_index =
            delegate.IncrementArrayEntryIndex(parent_da->key.key().key);
      }
      parent_da->added_entry = parent_da->added_entry || just_added_entry;
    }
  }
  return base::OkStatus();
}

const ProtoToArgsParser::Key& ProtoToArgsParser::EffectiveNestedValueKey(
    size_t nv_index) const {
  PERFETTO_DCHECK(nv_index < work_stack_.size());
  const auto& nv = std::get<NestedValueWorkItem>(work_stack_[nv_index]);
  if (nv.key) {
    return nv.key->key();
  }
  PERFETTO_DCHECK(nv_index > 0);
  PERFETTO_DCHECK(std::holds_alternative<DebugAnnotationWorkItem>(
      work_stack_[nv_index - 1]));
  return std::get<DebugAnnotationWorkItem>(work_stack_[nv_index - 1]).key.key();
}

void ProtoToArgsParser::PropagateNestedValueResult(size_t child_index,
                                                   bool added_entry,
                                                   Delegate& delegate) {
  if (child_index == 0) {
    return;
  }
  auto& parent = work_stack_[child_index - 1];
  if (auto* parent_nv = std::get_if<NestedValueWorkItem>(&parent)) {
    if (added_entry && parent_nv->array_index) {
      const Key& pk = EffectiveNestedValueKey(child_index - 1);
      parent_nv->array_index = delegate.IncrementArrayEntryIndex(pk.key);
    }
    parent_nv->added_entry = parent_nv->added_entry || added_entry;
  } else if (auto* parent_da = std::get_if<DebugAnnotationWorkItem>(&parent)) {
    // Root NestedValue popping back to the DebugAnnotation that pushed it.
    parent_da->added_entry = parent_da->added_entry || added_entry;
  }
}

base::Status ProtoToArgsParser::StepNestedValue(NestedValueWorkItem& item,
                                                size_t self_index,
                                                Delegate& delegate,
                                                bool& done) {
  using NV = protos::pbzero::DebugAnnotation::NestedValue;
  const Key& key = EffectiveNestedValueKey(self_index);

  switch (item.decoder.nested_type()) {
    case NV::UNSPECIFIED: {
      if (item.decoder.has_bool_value()) {
        delegate.AddBoolean(key, item.decoder.bool_value());
        item.added_entry = true;
      } else if (item.decoder.has_int_value()) {
        delegate.AddInteger(key, item.decoder.int_value());
        item.added_entry = true;
      } else if (item.decoder.has_double_value()) {
        delegate.AddDouble(key, item.decoder.double_value());
        item.added_entry = true;
      } else if (item.decoder.has_string_value()) {
        delegate.AddString(key, item.decoder.string_value());
        item.added_entry = true;
      }
      break;
    }
    case NV::DICT: {
      if (!item.first_pass_done) {
        item.nested_current = static_cast<uint32_t>(nv_nested_storage_.size());
        uint32_t count = 0;
        auto keys = item.decoder.dict_keys();
        auto values = item.decoder.dict_values();
        // Gate on both iterators: a malformed NestedValue with mismatched
        // dict_keys / dict_values would otherwise advance the shorter one
        // past end_, triggering an unbounded scan in the next dereference.
        for (; keys && values; ++keys, ++values, ++count) {
          protozero::ConstChars k = *keys;
          nv_nested_storage_.push_back(
              {SanitizeDebugAnnotationName(k.ToStdStringView()), *values});
        }
        if (keys || values) {
          return base::ErrStatus(
              "Nested debug annotation DICT has mismatched dict_keys and "
              "dict_values counts");
        }
        item.nested_count = count;
        item.first_pass_done = true;
      }
      if (item.nested_current < nv_nested_storage_.size()) {
        const auto& nested_item = nv_nested_storage_[item.nested_current++];
        NestedValueWorkItem child{NV::Decoder(nested_item.nested_value)};
        child.key = EnterDictionary(nested_item.key);
        work_stack_.emplace_back(std::move(child));
        return base::OkStatus();
      }
      for (uint32_t i = 0; i < item.nested_count; ++i) {
        nv_nested_storage_.pop_back();
      }
      item.added_entry = true;
      break;
    }
    case NV::ARRAY: {
      if (!item.first_pass_done) {
        item.nested_current = static_cast<uint32_t>(nv_nested_storage_.size());
        uint32_t count = 0;
        for (auto res = item.decoder.array_values(); res; ++res, ++count) {
          nv_nested_storage_.push_back({"", *res});
        }
        item.nested_count = count;
        item.array_index = delegate.GetArrayEntryIndex(key.key);
        item.first_pass_done = true;
      }
      if (item.nested_current < nv_nested_storage_.size()) {
        NestedValueWorkItem child{NV::Decoder(
            nv_nested_storage_[item.nested_current++].nested_value)};
        child.key = EnterArray(*item.array_index);
        work_stack_.emplace_back(std::move(child));
        return base::OkStatus();
      }
      for (uint32_t i = 0; i < item.nested_count; ++i) {
        nv_nested_storage_.pop_back();
      }
      break;
    }
  }

  bool just_added_entry = item.added_entry;
  if (item.key) {
    item.key->RemoveFieldSuffix();
  }
  done = true;
  PropagateNestedValueResult(self_index, just_added_entry, delegate);
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::util
