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

#include "src/profiling/deobfuscator.h"

#include <optional>
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"

#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace profiling {
namespace {

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

enum class ProguardMemberType {
  kField,
  kMethod,
};

struct ProguardMember {
  ProguardMemberType type;
  std::string obfuscated_name;
  std::string deobfuscated_name;
};

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

  ProguardMemberType member_type;
  auto paren_idx = deobfuscated_name.find('(');
  if (paren_idx != std::string::npos) {
    member_type = ProguardMemberType::kMethod;
    deobfuscated_name = deobfuscated_name.substr(0, paren_idx);
    auto colon_idx = type_name.find(':');
    if (colon_idx != std::string::npos) {
      type_name = type_name.substr(colon_idx + 1);
    }
  } else {
    member_type = ProguardMemberType::kField;
  }
  return ProguardMember{member_type, std::move(obfuscated_name),
                        std::move(deobfuscated_name)};
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

// See https://www.guardsquare.com/en/products/proguard/manual/retrace for the
// file format we are parsing.
base::Status ProguardParser::AddLine(std::string line) {
  auto first_ch_pos = line.find_first_not_of(" \t");
  if (first_ch_pos == std::string::npos || line[first_ch_pos] == '#')
    return base::Status();

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
        current_class_->AddMethod(opt_member->obfuscated_name,
                                  opt_member->deobfuscated_name);
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
    for (const auto& field_p : cls.deobfuscated_methods()) {
      const std::string& obfuscated_method_name = field_p.first;
      const std::string& deobfuscated_method_name = field_p.second;
      auto* proto_member = proto_class->add_obfuscated_methods();
      proto_member->set_obfuscated_name(obfuscated_method_name);
      proto_member->set_deobfuscated_name(deobfuscated_method_name);
    }
  }
  callback(trace.SerializeAsString());
}

bool ReadProguardMapsToDeobfuscationPackets(
    const std::vector<ProguardMap>& maps,
    std::function<void(std::string)> fn) {
  for (const ProguardMap& map : maps) {
    const char* filename = map.filename.c_str();
    base::ScopedFstream f(fopen(filename, "re"));
    if (!f) {
      PERFETTO_ELOG("Failed to open %s", filename);
      return false;
    }
    profiling::ProguardParser parser;
    std::string contents;
    PERFETTO_CHECK(base::ReadFileStream(*f, &contents));
    if (!parser.AddLines(std::move(contents))) {
      PERFETTO_ELOG("Failed to parse %s", filename);
      return false;
    }
    std::map<std::string, profiling::ObfuscatedClass> obfuscation_map =
        parser.ConsumeMapping();

    // TODO(fmayer): right now, we don't use the profile we are given. We can
    // filter the output to only contain the classes actually seen in the
    // profile.
    MakeDeobfuscationPackets(map.package, obfuscation_map, fn);
  }
  return true;
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
