/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/tools/tracing_proto_extensions.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/message.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/common/descriptor.pbzero.h"
#include "src/protozero/multifile_error_collector.h"
#include "src/trace_processor/util/simple_json_parser.h"

namespace perfetto {
namespace gen_proto_extensions {

namespace pbzero = protos::pbzero;
using trace_processor::json::FieldResult;
using trace_processor::json::SimpleJsonParser;

namespace {

// Sorts ranges by start and checks for validity (start <= end, no internal
// overlaps).
base::Status SortAndValidateRanges(std::vector<Range>& ranges,
                                   const std::string& name,
                                   const std::string& source_path) {
  std::sort(ranges.begin(), ranges.end());
  for (size_t i = 0; i < ranges.size(); ++i) {
    if (ranges[i].first > ranges[i].second) {
      return base::ErrStatus("Invalid range [%d, %d] for '%s' in '%s'",
                             ranges[i].first, ranges[i].second, name.c_str(),
                             source_path.c_str());
    }
    if (i > 0 && ranges[i].first <= ranges[i - 1].second) {
      return base::ErrStatus(
          "Overlapping ranges [%d, %d] and [%d, %d] for '%s' in '%s'",
          ranges[i - 1].first, ranges[i - 1].second, ranges[i].first,
          ranges[i].second, name.c_str(), source_path.c_str());
    }
  }
  return base::OkStatus();
}

// Merges adjacent/touching sorted non-overlapping ranges into a canonical form.
std::vector<Range> MergeAdjacentRanges(const std::vector<Range>& sorted) {
  std::vector<Range> merged;
  for (const auto& r : sorted) {
    if (!merged.empty() && r.first == merged.back().second + 1) {
      merged.back().second = r.second;
    } else {
      merged.push_back(r);
    }
  }
  return merged;
}

// Converts a google::protobuf::FieldDescriptorProto to our protozero
// representation, keeping only the fields present in our descriptor.proto.
void ConvertFieldDescriptor(const google::protobuf::FieldDescriptorProto& src,
                            pbzero::FieldDescriptorProto* dst) {
  if (src.has_name())
    dst->set_name(src.name());
  if (src.has_number())
    dst->set_number(src.number());
  if (src.has_label())
    dst->set_label(
        static_cast<pbzero::FieldDescriptorProto_Label>(src.label()));
  if (src.has_type())
    dst->set_type(static_cast<pbzero::FieldDescriptorProto_Type>(src.type()));
  if (src.has_type_name())
    dst->set_type_name(src.type_name());
  if (src.has_extendee())
    dst->set_extendee(src.extendee());
  if (src.has_default_value())
    dst->set_default_value(src.default_value());
  if (src.has_oneof_index())
    dst->set_oneof_index(src.oneof_index());
  if (src.has_options() && src.options().has_packed()) {
    auto* opts = dst->set_options();
    opts->set_packed(src.options().packed());
  }
}

// Converts a google::protobuf::EnumValueDescriptorProto.
void ConvertEnumValueDescriptor(
    const google::protobuf::EnumValueDescriptorProto& src,
    pbzero::EnumValueDescriptorProto* dst) {
  if (src.has_name())
    dst->set_name(src.name());
  if (src.has_number())
    dst->set_number(src.number());
}

// Converts a google::protobuf::EnumDescriptorProto.
void ConvertEnumDescriptor(const google::protobuf::EnumDescriptorProto& src,
                           pbzero::EnumDescriptorProto* dst) {
  if (src.has_name())
    dst->set_name(src.name());
  for (int i = 0; i < src.value_size(); ++i) {
    ConvertEnumValueDescriptor(src.value(i), dst->add_value());
  }
  for (int i = 0; i < src.reserved_name_size(); ++i) {
    dst->add_reserved_name(src.reserved_name(i));
  }
}

// Forward declaration for recursion.
void ConvertDescriptor(const google::protobuf::DescriptorProto& src,
                       pbzero::DescriptorProto* dst);

void ConvertOneofDescriptor(const google::protobuf::OneofDescriptorProto& src,
                            pbzero::OneofDescriptorProto* dst) {
  if (src.has_name())
    dst->set_name(src.name());
  // OneofOptions is empty in our descriptor.proto, skip it.
}

void ConvertDescriptor(const google::protobuf::DescriptorProto& src,
                       pbzero::DescriptorProto* dst) {
  if (src.has_name())
    dst->set_name(src.name());
  for (int i = 0; i < src.field_size(); ++i) {
    ConvertFieldDescriptor(src.field(i), dst->add_field());
  }
  for (int i = 0; i < src.extension_size(); ++i) {
    ConvertFieldDescriptor(src.extension(i), dst->add_extension());
  }
  for (int i = 0; i < src.nested_type_size(); ++i) {
    ConvertDescriptor(src.nested_type(i), dst->add_nested_type());
  }
  for (int i = 0; i < src.enum_type_size(); ++i) {
    ConvertEnumDescriptor(src.enum_type(i), dst->add_enum_type());
  }
  for (int i = 0; i < src.oneof_decl_size(); ++i) {
    ConvertOneofDescriptor(src.oneof_decl(i), dst->add_oneof_decl());
  }
  for (int i = 0; i < src.reserved_range_size(); ++i) {
    auto* rr = dst->add_reserved_range();
    rr->set_start(src.reserved_range(i).start());
    rr->set_end(src.reserved_range(i).end());
  }
  for (int i = 0; i < src.reserved_name_size(); ++i) {
    dst->add_reserved_name(src.reserved_name(i));
  }
}

// Converts a google::protobuf::FileDescriptorProto to our protozero
// representation, keeping only the fields present in our descriptor.proto.
void ConvertFileDescriptor(const google::protobuf::FileDescriptorProto& src,
                           pbzero::FileDescriptorProto* dst) {
  if (src.has_name())
    dst->set_name(src.name());
  if (src.has_package())
    dst->set_package(src.package());
  for (int i = 0; i < src.dependency_size(); ++i) {
    dst->add_dependency(src.dependency(i));
  }
  for (int i = 0; i < src.public_dependency_size(); ++i) {
    dst->add_public_dependency(src.public_dependency(i));
  }
  for (int i = 0; i < src.message_type_size(); ++i) {
    ConvertDescriptor(src.message_type(i), dst->add_message_type());
  }
  for (int i = 0; i < src.enum_type_size(); ++i) {
    ConvertEnumDescriptor(src.enum_type(i), dst->add_enum_type());
  }
  for (int i = 0; i < src.extension_size(); ++i) {
    ConvertFieldDescriptor(src.extension(i), dst->add_extension());
  }
}

// Validates that:
// 1. At least one extension targeting |scope| exists in |file_desc|.
// 2. All such extension fields have field numbers within the given |ranges|.
base::Status ValidateFieldNumbers(
    const google::protobuf::FileDescriptor* file_desc,
    const std::string& scope,
    const std::vector<Range>& ranges) {
  auto in_range = [&](int32_t num) {
    for (const auto& r : ranges) {
      if (num >= r.first && num <= r.second)
        return true;
    }
    return false;
  };

  bool found_extension = false;

  for (int i = 0; i < file_desc->extension_count(); ++i) {
    const auto* ext = file_desc->extension(i);
    if (ext->containing_type()->full_name() == scope) {
      found_extension = true;
      if (!in_range(ext->number())) {
        return base::ErrStatus(
            "Extension field '%.*s' (number %d) in '%.*s' is outside the "
            "allocated ranges",
            static_cast<int>(ext->name().size()), ext->name().data(),
            ext->number(), static_cast<int>(file_desc->name().size()),
            file_desc->name().data());
      }
    }
  }
  // Also check extensions defined inside messages (the protozero wrapper
  // pattern: message Foo { extend TrackEvent { ... } }).
  for (int i = 0; i < file_desc->message_type_count(); ++i) {
    const auto* msg = file_desc->message_type(i);
    for (int j = 0; j < msg->extension_count(); ++j) {
      const auto* ext = msg->extension(j);
      if (ext->containing_type()->full_name() == scope) {
        found_extension = true;
        if (!in_range(ext->number())) {
          return base::ErrStatus(
              "Extension field '%.*s' (number %d) in '%.*s.%.*s' is "
              "outside the allocated ranges",
              static_cast<int>(ext->name().size()), ext->name().data(),
              ext->number(), static_cast<int>(file_desc->name().size()),
              file_desc->name().data(), static_cast<int>(msg->name().size()),
              msg->name().data());
        }
      }
    }
  }

  if (!found_extension) {
    return base::ErrStatus("Proto '%.*s' has no extensions targeting '%s'",
                           static_cast<int>(file_desc->name().size()),
                           file_desc->name().data(), scope.c_str());
  }
  return base::OkStatus();
}

// Parses a single registry object from the current parser position.
// The parser should be positioned at a JSON object with
// scope/range/allocations.
base::StatusOr<Registry> ParseRegistryObject(SimpleJsonParser& parser,
                                             const std::string& source_path) {
  Registry reg;
  reg.source_path = source_path;

  bool has_range = false;
  bool has_ranges = false;
  RETURN_IF_ERROR(parser.ForEachField([&](std::string_view key) -> FieldResult {
    if (key == "scope") {
      auto v = parser.GetString();
      if (v)
        reg.scope = std::string(*v);
      return FieldResult::Handled{};
    }
    if (key == "range") {
      if (has_ranges)
        return base::ErrStatus("Cannot have both 'range' and 'ranges' in '%s'",
                               source_path.c_str());
      has_range = true;
      if (!parser.IsArray())
        return base::ErrStatus("'range' must be an array in '%s'",
                               source_path.c_str());
      ASSIGN_OR_RETURN(auto arr, parser.CollectInt64Array());
      if (arr.size() != 2)
        return base::ErrStatus("'range' must have exactly 2 elements in '%s'",
                               source_path.c_str());
      reg.ranges.push_back(
          {static_cast<int32_t>(arr[0]), static_cast<int32_t>(arr[1])});
      return FieldResult::Handled{};
    }
    if (key == "ranges") {
      if (has_range)
        return base::ErrStatus("Cannot have both 'range' and 'ranges' in '%s'",
                               source_path.c_str());
      has_ranges = true;
      if (!parser.IsArray())
        return base::ErrStatus("'ranges' must be an array in '%s'",
                               source_path.c_str());
      RETURN_IF_ERROR(parser.ForEachArrayElement([&]() -> base::Status {
        if (!parser.IsArray())
          return base::ErrStatus(
              "Each element of 'ranges' must be an array in '%s'",
              source_path.c_str());
        ASSIGN_OR_RETURN(auto arr, parser.CollectInt64Array());
        if (arr.size() != 2)
          return base::ErrStatus(
              "Each range in 'ranges' must have exactly 2 elements in '%s'",
              source_path.c_str());
        reg.ranges.push_back(
            {static_cast<int32_t>(arr[0]), static_cast<int32_t>(arr[1])});
        return base::OkStatus();
      }));
      return FieldResult::Handled{};
    }
    if (key == "allocations") {
      if (!parser.IsArray())
        return base::ErrStatus("'allocations' must be an array in '%s'",
                               source_path.c_str());
      RETURN_IF_ERROR(parser.ForEachArrayElement([&]() -> base::Status {
        if (!parser.IsObject())
          return base::ErrStatus("Each allocation must be an object in '%s'",
                                 source_path.c_str());
        Allocation alloc;
        bool alloc_has_range = false;
        bool alloc_has_ranges = false;
        RETURN_IF_ERROR(parser.ForEachField([&](std::string_view field)
                                                -> FieldResult {
          if (field == "name") {
            auto v = parser.GetString();
            if (v)
              alloc.name = std::string(*v);
            return FieldResult::Handled{};
          }
          if (field == "range") {
            if (alloc_has_ranges)
              return base::ErrStatus(
                  "Cannot have both 'range' and 'ranges' in allocation");
            alloc_has_range = true;
            if (!parser.IsArray())
              return base::ErrStatus("allocation 'range' must be an array");
            ASSIGN_OR_RETURN(auto arr, parser.CollectInt64Array());
            if (arr.size() != 2)
              return base::ErrStatus("allocation 'range' must have 2 elements");
            alloc.ranges.push_back(
                {static_cast<int32_t>(arr[0]), static_cast<int32_t>(arr[1])});
            return FieldResult::Handled{};
          }
          if (field == "ranges") {
            if (alloc_has_range)
              return base::ErrStatus(
                  "Cannot have both 'range' and 'ranges' in allocation");
            alloc_has_ranges = true;
            if (!parser.IsArray())
              return base::ErrStatus("allocation 'ranges' must be an array");
            RETURN_IF_ERROR(parser.ForEachArrayElement([&]() -> base::Status {
              if (!parser.IsArray())
                return base::ErrStatus(
                    "Each element of allocation 'ranges' must be an "
                    "array");
              ASSIGN_OR_RETURN(auto arr, parser.CollectInt64Array());
              if (arr.size() != 2)
                return base::ErrStatus(
                    "Each range in allocation 'ranges' must have 2 "
                    "elements");
              alloc.ranges.push_back(
                  {static_cast<int32_t>(arr[0]), static_cast<int32_t>(arr[1])});
              return base::OkStatus();
            }));
            return FieldResult::Handled{};
          }
          if (field == "contact") {
            auto v = parser.GetString();
            if (v)
              alloc.contact = std::string(*v);
            return FieldResult::Handled{};
          }
          if (field == "description") {
            auto v = parser.GetString();
            if (v)
              alloc.description = std::string(*v);
            return FieldResult::Handled{};
          }
          if (field == "repo") {
            auto v = parser.GetString();
            if (v)
              alloc.repo = std::string(*v);
            return FieldResult::Handled{};
          }
          if (field == "proto") {
            auto v = parser.GetString();
            if (v)
              alloc.proto = std::string(*v);
            return FieldResult::Handled{};
          }
          if (field == "registry") {
            auto v = parser.GetString();
            if (v)
              alloc.registry = std::string(*v);
            return FieldResult::Handled{};
          }
          if (field == "comment")
            return FieldResult::Skip{};
          return base::ErrStatus("Unknown field '%.*s' in allocation in '%s'",
                                 static_cast<int>(field.size()), field.data(),
                                 source_path.c_str());
        }));
        reg.allocations.push_back(std::move(alloc));
        return base::OkStatus();
      }));
      return FieldResult::Handled{};
    }
    if (key == "comment")
      return FieldResult::Skip{};
    return base::ErrStatus("Unknown field '%.*s' in '%s'",
                           static_cast<int>(key.size()), key.data(),
                           source_path.c_str());
  }));

  return std::move(reg);
}

}  // namespace

base::StatusOr<std::vector<Registry>> ParseRegistryFile(
    const std::string& json_contents,
    const std::string& source_path) {
  SimpleJsonParser parser(json_contents);
  {
    auto status = parser.Parse();
    if (!status.ok())
      return base::ErrStatus("Failed to parse JSON in '%s': %s",
                             source_path.c_str(), status.message().c_str());
  }

  std::vector<Registry> extensions;
  RETURN_IF_ERROR(parser.ForEachField([&](std::string_view key) -> FieldResult {
    if (key == "extensions") {
      if (!parser.IsArray())
        return base::ErrStatus("'extensions' must be an array in '%s'",
                               source_path.c_str());
      RETURN_IF_ERROR(parser.ForEachArrayElement([&]() -> base::Status {
        if (!parser.IsObject())
          return base::ErrStatus(
              "Each entry in 'extensions' must be an object in '%s'",
              source_path.c_str());
        ASSIGN_OR_RETURN(auto reg, ParseRegistryObject(parser, source_path));
        extensions.push_back(std::move(reg));
        return base::OkStatus();
      }));
      return FieldResult::Handled{};
    }
    if (key == "comment")
      return FieldResult::Skip{};
    return base::ErrStatus("Unknown field '%.*s' in '%s'",
                           static_cast<int>(key.size()), key.data(),
                           source_path.c_str());
  }));

  return extensions;
}

base::Status ValidateRegistry(const Registry& reg) {
  // Currently only TrackEvent extensions are supported. In the future, this
  // field could be used to disambiguate TracePacket extensions.
  if (reg.scope != "perfetto.protos.TrackEvent") {
    return base::ErrStatus(
        "'scope' must be \"perfetto.protos.TrackEvent\" in '%s'",
        reg.source_path.c_str());
  }

  if (reg.ranges.empty()) {
    return base::ErrStatus("No ranges specified in '%s'",
                           reg.source_path.c_str());
  }

  // Sort and validate registry ranges.
  std::vector<Range> reg_ranges = reg.ranges;
  RETURN_IF_ERROR(
      SortAndValidateRanges(reg_ranges, "registry", reg.source_path));

  if (reg.allocations.empty()) {
    return base::ErrStatus("No allocations in '%s'", reg.source_path.c_str());
  }

  // Collect all allocation ranges, validating each one individually.
  std::vector<Range> all_alloc_ranges;
  for (const auto& alloc : reg.allocations) {
    if (alloc.ranges.empty()) {
      return base::ErrStatus("No ranges for allocation '%s' in '%s'",
                             alloc.name.c_str(), reg.source_path.c_str());
    }
    std::vector<Range> alloc_ranges = alloc.ranges;
    RETURN_IF_ERROR(
        SortAndValidateRanges(alloc_ranges, alloc.name, reg.source_path));
    all_alloc_ranges.insert(all_alloc_ranges.end(), alloc_ranges.begin(),
                            alloc_ranges.end());
  }

  // Sort all allocation ranges and check for overlaps between allocations.
  std::sort(all_alloc_ranges.begin(), all_alloc_ranges.end());
  for (size_t i = 1; i < all_alloc_ranges.size(); ++i) {
    if (all_alloc_ranges[i].first <= all_alloc_ranges[i - 1].second) {
      return base::ErrStatus(
          "Allocation ranges [%d, %d] and [%d, %d] overlap "
          "(gap or overlap) in '%s'",
          all_alloc_ranges[i - 1].first, all_alloc_ranges[i - 1].second,
          all_alloc_ranges[i].first, all_alloc_ranges[i].second,
          reg.source_path.c_str());
    }
  }

  // Check that the union of all allocation ranges exactly tiles the registry
  // ranges. Merge adjacent ranges and compare.
  auto merged_allocs = MergeAdjacentRanges(all_alloc_ranges);
  auto merged_reg = MergeAdjacentRanges(reg_ranges);
  if (merged_allocs != merged_reg) {
    return base::ErrStatus(
        "Allocations do not exactly tile the registry ranges "
        "(gap or overlap) in '%s'",
        reg.source_path.c_str());
  }

  // Check that each non-unallocated entry has either proto or registry (but
  // not both), and that unallocated entries have neither.
  for (const auto& alloc : reg.allocations) {
    if (alloc.name == "unallocated") {
      if (!alloc.proto.empty() || !alloc.registry.empty()) {
        return base::ErrStatus(
            "Unallocated entry should not have 'proto' or 'registry' in '%s'",
            reg.source_path.c_str());
      }
      continue;
    }
    bool has_proto = !alloc.proto.empty();
    bool has_registry = !alloc.registry.empty();
    bool has_repo = !alloc.repo.empty();
    // Remote entries (has repo) might not have a local proto/registry path,
    // or they might have one that points into the remote repo. Either way,
    // we don't validate remote entries.
    if (!has_repo && !has_proto && !has_registry) {
      return base::ErrStatus(
          "Allocation '%s' must have 'proto' or 'registry' in '%s'",
          alloc.name.c_str(), reg.source_path.c_str());
    }
    if (has_proto && has_registry) {
      return base::ErrStatus(
          "Allocation '%s' has both 'proto' and 'registry' in '%s'",
          alloc.name.c_str(), reg.source_path.c_str());
    }
  }
  return base::OkStatus();
}

namespace {

// Recursively collects all proto files from the registry tree.
struct ProtoEntry {
  std::string proto_path;
  std::string scope;
  std::vector<Range> ranges;
};

// Walks allocations from an already-parsed registry. For sub-registries,
// reads and parses them using the flat (non-wrapped) format.
base::Status CollectProtosFromRegistry(const Registry& reg,
                                       const std::string& root_dir,
                                       std::vector<ProtoEntry>* out) {
  for (const auto& alloc : reg.allocations) {
    if (!alloc.proto.empty() && alloc.repo.empty()) {
      // Local proto leaf.
      out->push_back({root_dir + "/" + alloc.proto, reg.scope, alloc.ranges});
    } else if (!alloc.registry.empty() && alloc.repo.empty()) {
      // Local sub-registry (same {"extensions": [...]} format).
      std::string sub_path = root_dir + "/" + alloc.registry;
      std::string contents;
      if (!base::ReadFile(sub_path, &contents)) {
        return base::ErrStatus("Failed to read '%s'", sub_path.c_str());
      }
      ASSIGN_OR_RETURN(auto sub_regs, ParseRegistryFile(contents, sub_path));
      for (const auto& sub_reg : sub_regs) {
        RETURN_IF_ERROR(ValidateRegistry(sub_reg));
        RETURN_IF_ERROR(CollectProtosFromRegistry(sub_reg, root_dir, out));
      }
    }
    // Remote entries (repo is set) are skipped.
  }
  return base::OkStatus();
}

}  // namespace

base::StatusOr<std::vector<uint8_t>> GenerateExtensionDescriptors(
    const std::string& root_json_path,
    const std::vector<std::string>& proto_paths,
    const std::string& root_dir) {
  // 1. Read and parse the root registry file (uses the "extensions" wrapper).
  std::string root_contents;
  if (!base::ReadFile(root_json_path, &root_contents)) {
    return base::ErrStatus("Failed to read '%s'", root_json_path.c_str());
  }
  ASSIGN_OR_RETURN(auto extensions,
                   ParseRegistryFile(root_contents, root_json_path));

  // 2. Recursively collect all local proto entries from each registry.
  std::vector<ProtoEntry> entries;
  for (const auto& reg : extensions) {
    RETURN_IF_ERROR(ValidateRegistry(reg));
    RETURN_IF_ERROR(CollectProtosFromRegistry(reg, root_dir, &entries));
  }

  if (entries.empty()) {
    PERFETTO_ILOG("No local proto files found in registry.");
    // Return an empty FileDescriptorSet.
    protozero::HeapBuffered<pbzero::FileDescriptorSet> fds;
    return fds.SerializeAsArray();
  }

  // 2. Set up protoc importer.
  protozero::MultiFileErrorCollectorImpl error_collector;
  google::protobuf::compiler::DiskSourceTree source_tree;
  for (const auto& path : proto_paths) {
    source_tree.MapPath("", path);
  }
  google::protobuf::compiler::Importer importer(&source_tree, &error_collector);

  // Track which files we've already added to avoid duplicates.
  std::set<std::string> added_files;

  // 3. Compile each proto and collect descriptors.
  protozero::HeapBuffered<pbzero::FileDescriptorSet> fds;

  for (const auto& entry : entries) {
    // The proto path in the JSON is relative to root_dir, but protoc needs
    // it relative to one of the -I paths. Since root_dir is typically one
    // of the -I paths, we use the path as-is from the allocation.
    // We need to derive the proto import path from the full path.
    std::string proto_import_path = entry.proto_path;
    // Strip root_dir prefix if present.
    if (base::StartsWith(proto_import_path, root_dir + "/")) {
      proto_import_path = proto_import_path.substr(root_dir.size() + 1);
    }

    const auto* file_desc = importer.Import(proto_import_path);
    if (!file_desc) {
      return base::ErrStatus("Failed to compile proto '%s'",
                             proto_import_path.c_str());
    }

    // Validate field numbers.
    RETURN_IF_ERROR(ValidateFieldNumbers(file_desc, entry.scope, entry.ranges));

    // Convert to our descriptor format. We include the extension file itself
    // and its transitive dependencies that are NOT core Perfetto protos
    // (those are already built into TraceProcessor).
    // For simplicity, we include all dependencies. TraceProcessor's
    // DescriptorPool handles duplicates gracefully.
    google::protobuf::FileDescriptorProto file_proto;
    file_desc->CopyTo(&file_proto);

    if (added_files.insert(file_proto.name()).second) {
      ConvertFileDescriptor(file_proto, fds->add_file());
    }

    // Also add direct dependencies needed for type resolution.
    for (int i = 0; i < file_desc->dependency_count(); ++i) {
      google::protobuf::FileDescriptorProto dep_proto;
      file_desc->dependency(i)->CopyTo(&dep_proto);
      if (added_files.insert(dep_proto.name()).second) {
        ConvertFileDescriptor(dep_proto, fds->add_file());
      }
    }
  }

  return fds.SerializeAsArray();
}

}  // namespace gen_proto_extensions
}  // namespace perfetto
