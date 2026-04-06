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
#include <optional>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/field.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"

namespace perfetto::trace_processor::util {

namespace {

std::string SanitizeDebugAnnotationName(std::string_view raw_name) {
  std::string result(raw_name);
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

    result = SanitizeDebugAnnotationName(decoder->name().ToStdStringView());
  } else if (annotation.has_name()) {
    result = SanitizeDebugAnnotationName(annotation.name().ToStdStringView());
  } else {
    return base::ErrStatus("Debug annotation without name");
  }
  return base::OkStatus();
}

// static
base::Status DebugAnnotationParser::Parse(
    protozero::ConstBytes data,
    ProtoToArgsParser::Delegate& delegate) {
  // This function parses a tree of debug annotations iteratively using a work
  // stack. A recursive approach is not used to avoid stack overflows when
  // parsing deeply nested annotations.
  struct WorkItem {
    // The decoder for the current annotation node.
    protos::pbzero::DebugAnnotation::Decoder decoder;
    // The key context for the current annotation node.
    ProtoToArgsParser::ScopedNestedKeyContext key;
    // The index of the current array entry.
    std::optional<size_t> array_index = {};
    // The number of entries in the current array or dictionary.
    uint32_t nested_count = 0;
    // The current position in the array or dictionary.
    uint32_t nested_current = 0;
    // Whether an entry has been added to the delegate.
    bool added_entry = false;
    // Whether this is the first pass through the dictionary or array.
    bool first_pass_done = false;
  };

  // The work stack for the iterative parsing.
  base::SmallVector<WorkItem, 4> work_stack;
  base::SmallVector<protozero::ConstBytes, 64> nested_storage;
  {
    protos::pbzero::DebugAnnotation::Decoder root(data);
    std::string name;
    RETURN_IF_ERROR(ParseDebugAnnotationName(root, delegate, name));
    work_stack.emplace_back(
        WorkItem{std::move(root), proto_to_args_parser_.EnterDictionary(name)});
  }
  while (!work_stack.empty()) {
    WorkItem& item = work_stack.back();

    // Dictionaries are parsed by adding each entry to the work stack.
    if (item.decoder.has_dict_entries()) {
      // First time seeing this dictionary, add them to the nested storage.
      if (!item.first_pass_done) {
        item.nested_current = static_cast<uint32_t>(nested_storage.size());
        uint32_t count = 0;
        for (auto res = item.decoder.dict_entries(); res; ++res, ++count) {
          nested_storage.emplace_back(*res);
        }
        item.nested_count = count;
        item.first_pass_done = true;
      }
      // If there are remaining entries, pop one and add it to the work stack.
      if (item.nested_current < nested_storage.size()) {
        protos::pbzero::DebugAnnotation::Decoder key_value(
            nested_storage[item.nested_current++]);
        std::string key;
        RETURN_IF_ERROR(ParseDebugAnnotationName(key_value, delegate, key));
        work_stack.emplace_back(WorkItem{
            std::move(key_value),
            proto_to_args_parser_.EnterDictionary(key),
        });
        continue;
      }
      // If we are here, we have processed all entries in the dictionary.
      for (uint32_t i = 0; i < item.nested_count; ++i) {
        nested_storage.pop_back();
      }
      item.added_entry = true;
    } else if (item.decoder.has_array_values()) {
      // First time seeing this array, add them to the nested storage.
      if (!item.first_pass_done) {
        item.nested_current = static_cast<uint32_t>(nested_storage.size());
        uint32_t count = 0;
        for (auto res = item.decoder.array_values(); res; ++res, ++count) {
          nested_storage.emplace_back(*res);
        }
        item.nested_count = count;
        item.array_index = delegate.GetArrayEntryIndex(item.key.key().key);
        item.first_pass_done = true;
      }
      // If there are remaining array, pop one and add it to the work stack.
      if (item.nested_current < nested_storage.size()) {
        work_stack.emplace_back(WorkItem{
            protos::pbzero::DebugAnnotation::Decoder(
                nested_storage[item.nested_current++]),
            proto_to_args_parser_.EnterArray(*item.array_index),
        });
        continue;
      }
      // If we are here, we have processed all entries in the dictionary.
      for (uint32_t i = 0; i < item.nested_count; ++i) {
        nested_storage.pop_back();
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
      auto* decoder = delegate.GetInternedMessage(
          protos::pbzero::InternedData::kDebugAnnotationStringValues,
          item.decoder.string_value_iid());
      if (!decoder) {
        return base::ErrStatus(
            "Debug annotation with invalid string_value_iid");
      }
      delegate.AddString(item.key.key(), decoder->str().ToStdString());
      item.added_entry = true;
    } else if (item.decoder.has_pointer_value()) {
      delegate.AddPointer(item.key.key(), item.decoder.pointer_value());
      item.added_entry = true;
    } else if (item.decoder.has_legacy_json_value()) {
      delegate.AddJson(item.key.key(), item.decoder.legacy_json_value());
      item.added_entry = true;
    } else if (item.decoder.has_proto_value()) {
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
            "DebugAnnotation has proto_value, but doesn't "
            "have proto type name");
      }
      RETURN_IF_ERROR(proto_to_args_parser_.ParseMessage(
          item.decoder.proto_value(), type_name, nullptr, delegate));
      item.added_entry = true;
    } else if (item.decoder.has_nested_value()) {
      auto res = ParseNestedValueArgs(item.decoder.nested_value(),
                                      item.key.key(), delegate);
      RETURN_IF_ERROR(res.status);
      item.added_entry = res.added_entry;
    }
    // We are done with this item, pop it from the stack.
    bool added_entry = item.added_entry;
    work_stack.pop_back();
    if (!work_stack.empty()) {
      auto& back = work_stack.back();
      if (added_entry && back.array_index) {
        back.array_index =
            delegate.IncrementArrayEntryIndex(back.key.key().key);
      }
      back.added_entry = back.added_entry || added_entry;
    }
  }
  return base::OkStatus();
}

