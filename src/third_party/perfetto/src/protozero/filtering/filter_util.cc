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

#include "src/protozero/filtering/filter_util.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <set>

#include <google/protobuf/compiler/importer.h>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/getopt.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/protozero/filtering/filter_bytecode_generator.h"
#include "src/protozero/filtering/filter_bytecode_parser.h"

namespace protozero {

namespace {

class MultiFileErrorCollectorImpl
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  ~MultiFileErrorCollectorImpl() override;
  void AddError(const std::string&, int, int, const std::string&) override;
  void AddWarning(const std::string&, int, int, const std::string&) override;
};

MultiFileErrorCollectorImpl::~MultiFileErrorCollectorImpl() = default;

void MultiFileErrorCollectorImpl::AddError(const std::string& filename,
                                           int line,
                                           int column,
                                           const std::string& message) {
  PERFETTO_ELOG("Error %s %d:%d: %s", filename.c_str(), line, column,
                message.c_str());
}

void MultiFileErrorCollectorImpl::AddWarning(const std::string& filename,
                                             int line,
                                             int column,
                                             const std::string& message) {
  PERFETTO_ELOG("Warning %s %d:%d: %s", filename.c_str(), line, column,
                message.c_str());
}

}  // namespace

FilterUtil::FilterUtil() = default;
FilterUtil::~FilterUtil() = default;

bool FilterUtil::LoadMessageDefinition(
    const std::string& proto_file,
    const std::string& root_message,
    const std::string& proto_dir_path,
    const std::set<std::string>& passthrough_fields,
    const std::set<std::string>& string_filter_fields) {
  passthrough_fields_ = passthrough_fields;
  passthrough_fields_seen_.clear();
  filter_string_fields_ = string_filter_fields;
  filter_string_fields_seen_.clear();

  // The protobuf compiler doesn't like backslashes and prints an error like:
  // Error C:\it7mjanpw3\perfetto-a16500 -1:0: Backslashes, consecutive slashes,
  // ".", or ".." are not allowed in the virtual path.
  // Given that C:\foo\bar is a legit path on windows, fix it at this level
  // because the problem is really the protobuf compiler being too picky.
  static auto normalize_for_win = [](const std::string& path) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    return perfetto::base::ReplaceAll(path, "\\", "/");
#else
    return path;
#endif
  };

  google::protobuf::compiler::DiskSourceTree dst;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // If the path is absolute, maps "C:/" -> "C:/" (without hardcoding 'C').
  if (proto_file.size() > 3 && proto_file[1] == ':') {
    char win_drive[4]{proto_file[0], ':', '/', '\0'};
    dst.MapPath(win_drive, win_drive);
  }
#endif
  dst.MapPath("/", "/");  // We might still need this on Win under cygwin.
  dst.MapPath("", normalize_for_win(proto_dir_path));
  MultiFileErrorCollectorImpl mfe;
  google::protobuf::compiler::Importer importer(&dst, &mfe);
  const google::protobuf::FileDescriptor* root_file =
      importer.Import(normalize_for_win(proto_file));
  const google::protobuf::Descriptor* root_msg = nullptr;
  if (!root_message.empty()) {
    root_msg = importer.pool()->FindMessageTypeByName(root_message);
  } else if (root_file->message_type_count() > 0) {
    // The user didn't specify the root type. Pick the first type in the file,
    // most times it's the right guess.
    root_msg = root_file->message_type(0);
    if (root_msg)
      PERFETTO_LOG(
          "The guessed root message name is \"%.*s\". Pass -r com.MyName to "
          "override",
          int(root_msg->full_name().size()), root_msg->full_name().data());
  }

  if (!root_msg) {
    PERFETTO_ELOG("Could not find the root message \"%s\" in %s",
                  root_message.c_str(), proto_file.c_str());
    return false;
  }

  // |descriptors_by_full_name| is passed by argument rather than being a member
  // field so that we don't risk leaving it out of sync (and depending on it in
  // future without realizing) when performing the Dedupe() pass.
  DescriptorsByNameMap descriptors_by_full_name;
  ParseProtoDescriptor(root_msg, &descriptors_by_full_name);

  // If the user specified a set of fields to pass through, print an error and
  // fail if any of the passed fields have not been seen while recursing in the
  // schema. This is to avoid typos or naming changes to be silently ignored.
  std::vector<std::string> unused;
  std::set_difference(passthrough_fields_.begin(), passthrough_fields_.end(),
                      passthrough_fields_seen_.begin(),
                      passthrough_fields_seen_.end(),
                      std::back_inserter(unused));
  for (const std::string& message_and_field : unused) {
    PERFETTO_ELOG("Field not found %s", message_and_field.c_str());
  }
  if (!unused.empty()) {
    PERFETTO_ELOG("Passthrough syntax: perfetto.protos.MessageName:field_name");
    return false;
  }
  std::set_difference(
      filter_string_fields_.begin(), filter_string_fields_.end(),
      filter_string_fields_seen_.begin(), filter_string_fields_seen_.end(),
      std::back_inserter(unused));
  for (const std::string& message_and_field : unused) {
    PERFETTO_ELOG("Field not found %s", message_and_field.c_str());
  }
  if (!unused.empty()) {
    PERFETTO_ELOG(
        "Filter string syntax: perfetto.protos.MessageName:field_name");
    return false;
  }
  return true;
}

