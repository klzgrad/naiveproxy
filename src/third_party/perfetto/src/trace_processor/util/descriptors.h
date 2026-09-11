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

#ifndef SRC_TRACE_PROCESSOR_UTIL_DESCRIPTORS_H_
#define SRC_TRACE_PROCESSOR_UTIL_DESCRIPTORS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/protozero/field.h"

namespace perfetto::trace_processor {

class FieldDescriptor {
 public:
  FieldDescriptor(std::string name,
                  uint32_t number,
                  uint32_t type,
                  std::string raw_type_name,
                  std::vector<uint8_t> options,
                  std::optional<std::string> default_value,
                  bool is_repeated,
                  bool is_packed,
                  bool is_extension = false);

  const std::string& name() const { return name_; }
  uint32_t number() const { return number_; }
  uint32_t type() const { return type_; }
  const std::string& raw_type_name() const { return raw_type_name_; }
  const std::string& resolved_type_name() const { return resolved_type_name_; }
  std::optional<uint32_t> flags_enum_descriptor_idx() const {
    return flags_enum_descriptor_idx_;
  }
  bool is_pid() const { return is_pid_; }
  bool is_tid() const { return is_tid_; }
  bool is_repeated() const { return is_repeated_; }
  bool is_packed() const { return is_packed_; }
  bool is_extension() const { return is_extension_; }
  const std::string& extension_full_name() const {
    return extension_full_name_;
  }

  const std::vector<uint8_t>& options() const { return options_; }
  std::vector<uint8_t>* mutable_options() { return &options_; }
  const std::optional<std::string>& default_value() const {
    return default_value_;
  }

  void set_resolved_type_name(const std::string& resolved_type_name) {
    resolved_type_name_ = resolved_type_name;
  }

  void set_flags_enum_descriptor_idx(uint32_t idx) {
    flags_enum_descriptor_idx_ = idx;
  }

  void set_is_pid(bool is_pid) { is_pid_ = is_pid; }
  void set_is_tid(bool is_tid) { is_tid_ = is_tid; }

  void set_extension_full_name(const std::string& extension_full_name) {
    extension_full_name_ = extension_full_name;
  }

 private:
  std::string name_;
  uint32_t number_;
  uint32_t type_;
  std::string raw_type_name_;
  std::string resolved_type_name_;
  std::optional<uint32_t> flags_enum_descriptor_idx_;
  bool is_pid_ = false;
  bool is_tid_ = false;
  std::vector<uint8_t> options_;
  std::optional<std::string> default_value_;
  bool is_repeated_;
  bool is_packed_;
  bool is_extension_;
  std::string extension_full_name_;
};

class ProtoDescriptor {
 public:
  enum class Type { kEnum = 0, kMessage = 1 };

  ProtoDescriptor(std::string file_name,
                  std::string package_name,
                  std::string full_name,
                  Type type,
                  std::optional<uint32_t> parent_id);

  void AddField(FieldDescriptor descriptor) {
    PERFETTO_DCHECK(type_ == Type::kMessage);
    fields_.emplace(descriptor.number(), std::move(descriptor));
  }

  void AddEnumValue(int32_t integer_representation,
                    std::string string_representation) {
    PERFETTO_DCHECK(type_ == Type::kEnum);
    enum_values_by_name_[string_representation] = integer_representation;
    enum_names_by_value_[integer_representation] =
        std::move(string_representation);
  }

