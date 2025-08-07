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

#include "src/trace_processor/util/debug_annotation_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/status.h"
#include "perfetto/protozero/field.h"
#include "perfetto/public/compiler.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"

#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor::util {

namespace {

std::string SanitizeDebugAnnotationName(const std::string& raw_name) {
  std::string result = raw_name;
  std::replace(result.begin(), result.end(), '.', '_');
  std::replace(result.begin(), result.end(), '[', '_');
  std::replace(result.begin(), result.end(), ']', '_');
  return result;
}

}  // namespace

DebugAnnotationParser::DebugAnnotationParser(ProtoToArgsParser& parser)
    : proto_to_args_parser_(parser) {}

base::Status DebugAnnotationParser::ParseDebugAnnotationName(
    protos::pbzero::DebugAnnotation::Decoder& annotation,
    ProtoToArgsParser::Delegate& delegate,
    std::string& result) {
  uint64_t name_iid = annotation.name_iid();
  if (PERFETTO_LIKELY(name_iid)) {
    auto* decoder = delegate.GetInternedMessage(
        protos::pbzero::InternedData::kDebugAnnotationNames, name_iid);
    if (!decoder)
      return base::ErrStatus("Debug annotation with invalid name_iid");

    result = SanitizeDebugAnnotationName(decoder->name().ToStdString());
  } else if (annotation.has_name()) {
    result = SanitizeDebugAnnotationName(annotation.name().ToStdString());
  } else {
    return base::ErrStatus("Debug annotation without name");
  }
  return base::OkStatus();
}

DebugAnnotationParser::ParseResult
DebugAnnotationParser::ParseDebugAnnotationValue(
    protos::pbzero::DebugAnnotation::Decoder& annotation,
    ProtoToArgsParser::Delegate& delegate,
    const ProtoToArgsParser::Key& context_name) {
  if (annotation.has_bool_value()) {
    delegate.AddBoolean(context_name, annotation.bool_value());
  } else if (annotation.has_uint_value()) {
    delegate.AddUnsignedInteger(context_name, annotation.uint_value());
  } else if (annotation.has_int_value()) {
    delegate.AddInteger(context_name, annotation.int_value());
  } else if (annotation.has_double_value()) {
    delegate.AddDouble(context_name, annotation.double_value());
  } else if (annotation.has_string_value()) {
    delegate.AddString(context_name, annotation.string_value());
  } else if (annotation.has_string_value_iid()) {
    auto* decoder = delegate.GetInternedMessage(
        protos::pbzero::InternedData::kDebugAnnotationStringValues,
        annotation.string_value_iid());
    if (!decoder) {
      return {base::ErrStatus("Debug annotation with invalid string_value_iid"),
              false};
    }
    delegate.AddString(context_name, decoder->str().ToStdString());
  } else if (annotation.has_pointer_value()) {
    delegate.AddPointer(context_name, annotation.pointer_value());
  } else if (annotation.has_dict_entries()) {
    bool added_entry = false;
    for (auto it = annotation.dict_entries(); it; ++it) {
      protos::pbzero::DebugAnnotation::Decoder key_value(*it);
      std::string key;
      base::Status key_parse_result =
          ParseDebugAnnotationName(key_value, delegate, key);
      if (!key_parse_result.ok())
        return {key_parse_result, added_entry};

      auto nested_key = proto_to_args_parser_.EnterDictionary(key);
      ParseResult value_parse_result =
          ParseDebugAnnotationValue(key_value, delegate, nested_key.key());
      added_entry |= value_parse_result.added_entry;
      if (!value_parse_result.status.ok())
        return {value_parse_result.status, added_entry};
    }
  } else if (annotation.has_array_values()) {
    size_t index = delegate.GetArrayEntryIndex(context_name.key);
    bool added_entry = false;
    for (auto it = annotation.array_values(); it; ++it) {
      std::string array_key = context_name.key;
      protos::pbzero::DebugAnnotation::Decoder value(*it);

      auto nested_key = proto_to_args_parser_.EnterArray(index);
      ParseResult value_parse_result =
          ParseDebugAnnotationValue(value, delegate, nested_key.key());

      if (value_parse_result.added_entry) {
        index = delegate.IncrementArrayEntryIndex(array_key);
        added_entry = true;
      }
      if (!value_parse_result.status.ok())
        return {value_parse_result.status, added_entry};
    }
  } else if (annotation.has_legacy_json_value()) {
    bool added_entry =
        delegate.AddJson(context_name, annotation.legacy_json_value());
    return {base::ErrStatus("Failed to parse JSON"), added_entry};
  } else if (annotation.has_nested_value()) {
    return ParseNestedValueArgs(annotation.nested_value(), context_name,
                                delegate);
  } else if (annotation.has_proto_value()) {
    std::string type_name;
    if (annotation.has_proto_type_name()) {
      type_name = annotation.proto_type_name().ToStdString();
    } else if (annotation.has_proto_type_name_iid()) {
      auto* interned_name = delegate.GetInternedMessage(
          protos::pbzero::InternedData::kDebugAnnotationValueTypeNames,
          annotation.proto_type_name_iid());
      if (!interned_name)
        return {base::ErrStatus("Interned proto type name not found"), false};
      type_name = interned_name->name().ToStdString();
    } else {
      return {base::ErrStatus("DebugAnnotation has proto_value, but doesn't "
                              "have proto type name"),
              false};
    }
    return {proto_to_args_parser_.ParseMessage(annotation.proto_value(),
                                               type_name, nullptr, delegate),
            true};
  } else {
    return {base::OkStatus(), /*added_entry=*/false};
  }

  return {base::OkStatus(), /*added_entry=*/true};
}