// Generates a Message object for the given libprotobuf message descriptor.
// Recurses as needed into nested fields.
FilterUtil::Message* FilterUtil::ParseProtoDescriptor(
    const google::protobuf::Descriptor* proto,
    DescriptorsByNameMap* descriptors_by_full_name) {
  auto descr_it =
      descriptors_by_full_name->find(std::string(proto->full_name()));
  if (descr_it != descriptors_by_full_name->end())
    return descr_it->second;

  descriptors_.emplace_back();
  Message* msg = &descriptors_.back();
  msg->full_name = std::string(proto->full_name());
  (*descriptors_by_full_name)[msg->full_name] = msg;
  for (int i = 0; i < proto->field_count(); ++i) {
    const auto* proto_field = proto->field(i);
    const uint32_t field_id = static_cast<uint32_t>(proto_field->number());
    PERFETTO_CHECK(msg->fields.count(field_id) == 0);
    auto& field = msg->fields[field_id];
    field.name = proto_field->name();
    field.type = proto_field->type_name();

    std::string message_and_field = msg->full_name + ":" + field.name;
    bool passthrough = false;
    if (passthrough_fields_.count(message_and_field)) {
      field.type = "bytes";
      passthrough = true;
      passthrough_fields_seen_.insert(message_and_field);
    }
    if (filter_string_fields_.count(message_and_field)) {
      PERFETTO_CHECK(field.type == "string");
      field.filter_string = true;
      msg->has_filter_string_fields = true;
      filter_string_fields_seen_.insert(message_and_field);
    }
    if (proto_field->message_type() && !passthrough) {
      msg->has_nested_fields = true;
      // Recurse.
      field.nested_type = ParseProtoDescriptor(proto_field->message_type(),
                                               descriptors_by_full_name);
    }
  }
  return msg;
}

void FilterUtil::Dedupe() {
  std::map<std::string /*identity*/, Message*> index;

  std::map<Message*, Message*> dupe_graph;  // K,V: K shall be duped against V.

  // As a first pass, generate an |identity| string for each leaf message. The
  // identity is simply the comma-separated stringification of its field ids.
  // If another message with the same identity exists, add an edge to the graph.
  const size_t initial_count = descriptors_.size();
  size_t field_count = 0;
  for (auto& descr : descriptors_) {
    // Dedupe only leaf messages without nested or string filter fields.
    if (descr.has_nested_fields || descr.has_filter_string_fields)
      continue;
    std::string identity;
    for (const auto& id_and_field : descr.fields)
      identity.append(std::to_string(id_and_field.first) + ",");
    auto it_and_inserted = index.emplace(identity, &descr);
    if (!it_and_inserted.second) {
      // insertion failed, a dupe exists already.
      Message* dupe_against = it_and_inserted.first->second;
      dupe_graph.emplace(&descr, dupe_against);
    }
  }

  // Now apply de-duplications by re-directing the nested_type pointer to the
  // equivalent descriptors that have the same set of allowed field ids.
  std::set<Message*> referenced_descriptors;
  referenced_descriptors.emplace(&descriptors_.front());  // The root.
  for (auto& descr : descriptors_) {
    for (auto& id_and_field : descr.fields) {
      Message* target = id_and_field.second.nested_type;
      if (!target)
        continue;  // Only try to dedupe nested types.
      auto it = dupe_graph.find(target);
      if (it == dupe_graph.end()) {
        referenced_descriptors.emplace(target);
        continue;
      }
      ++field_count;
      // Replace with the dupe.
      id_and_field.second.nested_type = it->second;
    }  // for (nested_fields).
  }  // for (descriptors_).

  // Remove unreferenced descriptors. We should much rather crash in the case of
  // a logic bug rather than trying to use them but don't emit them.
  size_t removed_count = 0;
  for (auto it = descriptors_.begin(); it != descriptors_.end();) {
    if (referenced_descriptors.count(&*it)) {
      ++it;
    } else {
      ++removed_count;
      it = descriptors_.erase(it);
    }
  }
  PERFETTO_LOG(
      "Deduplication removed %zu duped descriptors out of %zu descriptors from "
      "%zu fields",
      removed_count, initial_count, field_count);
}

