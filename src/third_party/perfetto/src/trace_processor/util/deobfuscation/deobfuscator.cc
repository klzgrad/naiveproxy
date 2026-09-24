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

#include "src/trace_processor/util/deobfuscation/deobfuscator.h"
#include "src/trace_processor/util/simple_json_parser.h"

#include <stdlib.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <set>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"

#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace profiling {
namespace {

using SimpleJsonParser = perfetto::trace_processor::json::SimpleJsonParser;
using FieldResult = perfetto::trace_processor::json::FieldResult;

// Json keys used to parse merged classes.
constexpr std::string_view kMergedClassesId =
    "com.android.tools.r8.mergedClasses";
constexpr std::string_view kIdKey = "id";
constexpr std::string_view kClassNameKey = "name";
constexpr std::string_view kClassIdKey = "classId";
constexpr std::string_view kClassIdFieldKey = "classIdField";
constexpr std::string_view kMergedClassesKey = "mergedClasses";

struct ProguardClass {
  std::string obfuscated_name;
  std::string deobfuscated_name;
};

std::optional<ProguardClass> ParseClass(std::string line) {
  base::StringSplitter ss(std::move(line), ' ');

  if (!ss.Next()) {
    PERFETTO_ELOG("Missing deobfuscated name.");
    return std::nullopt;
  }
  std::string deobfuscated_name(ss.cur_token(), ss.cur_token_size());

  if (!ss.Next() || ss.cur_token_size() != 2 ||
      strncmp("->", ss.cur_token(), 2) != 0) {
    PERFETTO_ELOG("Missing ->");
    return std::nullopt;
  }

  if (!ss.Next()) {
    PERFETTO_ELOG("Missing obfuscated name.");
    return std::nullopt;
  }
  std::string obfuscated_name(ss.cur_token(), ss.cur_token_size());
  if (obfuscated_name.empty()) {
    PERFETTO_ELOG("Empty obfuscated name.");
    return std::nullopt;
  }
  if (obfuscated_name.back() != ':') {
    PERFETTO_ELOG("Expected colon.");
    return std::nullopt;
  }

  obfuscated_name.resize(obfuscated_name.size() - 1);
  if (ss.Next()) {
    PERFETTO_ELOG("Unexpected data.");
    return std::nullopt;
  }
  return ProguardClass{std::move(obfuscated_name),
                       std::move(deobfuscated_name)};
}

// Forward declaration.
base::Status ParseMergedClass(SimpleJsonParser& parser,
                              ObfuscatedClass::MergedClass& out);

// Parses common fields for MergedClasses (classIdField and mergedClasses).
FieldResult ParseMergedClassesField(SimpleJsonParser& parser,
                                    std::string_view key,
                                    ObfuscatedClass::MergedClasses& mcs) {
  if (key == kClassIdFieldKey) {
    if (auto val = parser.GetString()) {
      mcs.class_id_field_name = std::string(*val);
    } else {
      return base::Status("Expected class id field name.");
    }
    return FieldResult::Handled{};
  }
  if (key == kMergedClassesKey) {
    if (!parser.IsArray()) {
      return base::Status("Expected array for mergedClasses.");
    }
    base::Status array_status =
        parser.ForEachArrayElement([&]() -> base::Status {
          ObfuscatedClass::MergedClass mc;
          RETURN_IF_ERROR(ParseMergedClass(parser, mc));
          mcs.merged_classes.push_back(std::move(mc));
          return base::OkStatus();
        });
    return FieldResult(array_status);
  }
  return FieldResult::Skip{};
}

// Parses a single MergedClass entry from R8 mergedClasses JSON.
base::Status ParseMergedClass(SimpleJsonParser& parser,
                              ObfuscatedClass::MergedClass& out) {
  return parser.ForEachField([&](std::string_view key) -> FieldResult {
    if (key == kClassNameKey) {
      if (auto val = parser.GetString()) {
        out.name = std::string(*val);
      } else {
        return base::Status("Expected class name.");
      }
      return FieldResult::Handled{};
    }
    if (key == kClassIdKey) {
      if (auto val_str = parser.GetString()) {
        if (auto parsed_val = base::StringViewToInt32(*val_str)) {
          out.class_id = *parsed_val;
        } else {
          return base::Status("Invalid class id format in string.");
        }
      } else if (auto val_int = parser.GetInt64();
                 val_int.has_value() &&
                 val_int.value() >= std::numeric_limits<int32_t>::min() &&
                 val_int.value() <= std::numeric_limits<int32_t>::max()) {
        out.class_id = static_cast<int32_t>(*val_int);
      } else {
        return base::Status("Expected class id.");
      }
      return FieldResult::Handled{};
    }
    FieldResult res =
        ParseMergedClassesField(parser, key, out.nested_merged_classes);
    if (!res.handled) {
      PERFETTO_DLOG("Unknown field in merged class JSON: %.*s",
                    static_cast<int>(key.size()), key.data());
    }
    return res;
  });
}

// Parses R8 `com.android.tools.r8.mergedClasses` JSON comment string in
// Proguard mapping.
base::Status ParseMergedClassesComment(std::string_view json_str,
                                       ObfuscatedClass& target_class) {
  if (json_str.find(kMergedClassesKey) == std::string_view::npos) {
    // Avoid full parsing if possible.
    return base::OkStatus();
  }
  SimpleJsonParser parser(json_str);
  RETURN_IF_ERROR(parser.Parse());

  bool is_merged_classes_id = false;
  ObfuscatedClass::MergedClasses mcs;

  base::Status s =
      parser.ForEachField([&](std::string_view key) -> FieldResult {
        if (key == kIdKey) {
          if (auto val = parser.GetString()) {
            if (*val == kMergedClassesId) {
              is_merged_classes_id = true;
            }
          }
          return FieldResult::Handled{};
        }
        return ParseMergedClassesField(parser, key, mcs);
      });
  RETURN_IF_ERROR(s);

  if (is_merged_classes_id) {
    *target_class.mutable_merged_classes() = std::move(mcs);
  }
  return base::OkStatus();
}

enum class ProguardMemberType {
  kField,
  kMethod,
};

struct ProguardMember {
  ProguardMemberType type;
  std::string obfuscated_name;
  std::string deobfuscated_name;
  // Line number info for R8 inline support (methods only)
  std::optional<uint32_t> obfuscated_line_start;
  std::optional<uint32_t> obfuscated_line_end;
  std::optional<uint32_t> source_line_start;
  std::optional<uint32_t> source_line_end;
};

// Parse line range like "1:3" or just "1". Returns (start, end) or nullopt.
std::optional<std::pair<uint32_t, uint32_t>> ParseLineRange(
    const std::string& s) {
  if (s.empty()) {
    return std::nullopt;
  }
  auto colon = s.find(':');
  if (colon == std::string::npos) {
    char* end;
    uint32_t val = static_cast<uint32_t>(strtoul(s.c_str(), &end, 10));
    if (end != s.c_str() + s.size()) {
      return std::nullopt;
    }
    return std::make_pair(val, val);
  }
  char* end;
  std::string start_str = s.substr(0, colon);
  uint32_t start = static_cast<uint32_t>(strtoul(start_str.c_str(), &end, 10));
  if (*end != '\0') {
    return std::nullopt;
  }
  std::string stop_str = s.substr(colon + 1);
  uint32_t stop = static_cast<uint32_t>(strtoul(stop_str.c_str(), &end, 10));
  if (*end != '\0') {
    return std::nullopt;
  }
  return std::make_pair(start, stop);
}

std::optional<ProguardMember> ParseMember(std::string line) {
  base::StringSplitter ss(std::move(line), ' ');

  if (!ss.Next()) {
    PERFETTO_ELOG("Missing type name.");
    return std::nullopt;
  }
  std::string type_name(ss.cur_token(), ss.cur_token_size());

  if (!ss.Next()) {
    PERFETTO_ELOG("Missing deobfuscated name.");
    return std::nullopt;
  }
  std::string deobfuscated_name(ss.cur_token(), ss.cur_token_size());

  if (!ss.Next() || ss.cur_token_size() != 2 ||
      strncmp("->", ss.cur_token(), 2) != 0) {
    PERFETTO_ELOG("Missing ->");
    return std::nullopt;
  }

  if (!ss.Next()) {
    PERFETTO_ELOG("Missing obfuscated name.");
    return std::nullopt;
  }
  std::string obfuscated_name(ss.cur_token(), ss.cur_token_size());

  if (ss.Next()) {
    PERFETTO_ELOG("Unexpected data.");
    return std::nullopt;
  }

  ProguardMember result;
  result.obfuscated_name = std::move(obfuscated_name);

  auto paren_idx = deobfuscated_name.find('(');
  if (paren_idx != std::string::npos) {
    result.type = ProguardMemberType::kMethod;

    // Parse R8 format: "1:3:void foo():10:12" or "1:3:void foo():10"
    // type_name may be "1:3:void" (with obfuscated line range prefix)
    // deobfuscated_name may be "foo():10:12" (with source line suffix)

    // Extract obfuscated line range from type_name prefix (e.g., "1:3:void")
    // Count colons to find the pattern X:Y:type
    size_t first_colon = type_name.find(':');
    if (first_colon != std::string::npos) {
      size_t second_colon = type_name.find(':', first_colon + 1);
      if (second_colon != std::string::npos) {
        // Has obfuscated line range: "1:3:void"
        std::string obf_range = type_name.substr(0, second_colon);
        auto parsed = ParseLineRange(obf_range);
        if (parsed) {
          result.obfuscated_line_start = parsed->first;
          result.obfuscated_line_end = parsed->second;
        }
        type_name = type_name.substr(second_colon + 1);
      }
    }

    // Extract source line range from deobfuscated_name suffix
    // Format: "foo():10:12" or "foo():10" or "Cls.foo():10"
    size_t close_paren = deobfuscated_name.find(')');
    if (close_paren != std::string::npos &&
        close_paren + 1 < deobfuscated_name.size() &&
        deobfuscated_name[close_paren + 1] == ':') {
      std::string source_range = deobfuscated_name.substr(close_paren + 2);
      auto parsed = ParseLineRange(source_range);
      if (parsed) {
        result.source_line_start = parsed->first;
        result.source_line_end = parsed->second;
      }
      deobfuscated_name.resize(close_paren + 1);
    }

    // Remove parameter list: "foo()" -> "foo"
    deobfuscated_name.resize(paren_idx);
  } else {
    result.type = ProguardMemberType::kField;
  }
  result.deobfuscated_name = std::move(deobfuscated_name);
  return result;
}

std::string FlattenMethods(const std::vector<std::string>& v) {
  if (v.size() == 1) {
    return v[0];
  }
  return "[ambiguous]";
}

}  // namespace

