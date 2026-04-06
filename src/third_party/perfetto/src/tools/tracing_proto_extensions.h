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

#ifndef SRC_TOOLS_TRACING_PROTO_EXTENSIONS_H_
#define SRC_TOOLS_TRACING_PROTO_EXTENSIONS_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"

namespace perfetto {
namespace gen_proto_extensions {

using Range = std::pair<int32_t, int32_t>;

// Represents an allocation entry in a track_event_extensions.json file.
struct Allocation {
  std::string name;
  std::vector<Range> ranges;  // Each pair is [start, end] inclusive.
  std::string contact;
  std::string description;
  std::string repo;
  // Exactly one of proto or registry should be set (or neither for
  // "unallocated" entries).
  std::string proto;     // Path to a leaf .proto file.
  std::string registry;  // Path to a sub-delegation .json file.
};

// Represents a parsed track_event_extensions.json file.
struct Registry {
  // The fully-qualified proto message being extended. Currently only
  // "perfetto.protos.TrackEvent" is supported. In the future this could be
  // used to disambiguate TracePacket extensions from TrackEvent extensions.
  std::string scope;
  std::vector<Range> ranges;  // Each pair is [start, end] inclusive.
  std::vector<Allocation> allocations;
  // The path of the .json file this was loaded from (for error messages).
  std::string source_path;
};

// Parses a track_event_extensions.json file with the {"extensions": [...]}
// format. Returns one Registry per entry in the array.
base::StatusOr<std::vector<Registry>> ParseRegistryFile(
    const std::string& json_contents,
    const std::string& source_path);

// Validates a registry: checks that allocations tile the ranges exactly
// (no gaps or overlaps) and that constraints on proto/registry fields are met.
base::Status ValidateRegistry(const Registry& registry);

// Recursively walks the registry tree starting from |root_json_path|,
// compiles all referenced local .proto files, validates field numbers,
// and returns a serialized FileDescriptorSet containing only the extension
// descriptors (using the field subset from our descriptor.proto).
//
// |proto_paths| are the -I include directories for protoc.
// |root_dir| is the base directory for resolving relative paths in the JSON.
base::StatusOr<std::vector<uint8_t>> GenerateExtensionDescriptors(
    const std::string& root_json_path,
    const std::vector<std::string>& proto_paths,
    const std::string& root_dir);

}  // namespace gen_proto_extensions
}  // namespace perfetto

#endif  // SRC_TOOLS_TRACING_PROTO_EXTENSIONS_H_