// Prints the list of messages and fields in a diff-friendly text format.
void FilterUtil::PrintAsText(std::optional<std::string> filter_bytecode) {
  using perfetto::base::StripPrefix;
  const std::string& root_name = descriptors_.front().full_name;
  std::string root_prefix = root_name.substr(0, root_name.rfind('.'));
  if (!root_prefix.empty())
    root_prefix.append(".");

  FilterBytecodeParser parser;
  if (filter_bytecode) {
    PERFETTO_CHECK(
        parser.Load(filter_bytecode->data(), filter_bytecode->size()));
  }

  // <Filter msg_index, Message>
  std::deque<std::pair<uint32_t, const Message*>> queue;
  std::set<const Message*> seen_msgs{&descriptors_.front()};
  queue.emplace_back(0u, &descriptors_.front());

  while (!queue.empty()) {
    auto index_and_descr = queue.front();
    queue.pop_front();
    uint32_t msg_index = index_and_descr.first;
    const auto& descr = *index_and_descr.second;

    for (const auto& id_and_field : descr.fields) {
      const uint32_t field_id = id_and_field.first;
      const auto& field = id_and_field.second;

      FilterBytecodeParser::QueryResult result{0, false};
      if (filter_bytecode) {
        result = parser.Query(msg_index, field_id);
        if (!result.allowed) {
          continue;
        }
      }

      const Message* nested_type = id_and_field.second.nested_type;
      bool passthrough = false;
      if (nested_type) {
        // result.simple_field might be true if the generated bytecode is
        // passing through a whole submessage without recursing.
        passthrough = result.allowed && result.simple_field();
        if (seen_msgs.find(nested_type) == seen_msgs.end()) {
          seen_msgs.insert(nested_type);
          queue.emplace_back(result.nested_msg_index, nested_type);
        }
      } else {  // simple field
        PERFETTO_CHECK(result.simple_field() || result.filter_string_field() ||
                       !filter_bytecode);
        PERFETTO_CHECK(result.filter_string_field() == field.filter_string ||
                       !filter_bytecode);
      }

      auto stripped_name = StripPrefix(descr.full_name, root_prefix);
      std::string stripped_nested =
          nested_type ? " " + StripPrefix(nested_type->full_name, root_prefix)
                      : "";
      if (passthrough)
        stripped_nested += "  # PASSTHROUGH";
      if (field.filter_string)
        stripped_nested += "  # FILTER STRING";
      fprintf(print_stream_, "%-60s %3u %-8s %-32s%s\n", stripped_name.c_str(),
              field_id, field.type.c_str(), field.name.c_str(),
              stripped_nested.c_str());
    }
  }
}

std::string FilterUtil::GenerateFilterBytecode() {
  protozero::FilterBytecodeGenerator bytecode_gen;

  // Assign indexes to descriptors, simply by counting them in order;
  std::map<Message*, uint32_t> descr_to_idx;
  for (auto& descr : descriptors_)
    descr_to_idx[&descr] = static_cast<uint32_t>(descr_to_idx.size());

  for (auto& descr : descriptors_) {
    for (auto it = descr.fields.begin(); it != descr.fields.end();) {
      uint32_t field_id = it->first;
      const Message::Field& field = it->second;
      if (field.nested_type) {
        // Append the index of the target submessage.
        PERFETTO_CHECK(descr_to_idx.count(field.nested_type));
        uint32_t nested_msg_index = descr_to_idx[field.nested_type];
        bytecode_gen.AddNestedField(field_id, nested_msg_index);
        ++it;
        continue;
      }
      if (field.filter_string) {
        bytecode_gen.AddFilterStringField(field_id);
        ++it;
        continue;
      }
      // Simple field. Lookahead to see if we have a range of contiguous simple
      // fields.
      for (uint32_t range_len = 1;; ++range_len) {
        ++it;
        if (it != descr.fields.end() && it->first == field_id + range_len &&
            it->second.is_simple()) {
          continue;
        }
        // At this point it points to either the end() of the vector or a
        // non-contiguous or non-simple field (which will be picked up by the
        // next iteration).
        if (range_len == 1) {
          bytecode_gen.AddSimpleField(field_id);
        } else {
          bytecode_gen.AddSimpleFieldRange(field_id, range_len);
        }
        break;
      }  // for (range_len)
    }  // for (descr.fields)
    bytecode_gen.EndMessage();
  }  // for (descriptors)
  return bytecode_gen.Serialize();
}

std::string FilterUtil::LookupField(const std::string& varint_encoded_path) {
  const uint8_t* ptr =
      reinterpret_cast<const uint8_t*>(varint_encoded_path.data());
  const uint8_t* const end = ptr + varint_encoded_path.size();

  std::vector<uint32_t> fields;
  while (ptr < end) {
    uint64_t varint;
    const uint8_t* next = proto_utils::ParseVarInt(ptr, end, &varint);
    PERFETTO_CHECK(next != ptr);
    fields.emplace_back(static_cast<uint32_t>(varint));
    ptr = next;
  }
  return LookupField(fields.data(), fields.size());
}

std::string FilterUtil::LookupField(const uint32_t* field_ids,
                                    size_t num_fields) {
  const Message* msg = descriptors_.empty() ? nullptr : &descriptors_.front();
  std::string res;
  for (size_t i = 0; i < num_fields; ++i) {
    const uint32_t field_id = field_ids[i];
    const Message::Field* field = nullptr;
    if (msg) {
      auto it = msg->fields.find(field_id);
      field = it == msg->fields.end() ? nullptr : &it->second;
    }
    res.append(".");
    if (field) {
      res.append(field->name);
      msg = field->nested_type;
    } else {
      res.append(std::to_string(field_id));
    }
  }
  return res;
}

}  // namespace protozero