std::string FlattenClasses(
    const std::map<std::string, std::vector<std::string>>& m) {
  std::string result;
  bool first = true;
  for (const auto& p : m) {
    if (!first) {
      result += " | ";
    }
    result += p.first + "." + FlattenMethods(p.second);
    first = false;
  }
  return result;
}

std::map<std::string, std::string> ObfuscatedClass::deobfuscated_methods()
    const {
  std::map<std::string, std::string> result;
  if (method_mappings_.empty()) {
    return result;
  }

  // Group mappings by obfuscated name, tracking line ranges for R8 inline
  // detection.
  struct Group {
    std::vector<size_t> indices;  // Indices into method_mappings_
  };
  std::map<std::string, Group> by_obfuscated;
  for (size_t i = 0; i < method_mappings_.size(); ++i) {
    by_obfuscated[method_mappings_[i].obfuscated_name].indices.push_back(i);
  }

  for (const auto& [obfuscated_name, group] : by_obfuscated) {
    // Try to detect R8 inline chains: entries with same line range but varying
    // source lines. For inline chains, use the outermost method (last in
    // chain).
    bool found_inline_chain = false;
    std::string outermost_method;

    // Check each line range group for inline chain pattern
    for (size_t start = 0; start < group.indices.size();) {
      const auto& first_mapping = method_mappings_[group.indices[start]];

      // Find entries with same line range
      size_t end = start + 1;
      while (end < group.indices.size()) {
        const auto& m = method_mappings_[group.indices[end]];
        if (m.obfuscated_line_start != first_mapping.obfuscated_line_start ||
            m.obfuscated_line_end != first_mapping.obfuscated_line_end) {
          break;
        }
        ++end;
      }

      // Check if source lines vary (indicates inline chain)
      bool is_inline = false;
      for (size_t j = start + 1; j < end; ++j) {
        if (method_mappings_[group.indices[j]].source_line_start !=
            first_mapping.source_line_start) {
          is_inline = true;
          break;
        }
      }

      if (is_inline) {
        // Outermost method is last in the chain
        const auto& last = method_mappings_[group.indices[end - 1]];
        if (!found_inline_chain) {
          found_inline_chain = true;
          outermost_method = last.deobfuscated_name;
        } else if (outermost_method != last.deobfuscated_name) {
          // Different outermost methods in different line ranges - ambiguous
          found_inline_chain = false;
          break;
        }
      } else {
        // Not an inline chain in this range
        found_inline_chain = false;
        break;
      }
      start = end;
    }

    if (found_inline_chain) {
      result[obfuscated_name] = outermost_method;
    } else {
      // Collect unique deobfuscated names
      std::set<std::string> unique_names;
      for (size_t idx : group.indices) {
        unique_names.insert(method_mappings_[idx].deobfuscated_name);
      }

      if (unique_names.size() == 1) {
        result[obfuscated_name] = *unique_names.begin();
      } else {
        // Join with " | " for ambiguous mappings
        std::string joined;
        bool first = true;
        for (const auto& name : unique_names) {
          if (!first) {
            joined += " | ";
          }
          joined += name;
          first = false;
        }
        result[obfuscated_name] = joined;
      }
    }
  }

  return result;
}