  const FieldDescriptor* FindFieldByName(const std::string& name) const {
    PERFETTO_DCHECK(type_ == Type::kMessage);
    auto it = std::find_if(
        fields_.begin(), fields_.end(),
        [name](const std::pair<const uint32_t, FieldDescriptor>& p) {
          return p.second.name() == name;
        });
    if (it == fields_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  const FieldDescriptor* FindFieldByTag(const uint32_t tag_number) const {
    PERFETTO_DCHECK(type_ == Type::kMessage);
    auto it = fields_.find(tag_number);
    if (it == fields_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  std::optional<std::string> FindEnumString(const int32_t value) const {
    PERFETTO_DCHECK(type_ == Type::kEnum);
    auto it = enum_names_by_value_.find(value);
    return it == enum_names_by_value_.end() ? std::nullopt
                                            : std::make_optional(it->second);
  }

  std::optional<int32_t> FindEnumValue(const std::string& value) const {
    PERFETTO_DCHECK(type_ == Type::kEnum);
    auto it = enum_values_by_name_.find(value);
    return it == enum_values_by_name_.end() ? std::nullopt
                                            : std::make_optional(it->second);
  }

  const std::unordered_map<int32_t, std::string>& enum_values_by_number()
      const {
    PERFETTO_DCHECK(type_ == Type::kEnum);
    return enum_names_by_value_;
  }

  const std::string& file_name() const { return file_name_; }

  const std::string& package_name() const { return package_name_; }

  const std::string& full_name() const { return full_name_; }

  Type type() const { return type_; }

  const std::unordered_map<uint32_t, FieldDescriptor>& fields() const {
    return fields_;
  }
  std::unordered_map<uint32_t, FieldDescriptor>* mutable_fields() {
    return &fields_;
  }

 private:
  std::string file_name_;  // File in which descriptor was originally defined.
  std::string package_name_;
  std::string full_name_;
  const Type type_;
  std::optional<uint32_t> parent_id_;
  std::unordered_map<uint32_t, FieldDescriptor> fields_;
  std::unordered_map<int32_t, std::string> enum_names_by_value_;
  std::unordered_map<std::string, int32_t> enum_values_by_name_;
};

struct ExtensionInfo {
  std::string package_name;
  // Enclosing message's full name. Empty for file-scope extends.
  std::string parent_full_name;
  protozero::ConstBytes field_desc_proto;
};

// Sometimes the same extension field number shows up twice with two
// different type names (this happens during a package rename). We can't
// tell right away whether that's fine, because the types they point at
// aren't resolved yet at that stage of loading. So instead of deciding
// on the spot, we jot down what we'd need to compare and come back to it
// once everything is resolved. Each entry here is one "compare these two
// later" note.
struct ExtensionTypeCheck {
  std::string extendee_full_name;
  std::string field_name;
  std::string existing_raw_type;
  std::string new_raw_type;
};

// Holds two descriptor indices that are being compared against each other.
// The pair is unordered: it stores the smaller index first so that (a, b)
// and (b, a) are treated as the same pair. We only care that "these two
// descriptors are being compared", not which one is on the left or right.
struct CanonicalDescriptorPair {
  CanonicalDescriptorPair(uint32_t a, uint32_t b)
      : min_idx(a < b ? a : b), max_idx(a < b ? b : a) {}

  bool operator<(const CanonicalDescriptorPair& other) const {
    if (min_idx != other.min_idx) {
      return min_idx < other.min_idx;
    }
    return max_idx < other.max_idx;
  }

  uint32_t min_idx;
  uint32_t max_idx;
};

class DescriptorPool {
 public:
  // Adds Descriptors from file_descriptor_set_proto. Ignores any FileDescriptor
  // with name matching a prefix in |skip_prefixes|.
  base::Status AddFromFileDescriptorSet(
      const uint8_t* file_descriptor_set_proto,
      size_t size,
      const std::vector<std::string>& skip_prefixes = {},
      bool merge_existing_messages = false);

  std::optional<uint32_t> FindDescriptorIdx(const std::string& full_name) const;

  std::vector<uint8_t> SerializeAsDescriptorSet() const;

  void AddProtoDescriptorForTesting(ProtoDescriptor descriptor) {
    AddProtoDescriptor(std::move(descriptor));
  }

  const std::vector<ProtoDescriptor>& descriptors() const {
    return descriptors_;
  }

  // Opaque memo of a resolved descriptor, reused across FindEnumString calls.
  class CachedDescriptor {
   public:
    CachedDescriptor() = default;

   private:
    friend class DescriptorPool;
    std::optional<uint32_t> descriptor_idx_;
  };

  // Returns the name of enum value |value| within enum |enum_name|, or nullopt
  // if the enum or the value is unknown. |cache| stores the resolved descriptor
  // so later calls skip the by-name lookup.
  std::optional<std::string> FindEnumString(CachedDescriptor& cache,
                                            std::string_view enum_name,
                                            int32_t value) const;

  // Appends the name of each single-bit flag set in |mask| to |*out| (views
  // into the pool's storage), for the flags enum at |enum_descriptor_idx|.
  // Returns the set bits that matched no flag.
  int64_t FlagSetToViews(uint32_t enum_descriptor_idx,
                         int64_t mask,
                         std::vector<std::string_view>* out) const;

 private:
  base::Status AddNestedProtoDescriptors(
      const std::string& file_name,
      const std::string& package_name,
      std::optional<uint32_t> parent_idx,
      protozero::ConstBytes descriptor_proto,
      std::vector<ExtensionInfo>* extensions,
      std::vector<ExtensionTypeCheck>* extension_type_checks,
      bool merge_existing_messages);
  base::Status AddEnumProtoDescriptors(const std::string& file_name,
                                       const std::string& package_name,
                                       std::optional<uint32_t> parent_idx,
                                       protozero::ConstBytes descriptor_proto,
                                       bool merge_existing_messages);

  base::Status AddExtensionField(
      const ExtensionInfo& extension,
      std::vector<ExtensionTypeCheck>* extension_type_checks);

  // Recursively searches for the given short type in all parent messages
  // and packages.
  std::optional<uint32_t> ResolveShortType(const std::string& parent_path,
                                           const std::string& short_type);

  base::Status ResolveUninterpretedOption(const ProtoDescriptor&,
                                          const FieldDescriptor&,
                                          std::vector<uint8_t>&);

  void ResolveFlagsEnumOption(const ProtoDescriptor& descriptor,
                              uint32_t option_number,
                              FieldDescriptor* field);

  // The field numbers of Perfetto's custom field options, looked up once.
  struct CustomOptionNumbers {
    std::optional<uint32_t> flags_enum;
    std::optional<uint32_t> pid;
    std::optional<uint32_t> tid;
  };
  CustomOptionNumbers FindCustomOptionNumbers() const;
  void ResolveCustomFieldOptions(const ProtoDescriptor& descriptor,
                                 const CustomOptionNumbers& numbers,
                                 FieldDescriptor* field);

  // Adds a new descriptor to the pool and returns its index. There must not be
  // already a descriptor with the same full_name in the pool.
  uint32_t AddProtoDescriptor(ProtoDescriptor descriptor);

  bool DescriptorsStructurallyEqual(
      uint32_t root_existing_idx,
      uint32_t root_candidate_idx,
      std::set<CanonicalDescriptorPair>& comparisons_in_progress);
  std::vector<ProtoDescriptor> descriptors_;
  // full_name -> index in the descriptors_ vector.
  std::unordered_map<std::string, uint32_t> full_name_to_descriptor_index_;
  std::set<std::string> processed_files_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_DESCRIPTORS_H_
