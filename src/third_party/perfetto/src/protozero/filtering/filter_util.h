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

#ifndef SRC_PROTOZERO_FILTERING_FILTER_UTIL_H_
#define SRC_PROTOZERO_FILTERING_FILTER_UTIL_H_

#include <stdint.h>

#include <list>
#include <map>
#include <optional>
#include <string>

#include "src/protozero/filtering/filter_bytecode_generator.h"

// We include this intentionally instead of forward declaring to allow
// for an easy find/replace transformation when moving to Google3.
#include <google/protobuf/descriptor.h>

namespace protozero {

// Parses a .proto message definition, recursing into its sub-messages, and
// builds up a set of Messages and Field definitions.
// Depends on libprotobuf-full and should be used only in host tools.
// See the //tools/proto_filter for an executable that wraps this class with
// a cmdline interface.
class FilterUtil {
 public:
  FilterUtil();
  ~FilterUtil();

  // Loads a message schema from a .proto file, recursing into nested types.
  // Filtering options are read from proto annotations
  // [(perfetto.protos.proto_filter)] on each field, rather than from
  // command-line arguments.
  // Args:
  // proto_file: path to the .proto file.
  // root_message: fully qualified message name (e.g., perfetto.protos.Trace).
  //     If empty, the first message in the file will be used.
  // proto_dir_path: the root for .proto includes. If empty uses CWD.
  bool LoadMessageDefinition(const std::string& proto_file,
                             const std::string& root_message,
                             const std::string& proto_dir_path);

  // Loads a message schema from a pre-compiled binary FileDescriptorSet.
  // This is simpler than LoadMessageDefinition as it doesn't need to resolve
  // proto imports at runtime - all dependencies are baked into the descriptor.
  // Args:
  // file_descriptor_set_proto: pointer to binary FileDescriptorSet data.
  // size: size of the binary data.
  // root_message: fully qualified message name (e.g., perfetto.protos.Trace).
  bool LoadFromDescriptorSet(const uint8_t* file_descriptor_set_proto,
                             size_t size,
                             const std::string& root_message);

  // Deduplicates leaf messages having the same sets of field ids.
  // It changes the internal state and affects the behavior of next calls to
  // GenerateFilterBytecode() and PrintAsText().
  void Dedupe();

  // Generates the filter bytecode for the root message previously loaded by
  // LoadMessageDefinition() using FilterBytecodeGenerator.
  // Returns the bytecode and optional v54 overlay (see FilterBytecodeGenerator
  // for details).
  // |min_version| specifies the minimum bytecode parser version to target.
  // Defaults to the latest version.
  FilterBytecodeGenerator::SerializeResult GenerateFilterBytecode(
      FilterBytecodeGenerator::BytecodeVersion min_version =
          FilterBytecodeGenerator::BytecodeVersion::kLatest);

  // Prints the list of messages and fields onto stdout in a diff-friendly text
  // format. Example:
  // PowerRails                 2 message  energy_data     PowerRails.EnergyData
  // PowerRails.RailDescriptor  1 uint32   index
  // If the optional bytecode filter is given, only the fields allowed by the
  // bytecode are printed.
  void PrintAsText(std::optional<std::string> filter_bytecode = {});

  // Resolves an array of field ids into a dot-concatenated field names.
  // E.g., [2,5,1] -> ".trace.packet.timestamp".
  std::string LookupField(const uint32_t* field_ids, size_t num_fields);

  // Like the above but the array of field is passed as a buffer containing
  // varints, e.g. "\x02\x05\0x01".
  std::string LookupField(const std::string& varint_encoded_path);

  void set_print_stream_for_testing(FILE* stream) { print_stream_ = stream; }

 private:
  struct Message {
    struct Field {
      std::string name;
      std::string type;  // "uint32", "string", "message"
      bool filter_string = false;
      // Semantic type for string fields that need filtering.
      // 0 = unspecified/unset. Only meaningful when filter_string == true.
      // Maps to SemanticType enum values from semantic_type.proto.
      uint32_t semantic_type = 0;
      // If true and semantic_type is set, include this field in v2 bytecode
      // (as simple filter_string).
      bool allow_v2_with_semantic_type = false;
      // If true and filter_string is set, include this field in v1 bytecode
      // (as simple string).
      bool allow_v1_with_filter_string = false;
      // Only when type == "message". Note that when using Dedupe() this can
      // be aliased against a different submessage which happens to have the
      // same set of field ids.
      Message* nested_type = nullptr;

      bool is_simple() const {
        return nested_type == nullptr && !filter_string;
      }
    };
    std::string full_name;  // e.g., "perfetto.protos.Foo.Bar";
    std::map<uint32_t /*field_id*/, Field> fields;

    // True if at least one field has a non-null |nested_type|.
    bool has_nested_fields = false;

    // True if at least one field has |filter_string|==true.
    bool has_filter_string_fields = false;
  };

  using DescriptorsByNameMap = std::map<std::string, Message*>;

  Message* ParseProtoDescriptor(const google::protobuf::Descriptor*,
                                DescriptorsByNameMap*);

  // list<> because pointers need to be stable.
  std::list<Message> descriptors_;

  FILE* print_stream_ = stdout;
};

}  // namespace protozero

#endif  // SRC_PROTOZERO_FILTERING_FILTER_UTIL_H_