DebugAnnotationParser::ParseResult DebugAnnotationParser::ParseNestedValueArgs(
    protozero::ConstBytes nested_value,
    const ProtoToArgsParser::Key& context_name,
    ProtoToArgsParser::Delegate& delegate) {
  // This function parses a tree of nested debug annotations iteratively using a
  // work stack. A recursive approach is not used to avoid stack overflows when
  // parsing deeply nested annotations.
  struct WorkItem {
    // The decoder for the current annotation node.
    protos::pbzero::DebugAnnotation::NestedValue::Decoder decoder;
    // The key context for the current annotation node.
    std::optional<ProtoToArgsParser::ScopedNestedKeyContext> key;
    // The index of the current array entry.
    std::optional<size_t> array_index = 0;
    // The number of entries in the current array or dictionary.
    uint32_t nested_count = 0;
    // The current position in the array or dictionary.
    uint32_t nested_current = 0;
    // Whether an entry has been added to the delegate.
    bool added_entry = false;
    // Whether this is the first pass through the dictionary or array.
    bool first_pass_done = false;
  };
  struct NestedItem {
    std::string key;
    protozero::ConstBytes nested_value;
  };

  auto key = [&](const WorkItem& item) -> const ProtoToArgsParser::Key& {
    if (item.key) {
      return item.key->key();
    }
    return context_name;
  };

  // The work stack for the iterative parsing.
  base::SmallVector<WorkItem, 4> work_stack;
  base::SmallVector<NestedItem, 64> nested_storage;
  bool added_entry = false;
  work_stack.emplace_back(WorkItem{
      protos::pbzero::DebugAnnotation::NestedValue::Decoder(nested_value),
      std::nullopt,
  });

  while (!work_stack.empty()) {
    WorkItem& item = work_stack.back();

    switch (item.decoder.nested_type()) {
      case protos::pbzero::DebugAnnotation::NestedValue::UNSPECIFIED: {
        // Leaf value.
        if (item.decoder.has_bool_value()) {
          delegate.AddBoolean(key(item), item.decoder.bool_value());
          item.added_entry = true;
        } else if (item.decoder.has_int_value()) {
          delegate.AddInteger(key(item), item.decoder.int_value());
          item.added_entry = true;
        } else if (item.decoder.has_double_value()) {
          delegate.AddDouble(key(item), item.decoder.double_value());
          item.added_entry = true;
        } else if (item.decoder.has_string_value()) {
          delegate.AddString(key(item), item.decoder.string_value());
          item.added_entry = true;
        }
        break;
      }
      case protos::pbzero::DebugAnnotation::NestedValue::DICT: {
        // First time seeing this dictionary, add them to the nested storage.
        if (!item.first_pass_done) {
          item.nested_current = static_cast<uint32_t>(nested_storage.size());
          uint32_t count = 0;
          auto keys = item.decoder.dict_keys();
          auto values = item.decoder.dict_values();
          for (; keys; ++keys, ++values, ++count) {
            PERFETTO_DCHECK(values);
            protozero::ConstChars k = *keys;
            nested_storage.emplace_back(NestedItem{
                SanitizeDebugAnnotationName(k.ToStdStringView()),
                *values,
            });
          }
          item.nested_count = count;
          item.first_pass_done = true;
        }
        // If there are remaining entries, pop one and add it to the work stack.
        if (item.nested_current < nested_storage.size()) {
          const auto& nested_item = nested_storage[item.nested_current++];
          work_stack.emplace_back(WorkItem{
              protos::pbzero::DebugAnnotation::NestedValue::Decoder(
                  nested_item.nested_value),
              proto_to_args_parser_.EnterDictionary(nested_item.key),
          });
          continue;
        }
        // If we are here, we have processed all entries in the dictionary.
        for (uint32_t i = 0; i < item.nested_count; ++i) {
          nested_storage.pop_back();
        }
        item.added_entry = true;
        break;
      }
      case protos::pbzero::DebugAnnotation::NestedValue::ARRAY: {
        // First time seeing this array, add them to the nested storage.
        if (!item.first_pass_done) {
          item.nested_current = static_cast<uint32_t>(nested_storage.size());
          uint32_t count = 0;
          for (auto res = item.decoder.array_values(); res; ++res, ++count) {
            nested_storage.emplace_back(NestedItem{"", *res});
          }
          item.nested_count = count;
          item.array_index = delegate.GetArrayEntryIndex(key(item).key);
          item.first_pass_done = true;
        }
        // If there are remaining array, pop one and add it to the work stack.
        if (item.nested_current < nested_storage.size()) {
          work_stack.emplace_back(WorkItem{
              protos::pbzero::DebugAnnotation::NestedValue::Decoder(
                  nested_storage[item.nested_current++].nested_value),
              proto_to_args_parser_.EnterArray(*item.array_index),
          });
          continue;
        }
        // If we are here, we have processed all entries in the dictionary.
        for (uint32_t i = 0; i < item.nested_count; ++i) {
          nested_storage.pop_back();
        }
        break;
      }
    }

    // We are done with this item, pop it from the stack.
    bool just_added_entry = item.added_entry;
    added_entry = added_entry || just_added_entry;
    work_stack.pop_back();
    if (!work_stack.empty()) {
      auto& back = work_stack.back();
      if (just_added_entry && back.array_index) {
        back.array_index = delegate.IncrementArrayEntryIndex(key(back).key);
      }
      back.added_entry = back.added_entry || just_added_entry;
    }
  }
  return {base::OkStatus(), added_entry};
}

}  // namespace perfetto::trace_processor::util
