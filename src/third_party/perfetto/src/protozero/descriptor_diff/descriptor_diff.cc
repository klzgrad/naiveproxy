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

#include "src/protozero/descriptor_diff/descriptor_diff.h"

#include <unordered_set>

#include "perfetto/ext/base/status_macros.h"
#include "protos/perfetto/common/descriptor.pbzero.h"

namespace protozero {
namespace {

using ::perfetto::base::StatusOr;
using ::perfetto::protos::pbzero::FileDescriptorProto;
using ::perfetto::protos::pbzero::FileDescriptorSet;
using ::protozero::proto_utils::ProtoWireType;

StatusOr<std::unordered_set<std::string_view>> GetFiles(
    std::string_view descriptor) {
  std::unordered_set<std::string_view> files;
  protozero::ProtoDecoder decoder(descriptor.data(), descriptor.size());
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() != FileDescriptorSet::kFileFieldNumber) {
      continue;
    }
    if (field.type() != ProtoWireType::kLengthDelimited) {
      return perfetto::base::ErrStatus("Error parsing descriptor");
    }
    FileDescriptorProto::Decoder file(field.as_bytes());
    files.insert(file.name().ToStdStringView());
  }
  return files;
}

}  // namespace

StatusOr<std::string> DescriptorDiff(std::string_view minuend,
                                     std::string_view subtrahend) {
  ASSIGN_OR_RETURN(auto subtrahend_files, GetFiles(subtrahend));

  std::string output;

  protozero::ProtoDecoder decoder(minuend.data(), minuend.size());
  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == FileDescriptorSet::kFileFieldNumber) {
      if (field.type() != ProtoWireType::kLengthDelimited) {
        return perfetto::base::ErrStatus("Error parsing descriptor");
      }
      FileDescriptorProto::Decoder file(field.as_bytes());
      if (subtrahend_files.count(file.name().ToStdStringView()) == 1) {
        // Skip the file. It's already included in the subtrahend.
        continue;
      }
    }
    field.SerializeAndAppendTo(&output);
  }
  return output;
}

}  // namespace protozero