// See https://www.guardsquare.com/en/products/proguard/manual/retrace for the
// file format we are parsing.
base::Status ProguardParser::AddLine(std::string line) {
  auto first_ch_pos = line.find_first_not_of(" \t");
  if (first_ch_pos == std::string::npos)
    return base::Status();

  if (line[first_ch_pos] == '#') {
    if (current_class_ == nullptr) {
      return base::Status();
    }
    size_t json_start = line.find('{');
    if (json_start != std::string::npos) {
      std::string_view json_sv = std::string_view(line).substr(json_start);
      base::Status s = ParseMergedClassesComment(json_sv, *current_class_);
      if (!s.ok()) {
        PERFETTO_ELOG("Failed to parse merged classes comment: %s\non line %s",
                      s.message().c_str(), line.c_str());
      }
    }
    return base::Status();
  }

  bool is_member = line[0] == ' ';
  if (is_member && !current_class_) {
    return base::Status(
        "Failed to parse proguard map. Saw member before class.");
  }
  if (!is_member) {
    auto opt_cls = ParseClass(std::move(line));
    if (!opt_cls)
      return base::Status("Class not found.");
    auto p = mapping_.emplace(std::move(opt_cls->obfuscated_name),
                              std::move(opt_cls->deobfuscated_name));
    if (!p.second) {
      return base::Status("Duplicate class.");
    }
    current_class_ = &p.first->second;
  } else {
    auto opt_member = ParseMember(std::move(line));
    if (!opt_member)
      return base::Status("Failed to parse member.");
    switch (opt_member->type) {
      case (ProguardMemberType::kField): {
        if (!current_class_->AddField(opt_member->obfuscated_name,
                                      opt_member->deobfuscated_name)) {
          return base::Status(std::string("Member redefinition: ") +
                              current_class_->deobfuscated_name().c_str() +
                              "." + opt_member->deobfuscated_name.c_str() +
                              " Proguard map invalid");
        }
        break;
      }
      case (ProguardMemberType::kMethod): {
        MethodMapping mapping;
        mapping.obfuscated_name = opt_member->obfuscated_name;

        // Build fully qualified deobfuscated name
        const std::string& method_name = opt_member->deobfuscated_name;
        if (method_name.find('.') != std::string::npos) {
          // Already fully qualified (e.g., "OtherClass.method")
          mapping.deobfuscated_name = method_name;
        } else {
          // Relative to current class
          mapping.deobfuscated_name =
              current_class_->deobfuscated_name() + "." + method_name;
        }
        mapping.obfuscated_line_start = opt_member->obfuscated_line_start;
        mapping.obfuscated_line_end = opt_member->obfuscated_line_end;
        mapping.source_line_start = opt_member->source_line_start;
        mapping.source_line_end = opt_member->source_line_end;
        current_class_->AddMethod(std::move(mapping));
        break;
      }
    }
  }
  return base::Status();
}