// static
base::Status DebugAnnotationParser::Parse(
    protozero::ConstBytes data,
    ProtoToArgsParser::Delegate& delegate) {
  protos::pbzero::DebugAnnotation::Decoder annotation(data);

  std::string name;
  base::Status name_parse_result =
      ParseDebugAnnotationName(annotation, delegate, name);
  if (!name_parse_result.ok())
    return name_parse_result;

  auto context = proto_to_args_parser_.EnterDictionary(name);

  return ParseDebugAnnotationValue(annotation, delegate, context.key()).status;
}

DebugAnnotationParser::ParseResult DebugAnnotationParser::ParseNestedValueArgs(
    protozero::ConstBytes nested_value,
    const ProtoToArgsParser::Key& context_name,
    ProtoToArgsParser::Delegate& delegate) {
  protos::pbzero::DebugAnnotation::NestedValue::Decoder value(nested_value);
  switch (value.nested_type()) {
    case protos::pbzero::DebugAnnotation::NestedValue::UNSPECIFIED: {
      // Leaf value.
      if (value.has_bool_value()) {
        delegate.AddBoolean(context_name, value.bool_value());
        return {base::OkStatus(), true};
      }
      if (value.has_int_value()) {
        delegate.AddInteger(context_name, value.int_value());
        return {base::OkStatus(), true};
      }
      if (value.has_double_value()) {
        delegate.AddDouble(context_name, value.double_value());
        return {base::OkStatus(), true};
      }
      if (value.has_string_value()) {
        delegate.AddString(context_name, value.string_value());
        return {base::OkStatus(), true};
      }
      return {base::OkStatus(), false};
    }
    case protos::pbzero::DebugAnnotation::NestedValue::DICT: {
      bool added_entry = false;
      auto key_it = value.dict_keys();
      auto value_it = value.dict_values();
      for (; key_it && value_it; ++key_it, ++value_it) {
        std::string child_name =
            SanitizeDebugAnnotationName((*key_it).ToStdString());
        auto nested_key = proto_to_args_parser_.EnterDictionary(child_name);
        ParseResult result =
            ParseNestedValueArgs(*value_it, nested_key.key(), delegate);
        added_entry |= result.added_entry;
        if (!result.status.ok())
          return {result.status, added_entry};
      }
      return {base::OkStatus(), true};
    }

    case protos::pbzero::DebugAnnotation::NestedValue::ARRAY: {
      std::string array_key = context_name.key;
      size_t array_index = delegate.GetArrayEntryIndex(context_name.key);
      bool added_entry = false;

      for (auto value_it = value.array_values(); value_it; ++value_it) {
        auto nested_key = proto_to_args_parser_.EnterArray(array_index);
        ParseResult result =
            ParseNestedValueArgs(*value_it, nested_key.key(), delegate);

        if (result.added_entry) {
          ++array_index;
          delegate.IncrementArrayEntryIndex(array_key);
          added_entry = true;
        }
        if (!result.status.ok())
          return {result.status, added_entry};
      }
      return {base::OkStatus(), added_entry};
    }
  }
  return {base::OkStatus(), false};
}

}  // namespace perfetto::trace_processor::util
