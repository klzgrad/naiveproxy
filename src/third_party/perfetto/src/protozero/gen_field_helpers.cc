/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "perfetto/protozero/gen_field_helpers.h"

namespace protozero {
namespace internal {
namespace gen_helpers {

void DeserializeString(const protozero::Field& field, std::string* dst) {
  field.get(dst);
}

template bool DeserializePackedRepeated<proto_utils::ProtoWireType::kVarInt,
                                        uint64_t>(const protozero::Field& field,
                                                  std::vector<uint64_t>* dst);

template bool DeserializePackedRepeated<proto_utils::ProtoWireType::kVarInt,
                                        int64_t>(const protozero::Field& field,
                                                 std::vector<int64_t>* dst);

template bool DeserializePackedRepeated<proto_utils::ProtoWireType::kVarInt,
                                        uint32_t>(const protozero::Field& field,
                                                  std::vector<uint32_t>* dst);

template bool DeserializePackedRepeated<proto_utils::ProtoWireType::kVarInt,
                                        int32_t>(const protozero::Field& field,
                                                 std::vector<int32_t>* dst);

void SerializeTinyVarInt(uint32_t field_id, bool value, Message* msg) {
  msg->AppendTinyVarInt(field_id, value);
}

template void SerializeExtendedVarInt<uint64_t>(uint32_t field_id,
                                                uint64_t value,
                                                Message* msg);

template void SerializeExtendedVarInt<uint32_t>(uint32_t field_id,
                                                uint32_t value,
                                                Message* msg);

template void SerializeFixed<double>(uint32_t field_id,
                                     double value,
                                     Message* msg);

template void SerializeFixed<float>(uint32_t field_id,
                                    float value,
                                    Message* msg);

template void SerializeFixed<uint64_t>(uint32_t field_id,
                                       uint64_t value,
                                       Message* msg);

template void SerializeFixed<int64_t>(uint32_t field_id,
                                      int64_t value,
                                      Message* msg);

template void SerializeFixed<uint32_t>(uint32_t field_id,
                                       uint32_t value,
                                       Message* msg);

template void SerializeFixed<int32_t>(uint32_t field_id,
                                      int32_t value,
                                      Message* msg);

void SerializeString(uint32_t field_id,
                     const std::string& value,
                     Message* msg) {
  msg->AppendString(field_id, value);
}

void SerializeUnknownFields(const std::string& unknown_fields, Message* msg) {
  msg->AppendRawProtoBytes(unknown_fields.data(), unknown_fields.size());
}

MessageSerializer::MessageSerializer() = default;

MessageSerializer::~MessageSerializer() = default;

std::vector<uint8_t> MessageSerializer::SerializeAsArray() {
  return msg_.SerializeAsArray();
}

std::string MessageSerializer::SerializeAsString() {
  return msg_.SerializeAsString();
}

template bool EqualsField<std::string>(const std::string&, const std::string&);

}  // namespace gen_helpers
}  // namespace internal
}  // namespace protozero