bool ProguardParser::AddLines(std::string contents) {
  size_t lineno = 1;
  for (base::StringSplitter lines(std::move(contents), '\n'); lines.Next();) {
    auto status = AddLine(lines.cur_token());
    if (!status.ok()) {
      PERFETTO_ELOG("Failed to parse proguard map (line %zu): %s", lineno,
                    status.c_message());
      return false;
    }
    lineno++;
  }
  return true;
}

static void SerializeMergedClasses(
    const profiling::ObfuscatedClass::MergedClasses& src,
    perfetto::protos::pbzero::ObfuscatedClass::MergedClasses* dest) {
  if (!src.class_id_field_name.empty()) {
    dest->set_class_id_field_name(src.class_id_field_name);
  }
  for (const auto& mc : src.merged_classes) {
    auto* dest_mc = dest->add_merged_classes();
    if (!mc.name.empty()) {
      dest_mc->set_name(mc.name);
    }
    if (mc.class_id.has_value()) {
      dest_mc->set_class_id(*mc.class_id);
    }
    if (!mc.nested_merged_classes.merged_classes.empty()) {
      SerializeMergedClasses(mc.nested_merged_classes,
                             dest_mc->set_merged_classes());
    }
  }
}

static void SerializeTopLevelMergedClasses(
    const profiling::ObfuscatedClass::MergedClasses& src,
    perfetto::protos::pbzero::ObfuscatedClass* dest) {
  if (!src.merged_classes.empty()) {
    SerializeMergedClasses(src, dest->set_merged_classes());
  }
}

