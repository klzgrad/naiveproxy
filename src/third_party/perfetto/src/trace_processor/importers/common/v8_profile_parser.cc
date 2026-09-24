/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/importers/common/v8_profile_parser.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/util/simple_json_parser.h"

namespace perfetto::trace_processor {
namespace {

base::Status ParseCallFrame(json::SimpleJsonParser& reader,
                            V8CallFrame& frame) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "url") {
      if (auto s = reader.GetString(); s && !s->empty()) {
        frame.url = std::string(*s);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "functionName") {
      if (auto s = reader.GetString()) {
        frame.function_name = std::string(*s);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

base::Status ParseNode(json::SimpleJsonParser& reader, V8Node& node) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "id") {
      if (auto v = reader.GetUint32()) {
        node.id = *v;
      }
      return json::FieldResult::Handled{};
    }
    if (key == "parent") {
      node.parent = reader.GetUint32();
      return json::FieldResult::Handled{};
    }
    if (key == "children" && reader.IsArray()) {
      auto result = reader.CollectUint32Array();
      if (result.ok()) {
        node.children = std::move(*result);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "callFrame" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseCallFrame(reader, node.call_frame));
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

base::Status ParseNodes(json::SimpleJsonParser& reader,
                        std::vector<V8Node>& nodes) {
  return reader.ForEachArrayElement([&]() {
    if (reader.IsObject()) {
      V8Node node;
      RETURN_IF_ERROR(ParseNode(reader, node));
      nodes.push_back(std::move(node));
    }
    return base::OkStatus();
  });
}

base::Status ParseCpuProfile(json::SimpleJsonParser& reader,
                             V8Profile& profile) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "nodes" && reader.IsArray()) {
      RETURN_IF_ERROR(ParseNodes(reader, profile.nodes));
      return json::FieldResult::Handled{};
    }
    if (key == "samples" && reader.IsArray()) {
      auto result = reader.CollectUint32Array();
      if (result.ok()) {
        profile.samples = std::move(*result);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

base::Status ParseProfileFields(json::SimpleJsonParser& reader,
                                V8Profile& profile) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "startTime") {
      profile.start_time = reader.GetInt64();
      return json::FieldResult::Handled{};
    }
    if (key == "cpuProfile" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseCpuProfile(reader, profile));
      return json::FieldResult::Handled{};
    }
    if (key == "timeDeltas" && reader.IsArray()) {
      auto result = reader.CollectInt64Array();
      if (result.ok()) {
        profile.time_deltas = std::move(*result);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

}  // namespace

base::StatusOr<V8Profile> ParseV8Profile(std::string_view json_str) {
  V8Profile profile;
  json::SimpleJsonParser reader(json_str);
  RETURN_IF_ERROR(reader.Parse());
  RETURN_IF_ERROR(ParseProfileFields(reader, profile));
  return profile;
}

base::StatusOr<V8Profile> ParseV8ProfileArgs(std::string_view json_str) {
  V8Profile profile;
  json::SimpleJsonParser reader(json_str);
  RETURN_IF_ERROR(reader.Parse());

  RETURN_IF_ERROR(
      reader.ForEachField([&](std::string_view key) -> json::FieldResult {
        if (key == "data" && reader.IsObject()) {
          RETURN_IF_ERROR(ParseProfileFields(reader, profile));
          return json::FieldResult::Handled{};
        }
        return json::FieldResult::Skip{};
      }));

  return profile;
}

}  // namespace perfetto::trace_processor
