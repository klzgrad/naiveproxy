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

#include "src/protovm/rw_proto.h"

#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"

namespace perfetto {
namespace protovm {

RwProto::RwProto(Allocator* allocator)
    : allocator_{allocator}, root_{Node::Empty{}} {}

RwProto::~RwProto() {
  allocator_->DeleteReferencedData(&root_);
}

RwProto::Cursor RwProto::GetRoot() {
  return Cursor{&root_, allocator_};
}

std::string RwProto::SerializeAsString() const {
  if (root_.GetIf<Node::Empty>()) {
    return "";
  }

  if (auto* bytes = root_.GetIf<Node::Bytes>()) {
    return std::string(static_cast<char*>(bytes->data.get()), bytes->size);
  }

  protozero::HeapBuffered<protozero::Message> proto;
  auto* message = root_.GetIf<Node::Message>();
  PERFETTO_DCHECK(message);

  for (auto it = message->field_id_to_node.begin(); it; ++it) {
    auto field_id = static_cast<uint32_t>(it->key);
    SerializeField(field_id, *it->value, proto.get());
  }

  return proto.SerializeAsString();
}

void RwProto::SerializeField(uint32_t field_id,
                             const Node& node,
                             protozero::Message* proto) const {
  if (node.GetIf<Node::Empty>()) {
    return;
  }

  if (auto* bytes = node.GetIf<Node::Bytes>()) {
    proto->AppendBytes(field_id, bytes->data.get(), bytes->size);
    return;
  }

  if (auto* scalar = node.GetIf<Scalar>()) {
    if (scalar->wire_type == protozero::proto_utils::ProtoWireType::kFixed32) {
      proto->AppendFixed(field_id, static_cast<uint32_t>(scalar->value));
      return;
    }

    if (scalar->wire_type == protozero::proto_utils::ProtoWireType::kFixed64) {
      proto->AppendFixed(field_id, static_cast<uint64_t>(scalar->value));
      return;
    }

    proto->AppendVarInt(field_id, scalar->value);
    return;
  }

  if (auto* message = node.GetIf<Node::Message>()) {
    auto* message_proto =
        proto->BeginNestedMessage<protozero::Message>(field_id);

    for (auto it = message->field_id_to_node.begin(); it; ++it) {
      auto message_field_id = static_cast<uint32_t>(it->key);
      SerializeField(message_field_id, *it->value, message_proto);
    }
  }

  if (node.GetIf<Node::IndexedRepeatedField>() ||
      node.GetIf<Node::MappedRepeatedField>()) {
    const IntrusiveMap* map;
    if (auto* indexed = node.GetIf<Node::IndexedRepeatedField>()) {
      map = &indexed->index_to_node;
    } else {
      map = &node.GetIf<Node::MappedRepeatedField>()->key_to_node;
    }

    for (auto it = map->begin(); it; ++it) {
      SerializeField(field_id, *it->value, proto);
    }
  }
}

}  // namespace protovm
}  // namespace perfetto