void MakeDeobfuscationPackets(
    const std::string& package_name,
    const std::map<std::string, profiling::ObfuscatedClass>& mapping,
    std::function<void(const std::string&)> callback) {
  protozero::HeapBuffered<perfetto::protos::pbzero::Trace> trace;
  auto* packet = trace->add_packet();
  // TODO(fmayer): Add handling for package name and version code here so we
  // can support multiple dumps in the same trace.
  auto* proto_mapping = packet->set_deobfuscation_mapping();
  proto_mapping->set_package_name(package_name);
  for (const auto& p : mapping) {
    const std::string& obfuscated_class_name = p.first;
    const profiling::ObfuscatedClass& cls = p.second;

    auto* proto_class = proto_mapping->add_obfuscated_classes();
    proto_class->set_obfuscated_name(obfuscated_class_name);
    proto_class->set_deobfuscated_name(cls.deobfuscated_name());
    for (const auto& field_p : cls.deobfuscated_fields()) {
      const std::string& obfuscated_field_name = field_p.first;
      const std::string& deobfuscated_field_name = field_p.second;
      auto* proto_member = proto_class->add_obfuscated_members();
      proto_member->set_obfuscated_name(obfuscated_field_name);
      proto_member->set_deobfuscated_name(deobfuscated_field_name);
    }
    // Emit line-aware method mappings for R8 inline support
    for (const auto& method : cls.method_mappings()) {
      auto* proto_member = proto_class->add_obfuscated_methods();
      proto_member->set_obfuscated_name(method.obfuscated_name);
      proto_member->set_deobfuscated_name(method.deobfuscated_name);
      if (method.obfuscated_line_start.has_value()) {
        proto_member->set_obfuscated_line_start(*method.obfuscated_line_start);
      }
      if (method.obfuscated_line_end.has_value()) {
        proto_member->set_obfuscated_line_end(*method.obfuscated_line_end);
      }
      if (method.source_line_start.has_value()) {
        proto_member->set_source_line_start(*method.source_line_start);
      }
      if (method.source_line_end.has_value()) {
        proto_member->set_source_line_end(*method.source_line_end);
      }
    }
    SerializeTopLevelMergedClasses(cls.merged_classes(), proto_class);
  }
  callback(trace.SerializeAsString());
}

base::Status ReadProguardMapsToDeobfuscationPackets(
    const std::vector<ProguardMap>& maps,
    std::function<void(std::string)> fn) {
  for (const ProguardMap& map : maps) {
    const char* filename = map.filename.c_str();
    base::ScopedFstream f = base::OpenFstream(filename, base::kFopenReadFlag);
    if (!f) {
      return base::ErrStatus("failed to open ProGuard map %s (errno: %d, %s)",
                             filename, errno, strerror(errno));
    }
    profiling::ProguardParser parser;
    std::string contents;
    if (!base::ReadFileStream(*f, &contents)) {
      return base::ErrStatus("failed to read ProGuard map %s", filename);
    }
    if (!parser.AddLines(std::move(contents))) {
      return base::ErrStatus(
          "failed to parse ProGuard map %s (not a valid mapping.txt)",
          filename);
    }
    std::map<std::string, profiling::ObfuscatedClass> obfuscation_map =
        parser.ConsumeMapping();

    // TODO(fmayer): right now, we don't use the profile we are given. We can
    // filter the output to only contain the classes actually seen in the
    // profile.
    MakeDeobfuscationPackets(map.package, obfuscation_map, fn);
  }
  return base::OkStatus();
}

std::vector<ProguardMap> GetPerfettoProguardMapPath() {
  const char* env = getenv("PERFETTO_PROGUARD_MAP");
  if (env == nullptr)
    return {};
  std::vector<ProguardMap> res;
  for (base::StringSplitter sp(std::string(env), ':'); sp.Next();) {
    std::string token(sp.cur_token(), sp.cur_token_size());
    size_t eq = token.find('=');
    if (eq == std::string::npos) {
      PERFETTO_ELOG(
          "Invalid PERFETTO_PROGUARD_MAP. "
          "Expected format packagename=filename[:packagename=filename...], "
          "e.g. com.example.package1=foo.txt:com.example.package2=bar.txt.");
      return {};
    }
    res.emplace_back(ProguardMap{token.substr(0, eq), token.substr(eq + 1)});
  }
  return res;  // for Wreturn-std-move-in-c++11.
}

}  // namespace profiling
}  // namespace perfetto
